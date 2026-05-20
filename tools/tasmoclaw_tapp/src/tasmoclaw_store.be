import persist
import json
import path
import tasmoclaw_util

class TasmoClawStore
  var config_file, history_file, pending_file, workspace_fallback, last_error

  def init()
    self.config_file = '/tasmoclaw_config.json'
    self.history_file = '/tasmoclaw_history.json'
    self.pending_file = '/tasmoclaw_pending.json'
    self.workspace_fallback = false
    self.last_error = ''
  end

  def default_config()
    return {
      'enabled': true,
      'provider':'deepseek',
      'api_url':'https://api.deepseek.com/chat/completions',
      'model':'deepseek-v4-flash',
      'model_flash':'deepseek-v4-flash',
      'model_pro':'deepseek-v4-pro',
      'api_key':'',
      'temperature':0.2,
      'max_tokens':900,
      'thinking':'omit',
      'reasoning_effort':'high',
      'max_tool_iterations':3,
      'history_limit':8,
      'auto_approve_tools':false,
      'workspace':'/tasmoclaw/',
      'system_extra':''
    }
  end

  def read_file(file)
    try
      if path.exists(file) != true
        return nil
      end
      var f = open(file, 'r')
      var data = f.read()
      f.close()
      return data
    except .. as e,m
      self.last_error = 'read failed for ' + file + ': ' + str(m)
      return nil
    end
  end

  def write_file(file, data)
    try
      var f = open(file, 'w')
      f.write(data)
      f.close()
      self.last_error = ''
      return {'ok':true}
    except .. as e,m
      self.last_error = 'write failed for ' + file + ': ' + str(m)
      return {'ok':false,'error':self.last_error}
    end
  end

  def remove_file(file)
    try
      if path.exists(file) == true
        path.remove(file)
      end
      self.last_error = ''
      return {'ok':true}
    except .. as e,m
      self.last_error = 'remove failed for ' + file + ': ' + str(m)
      return {'ok':false,'error':self.last_error}
    end
  end

  def load_config()
    var cfg = self.default_config()

    try
      var raw = self.read_file(self.config_file)
      if raw != nil
        var obj = json.load(raw)
        for k:obj.keys()
          cfg[k]=obj[k]
        end
      end
    except .. as e,m
      self.last_error = 'config parse failed: ' + str(m)

      try
        var raw2 = persist.get(self.config_file)
        if raw2 != nil
          var obj2 = json.load(raw2)
          for k:obj2.keys()
            cfg[k]=obj2[k]
          end
        end
      except .. as e2,m2
      end
    end

    return cfg
  end

  def save_config(cfg)
    var r = self.write_file(self.config_file, tasmoclaw_util.json_encode(cfg))

    if !r['ok']
      try
        persist.set(self.config_file, tasmoclaw_util.json_encode(cfg))
        return {'ok':true,'fallback':'persist','warning':r['error']}
      except .. as e,m
      end
    end

    return r
  end

  def load_history()
    try
      var raw = self.read_file(self.history_file)
      if raw != nil
        return json.load(raw)
      end
    except .. as e,m
      self.last_error = 'history parse failed: ' + str(m)

      try
        var raw2 = persist.get(self.history_file)
        if raw2 != nil
          return json.load(raw2)
        end
      except .. as e2,m2
      end
    end

    return []
  end

  def save_history(h)
    var r = self.write_file(self.history_file, tasmoclaw_util.json_encode(h))

    if !r['ok']
      try
        persist.set(self.history_file, tasmoclaw_util.json_encode(h))
        return {'ok':true,'fallback':'persist','warning':r['error']}
      except .. as e,m
      end
    end

    return r
  end

  def load_pending()
    try
      var raw = self.read_file(self.pending_file)

      if raw != nil && size(raw) > 0
        var p = json.load(raw)

        if p == nil || type(p) != 'map'
          return nil
        end

        return p
      end
    except .. as e,m
      self.last_error = 'pending parse failed: ' + str(m)

      try
        var raw2 = persist.get(self.pending_file)
        if raw2 != nil
          return json.load(raw2)
        end
      except .. as e2,m2
      end
    end

    return nil
  end

  def save_pending(p)
    if p == nil
      var r = self.remove_file(self.pending_file)

      if !r['ok']
        try
          persist.remove(self.pending_file)
          return {'ok':true,'fallback':'persist','warning':r['error']}
        except .. as e,m
        end
      end

      return r
    else
      var r2 = self.write_file(self.pending_file, tasmoclaw_util.json_encode(p))

      if !r2['ok']
        try
          persist.set(self.pending_file, tasmoclaw_util.json_encode(p))
          return {'ok':true,'fallback':'persist','warning':r2['error']}
        except .. as e2,m2
        end
      end

      return r2
    end
  end

  def ensure_workspace()
    self.workspace_fallback = false

    try
      if path.exists('/tasmoclaw') != true
        path.mkdir('/tasmoclaw')
      end

      if path.exists('/tasmoclaw/berry') != true
        path.mkdir('/tasmoclaw/berry')
      end

      if path.exists('/tasmoclaw/logs') != true
        path.mkdir('/tasmoclaw/logs')
      end

      return {'ok':true,'fallback':false}

    except .. as e,m
      self.workspace_fallback = true
      self.last_error = 'workspace mkdir failed: ' + str(m)

      try
        tasmota.log('TasmoClaw: ' + self.last_error, 2)
      except .. as e2,m2
      end

      return {'ok':false,'fallback':true,'error':self.last_error}
    end
  end

end

var tasmoclaw_store = module("tasmoclaw_store")

tasmoclaw_store.create = def()
  return TasmoClawStore()
end

return tasmoclaw_store
