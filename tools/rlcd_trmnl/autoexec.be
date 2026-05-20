import json
import path
import string

class TrmnlDashboard
  static DEFAULT_API_URL = "https://trmnl.com/api/display"
  static CONFIG_FILE = "/trmnl_config.json"
  static FRAME_CURRENT = "/trmnl_frame.png"
  static FRAME_NEXT = "/trmnl_frame.next"
  static ROTATION = 270
  static WIDTH = 800
  static HEIGHT = 600
  static START_DELAY_MS = 5000
  static MIN_REFRESH_S = 60
  static DEFAULT_REFRESH_S = 300

  var next_at
  var busy
  var refresh_s
  var api_url
  var send_auth
  var trmnl_id
  var trmnl_token

  def init()
    self.busy = false
    self.refresh_s = self.DEFAULT_REFRESH_S
    self.api_url = self.DEFAULT_API_URL
    self.send_auth = true
    self.next_at = tasmota.millis(self.START_DELAY_MS)
    if global.trmnl_dashboard
      global.trmnl_dashboard.stop()
    end
    tasmota.add_driver(global.trmnl_dashboard := self)
    tasmota.log("TRMNL: dashboard driver armed", 2)
  end

  def stop()
    tasmota.remove_driver(self)
  end

  def remove_file(name)
    try
      if path.exists(name)
        path.remove(name)
      end
    except .. as e, m
      tasmota.log(format("TRMNL: remove %s failed: %s %s", name, e, m), 2)
    end
  end

  def schedule(seconds)
    seconds = int(seconds)
    if seconds < self.MIN_REFRESH_S
      seconds = self.MIN_REFRESH_S
    end
    self.refresh_s = seconds
    self.next_at = tasmota.millis(seconds * 1000)
    tasmota.log(format("TRMNL: next refresh in %d seconds", seconds), 2)
  end

  def every_50ms()
    if self.busy || self.next_at == nil
      return
    end
    if tasmota.time_reached(self.next_at)
      self.next_at = nil
      self.fetch()
    end
  end

  def load_config()
    if !path.exists(self.CONFIG_FILE)
      raise "config_error", self.CONFIG_FILE + " is missing"
    end
    var f = open(self.CONFIG_FILE, "r")
    var cfg = json.load(f.read())
    f.close()
    if cfg == nil
      raise "config_error", self.CONFIG_FILE + " is invalid json"
    end
    var cfg_api_url = cfg.find("api_url")
    if cfg_api_url != nil
      self.api_url = cfg_api_url
    else
      self.api_url = self.DEFAULT_API_URL
    end
    var cfg_send_auth = cfg.find("send_auth")
    if cfg_send_auth != nil
      self.send_auth = cfg_send_auth
    else
      self.send_auth = true
    end
    if self.send_auth
      self.trmnl_id = cfg.find("id")
      self.trmnl_token = cfg.find("token")
    else
      self.trmnl_id = nil
      self.trmnl_token = nil
    end
    if self.send_auth && (self.trmnl_id == nil || self.trmnl_token == nil)
      raise "config_error", self.CONFIG_FILE + " must contain id and token"
    end
  end

  def display_headers(client)
    if self.send_auth
      client.add_header("ID", self.trmnl_id)
      client.add_header("Access-Token", self.trmnl_token)
    end
    client.add_header("Refresh-Rate", str(self.refresh_s))
    client.add_header("Battery-Voltage", "4.2")
    client.add_header("FW-Version", "tasmota-lvgl-trmnl")
    client.add_header("RSSI", "100")
    client.add_header("Width", str(self.WIDTH))
    client.add_header("Height", str(self.HEIGHT))
  end

  def native_https_available(name)
    return global.contains(name)
  end

  def display_headers_json(accept)
    var h = {
      "Accept": accept,
      "Connection": "close",
      "User-Agent": "Tasmota-LVGL-TRMNL/0.1",
      "Refresh-Rate": str(self.refresh_s),
      "Battery-Voltage": "4.2",
      "FW-Version": "tasmota-lvgl-trmnl",
      "RSSI": "100",
      "Width": str(self.WIDTH),
      "Height": str(self.HEIGHT)
    }
    if self.send_auth
      h["ID"] = self.trmnl_id
      h["Access-Token"] = self.trmnl_token
    end
    return json.dump(h)
  end

  def native_get(url, accept)
    if !string.startswith(url, "https://") || !self.native_https_available("idf_https_get")
      return nil
    end
    var raw = idf_https_get(url, self.display_headers_json(accept))
    var res = json.load(raw)
    if res == nil
      raise "connection_error", "native HTTPS GET returned invalid json"
    end
    if !res.find("ok")
      var err = res.find("error")
      if err == nil err = "native HTTPS GET failed" end
      raise "connection_error", err
    end
    var status = res.find("status")
    if status != 200
      raise "connection_error", format("native HTTPS GET status %d", status)
    end
    return res.find("body")
  end

  def native_download(url, accept, file_path)
    if !string.startswith(url, "https://") || !self.native_https_available("idf_https_download")
      return nil
    end
    var raw = idf_https_download(url, self.display_headers_json(accept), file_path)
    var res = json.load(raw)
    if res == nil
      raise "connection_error", "native HTTPS download returned invalid json"
    end
    if !res.find("ok")
      var err = res.find("error")
      if err == nil err = "native HTTPS download failed" end
      raise "connection_error", err
    end
    return res.find("bytes")
  end

  def webclient_get(url, accept)
    var api = webclient()
    api.set_follow_redirects(true)
    api.set_timeouts(20000, 5000)
    self.display_headers(api)
    api.add_header("Accept", accept)
    api.begin(url)
    var code = api.GET()
    if code != 200
      api.close()
      raise "connection_error", format("display api status %d", code)
    end
    var body = api.get_string()
    api.close()
    return body
  end

  def webclient_download(url, file_path)
    var img = webclient()
    img.set_follow_redirects(true)
    img.set_timeouts(20000, 5000)
    img.begin(url)
    var code = img.GET()
    if code != 200
      img.close()
      raise "connection_error", format("image status %d", code)
    end
    var written = img.write_file(file_path)
    img.close()
    if written <= 0
      raise "io_error", "image write failed"
    end
    return written
  end

  def fetch()
    self.busy = true
    var next_refresh = self.DEFAULT_REFRESH_S
    try
      self.load_config()
      var body = self.native_get(self.api_url, "application/json")
      if body == nil
        body = self.webclient_get(self.api_url, "application/json")
      end

      var info = json.load(body)
      if info == nil
        raise "value_error", "display api returned invalid json"
      end
      var image_url = info.find("image_url")
      if image_url == nil
        raise "value_error", "display api response has no image_url"
      end
      var api_refresh = info.find("refresh_rate")
      if api_refresh != nil
        next_refresh = int(api_refresh)
      end

      self.remove_file(self.FRAME_NEXT)
      var written = self.native_download(image_url, "image/png,*/*", self.FRAME_NEXT)
      if written == nil
        if string.startswith(image_url, "https://trmnl.s3.")
          image_url = "http://" + image_url[8..]
        end
        written = self.webclient_download(image_url, self.FRAME_NEXT)
      end

      lv.start()
      var rendered = lv.trmnl_show_png(self.FRAME_NEXT, self.ROTATION)
      self.remove_file(self.FRAME_CURRENT)
      if !path.rename(self.FRAME_NEXT, self.FRAME_CURRENT)
        raise "io_error", "frame rename failed"
      end
      tasmota.log(format("TRMNL: displayed %s from %d bytes", rendered, written), 2)
    except .. as e, m
      tasmota.log(format("TRMNL: refresh failed: %s %s", e, m), 2)
      self.remove_file(self.FRAME_NEXT)
      next_refresh = self.MIN_REFRESH_S
    end
    self.busy = false
    self.schedule(next_refresh)
  end
end

TrmnlDashboard()
