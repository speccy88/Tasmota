import string

class TasmoClawUtil
  def json_escape_string(s)
    if s == nil return '' end
    s = string.replace(s, '\\', '\\\\')
    s = string.replace(s, '"', '\\"')
    s = string.replace(s, '\n', '\\n')
    s = string.replace(s, '\r', '\\r')
    s = string.replace(s, '\t', '\\t')
    return s
  end

  def json_quote(s)
    return '"' + self.json_escape_string(s) + '"'
  end

  def json_encode(v)
    import json
    try
      return json.dump(v)
    except .. as e,m
    end
    if v == nil return 'null' end
    var t = type(v)
    if t == 'string' return self.json_quote(v) end
    if t == 'bool' return v ? 'true' : 'false' end
    if t == 'real' || t == 'int' return str(v) end
    if t == 'list'
      var out = '['
      var first = true
      for item:v
        if !first out += ',' end
        out += self.json_encode(item)
        first = false
      end
      return out + ']'
    end
    if t == 'map'
      var out2 = '{'
      var first2 = true
      for k:v.keys()
        if !first2 out2 += ',' end
        out2 += self.json_quote(str(k)) + ':' + self.json_encode(v[k])
        first2 = false
      end
      return out2 + '}'
    end
    return self.json_quote(str(v))
  end

  def preview(s, limit)
    if s == nil return '' end
    if size(s) <= limit return s end
    return s[0..limit-1]
  end
end

var _util = TasmoClawUtil()
var tasmoclaw_util = module("tasmoclaw_util")
tasmoclaw_util.json_encode = def(v) return _util.json_encode(v) end
tasmoclaw_util.preview = def(s, limit) return _util.preview(s, limit) end
return tasmoclaw_util
