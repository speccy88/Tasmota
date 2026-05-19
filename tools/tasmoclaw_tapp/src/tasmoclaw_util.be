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
    if t == 'list' return self.json_encode_list_simple(v) end
    if t == 'map' return self.json_encode_map_simple(v) end
    return self.json_quote(str(v))
  end

  def json_encode_list_simple(l)
    var out = '['
    var first = true
    for v:l
      if !first out += ',' end
      out += self.json_encode(v)
      first = false
    end
    return out + ']'
  end

  def json_encode_map_simple(m)
    var out = '{'
    var first = true
    for k:v in m
      if !first out += ',' end
      out += self.json_quote(str(k)) + ':' + self.json_encode(v)
      first = false
    end
    return out + '}'
  end
end

var tasmoclaw_util = TasmoClawUtil()
