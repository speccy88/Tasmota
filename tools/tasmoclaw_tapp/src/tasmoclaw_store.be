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
      'https_transport':'webclient',
      'api_key':'',
      'temperature':0.2,
      'max_tokens':700,
      'thinking':'omit',
      'reasoning_effort':'high',
      'max_tool_iterations':3,
      'history_limit':6,
      'prompt_mode':'compact',
      'context_byte_limit':5200,
      'auto_approve_tools':false,
      'workspace':'/tasmoclaw/',
      'system_extra':''
    }
  end

  def read_file(file)
    try
      if path.exists(file) != true
        tasmoclaw_util.debug('store read missing file=' + str(file))
        return nil
      end
      var f = open(file, 'r')
      var data = f.read()
      f.close()
      tasmoclaw_util.debug('store read file=' + str(file) + ' bytes=' + str(data == nil ? 0 : size(data)))
      return data
    except .. as e,m
      self.last_error = 'read failed for ' + file + ': ' + str(m)
      tasmoclaw_util.debug('store read failed file=' + str(file) + ' error=' + str(m))
      return nil
    end
  end

  def write_file(file, data)
    try
      var f = open(file, 'w')
      f.write(data)
      f.close()
      self.last_error = ''
      tasmoclaw_util.debug('store write file=' + str(file) + ' bytes=' + str(data == nil ? 0 : size(data)))
      return {'ok':true}
    except .. as e,m
      self.last_error = 'write failed for ' + file + ': ' + str(m)
      tasmoclaw_util.debug('store write failed file=' + str(file) + ' error=' + str(m))
      return {'ok':false,'error':self.last_error}
    end
  end

  def remove_file(file)
    try
      if path.exists(file) == true
        path.remove(file)
      end
      self.last_error = ''
      tasmoclaw_util.debug('store remove file=' + str(file))
      return {'ok':true}
    except .. as e,m
      self.last_error = 'remove failed for ' + file + ': ' + str(m)
      tasmoclaw_util.debug('store remove failed file=' + str(file) + ' error=' + str(m))
      return {'ok':false,'error':self.last_error}
    end
  end

  def load_config()
    tasmoclaw_util.debug('store load_config start')
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
      tasmoclaw_util.debug('store config parse failed; trying persist error=' + str(m))

      try
        var raw2 = persist.get(self.config_file)
        if raw2 != nil
          var obj2 = json.load(raw2)
          for k:obj2.keys()
            cfg[k]=obj2[k]
          end
          tasmoclaw_util.debug('store config loaded from persist')
        end
      except .. as e2,m2
        tasmoclaw_util.debug('store config persist fallback failed: ' + str(m2))
      end
    end

    tasmoclaw_util.debug('store load_config done transport=' + str(cfg.find('https_transport')) + ' model=' + str(cfg.find('model')))
    return cfg
  end

  def save_config(cfg)
    var r = self.write_file(self.config_file, tasmoclaw_util.json_encode(cfg))

    if !r['ok']
      try
        persist.set(self.config_file, tasmoclaw_util.json_encode(cfg))
        tasmoclaw_util.debug('store save_config used persist fallback warning=' + str(r['error']))
        return {'ok':true,'fallback':'persist','warning':r['error']}
      except .. as e,m
        tasmoclaw_util.debug('store save_config persist fallback failed: ' + str(m))
      end
    end

    return r
  end

  def load_history()
    try
      var raw = self.read_file(self.history_file)
      if raw != nil
        var h = json.load(raw)
        if h != nil && type(h) == 'list'
          tasmoclaw_util.debug('store load_history count=' + str(size(h)))
          return h
        end
        tasmoclaw_util.debug('store load_history ignored non-list history file')
      end
    except .. as e,m
      self.last_error = 'history parse failed: ' + str(m)
      tasmoclaw_util.debug('store history parse failed; trying persist error=' + str(m))

      try
        var raw2 = persist.get(self.history_file)
        if raw2 != nil
          var h2 = json.load(raw2)
          if h2 != nil && type(h2) == 'list'
            tasmoclaw_util.debug('store history loaded from persist count=' + str(size(h2)))
            return h2
          end
          tasmoclaw_util.debug('store history persist ignored non-list value')
        end
      except .. as e2,m2
        tasmoclaw_util.debug('store history persist fallback failed: ' + str(m2))
      end
    end

    tasmoclaw_util.debug('store load_history default empty')
    return []
  end

  def save_history(h)
    var r = self.write_file(self.history_file, tasmoclaw_util.json_encode(h))

    if !r['ok']
      try
        persist.set(self.history_file, tasmoclaw_util.json_encode(h))
        tasmoclaw_util.debug('store save_history used persist fallback warning=' + str(r['error']))
        return {'ok':true,'fallback':'persist','warning':r['error']}
      except .. as e,m
        tasmoclaw_util.debug('store save_history persist fallback failed: ' + str(m))
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
          tasmoclaw_util.debug('store load_pending ignored non-map pending')
          return nil
        end

        tasmoclaw_util.debug('store load_pending found tool=' + str(p.find('tool')))
        return p
      end
    except .. as e,m
      self.last_error = 'pending parse failed: ' + str(m)
      tasmoclaw_util.debug('store pending parse failed; trying persist error=' + str(m))

      try
        var raw2 = persist.get(self.pending_file)
        if raw2 != nil
          var p2 = json.load(raw2)
          tasmoclaw_util.debug('store pending loaded from persist')
          return p2
        end
      except .. as e2,m2
        tasmoclaw_util.debug('store pending persist fallback failed: ' + str(m2))
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
          tasmoclaw_util.debug('store save_pending remove used persist fallback warning=' + str(r['error']))
          return {'ok':true,'fallback':'persist','warning':r['error']}
        except .. as e,m
          tasmoclaw_util.debug('store save_pending remove persist fallback failed: ' + str(m))
        end
      end

      return r
    else
      var r2 = self.write_file(self.pending_file, tasmoclaw_util.json_encode(p))

      if !r2['ok']
        try
          persist.set(self.pending_file, tasmoclaw_util.json_encode(p))
          tasmoclaw_util.debug('store save_pending used persist fallback warning=' + str(r2['error']))
          return {'ok':true,'fallback':'persist','warning':r2['error']}
        except .. as e2,m2
          tasmoclaw_util.debug('store save_pending persist fallback failed: ' + str(m2))
        end
      end

      return r2
    end
  end

  def ensure_workspace()
    tasmoclaw_util.debug('store ensure_workspace start')
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

      tasmoclaw_util.debug('store ensure_workspace done fallback=false')
      return {'ok':true,'fallback':false}

    except .. as e,m
      self.workspace_fallback = true
      self.last_error = 'workspace mkdir failed: ' + str(m)
      tasmoclaw_util.debug('store ensure_workspace failed: ' + str(m))

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
