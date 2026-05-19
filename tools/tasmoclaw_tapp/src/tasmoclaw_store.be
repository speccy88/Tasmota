import tasmota
import persist
import tasmoclaw_util

class TasmoClawStore
  var config_file, history_file, pending_file

  def init()
    self.config_file = '/tasmoclaw_config.json'
    self.history_file = '/tasmoclaw_history.json'
    self.pending_file = '/tasmoclaw_pending.json'
  end

  def default_config()
    return {
      'enabled': true, 'provider':'deepseek', 'api_url':'https://api.deepseek.com/chat/completions',
      'model':'deepseek-v4-flash', 'model_flash':'deepseek-v4-flash', 'model_pro':'deepseek-v4-pro',
      'api_key':'', 'temperature':0.2, 'max_tokens':900, 'thinking':'disabled', 'reasoning_effort':'high',
      'max_tool_iterations':3, 'history_limit':8, 'workspace':'/tasmoclaw/', 'system_extra':''
    }
  end

  def load_config()
    var cfg = self.default_config()
    try
      var raw = persist.get(self.config_file)
      if raw != nil
        import json
        var obj = json.load(raw)
        for k:v in obj cfg[k]=v end
      end
    except .. as e,m
    end
    return cfg
  end
  def save_config(cfg) persist.set(self.config_file, tasmoclaw_util.json_encode(cfg)); return true end
  def load_history()
    try
      var raw = persist.get(self.history_file)
      if raw != nil
        import json
        return json.load(raw)
      end
    except .. as e,m
    end
    return []
  end
  def save_history(h) persist.set(self.history_file, tasmoclaw_util.json_encode(h)); return true end
  def load_pending()
    try
      var raw = persist.get(self.pending_file)
      if raw != nil
        import json
        return json.load(raw)
      end
    except .. as e,m
    end
    return nil
  end
  def save_pending(p)
    if p == nil persist.remove(self.pending_file) else persist.set(self.pending_file, tasmoclaw_util.json_encode(p)) end
  end
end
