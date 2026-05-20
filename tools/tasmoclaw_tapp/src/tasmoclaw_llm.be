import json
import string
import tasmoclaw_util

class TasmoClawLLM

  def call_chat(cfg, messages)
    if cfg.find('api_key') == nil || cfg['api_key'] == ''
      return {'ok':false,'error':'Missing DeepSeek API key'}
    end

    if cfg.find('api_url') == nil || cfg['api_url'] == ''
      return {'ok':false,'error':'Missing api_url'}
    end

    if cfg.find('model') == nil || cfg['model'] == ''
      return {'ok':false,'error':'Missing model'}
    end

    var max_tokens = cfg.find('max_tokens')
    if max_tokens == nil
      max_tokens = 900
    end

    var temperature = cfg.find('temperature')
    if temperature == nil
      temperature = 0.2
    end

    var payload = {
      'model': cfg['model'],
      'messages': messages,
      'temperature': temperature,
      'max_tokens': max_tokens,
      'stream': false
    }

    var thinking = cfg.find('thinking')
    if thinking == nil
      thinking = 'omit'
    end

    if thinking == 'enabled'
      payload['thinking'] = {'type':'enabled'}
      payload['reasoning_effort'] = cfg.find('reasoning_effort') == nil ? 'high' : cfg['reasoning_effort']
    elif thinking == 'disabled'
      payload['thinking'] = {'type':'disabled'}
    end

    var payload_s = tasmoclaw_util.json_encode(payload)
    var headers = {
      'Content-Type':'application/json',
      'Accept':'application/json',
      'Connection':'close',
      'Authorization':'Bearer '+cfg['api_key'],
      'User-Agent':'TasmoClaw/0.1'
    }
    var headers_s = tasmoclaw_util.json_encode(headers)

    var native_missing = nil
    try
      var raw = idf_https_post(cfg['api_url'], headers_s, payload_s)
      var nr = json.load(raw)

      if !nr['ok']
        return {
          'ok':false,
          'transport':'esp_http_client',
          'status':nr.find('status'),
          'stage':nr.find('stage'),
          'esp_err':nr.find('esp_err'),
          'error':nr.find('error') == nil ? 'ESP-IDF HTTPS request failed' : nr['error'],
          'body':tasmoclaw_util.preview(nr.find('body'), 500)
        }
      end

      var status = nr.find('status')
      var body = nr.find('body')

      if status == nil
        status = 0
      end

      if status < 200 || status >= 300
        return {
          'ok':false,
          'transport':'esp_http_client',
          'status':status,
          'error':'HTTP '+str(status),
          'body':tasmoclaw_util.preview(body, 500)
        }
      end

      if body == nil || size(body) == 0
        return {'ok':false,'transport':'esp_http_client','status':status,'error':'empty response'}
      end

      if nr.find('truncated') == true
        return {'ok':false,'transport':'esp_http_client','status':status,'error':'oversized response','bytes':nr.find('bytes')}
      end

      var pr = self.parse_response(body)
      pr['transport'] = 'esp_http_client'
      pr['status'] = status
      return pr

    except .. as e_native,m_native
      native_missing = str(m_native)
    end

    return self.call_chat_webclient(cfg, payload_s, native_missing)
  end

  def call_chat_webclient(cfg, payload_s, native_missing)
    var cl = nil

    try
      cl = webclient()
    except .. as e,m
      return {
        'ok':false,
        'transport':'none',
        'error':'ESP-IDF HTTPS bridge idf_https_post is not available. Rebuild firmware with USE_TASMOCLAW_HTTPS. webclient unavailable: '+str(m),
        'native_error':native_missing
      }
    end

    try
      cl.begin(cfg['api_url'])

      try
        cl.set_timeouts(45000, 15000)
      except .. as e_to,m_to
      end

      try
        cl.use_http10(true)
      except .. as e_http10,m_http10
      end

      cl.add_header('Content-Type','application/json')
      cl.add_header('Accept','application/json')
      cl.add_header('Connection','close')
      cl.add_header('Authorization','Bearer '+cfg['api_key'])
      cl.add_header('User-Agent','TasmoClaw/0.1')

      var code = cl.POST(payload_s)

      if code < 0
        try
          cl.close()
        except .. as e_close,m_close
        end
        return {
          'ok': false,
          'transport':'webclient',
          'status':code,
          'error': 'HTTP '+str(code)+' from Tasmota webclient before receiving a server response',
          'hint': 'Likely DNS, Wi-Fi, TLS/HTTPS, timeout, heap, unsupported cipher, or webclient build issue.',
          'api_url': cfg['api_url'],
          'payload_bytes': size(payload_s),
          'model': cfg['model'],
          'native_error':native_missing
        }
      end

      var body = cl.get_string()
      cl.close()

      if code < 200 || code >= 300
        return {
          'ok':false,
          'transport':'webclient',
          'status':code,
          'error':'HTTP '+str(code),
          'body':tasmoclaw_util.preview(body, 500)
        }
      end

      if body == nil || size(body) == 0
        return {'ok':false,'transport':'webclient','status':code,'error':'empty response'}
      end

      if size(body) > 24000
        return {'ok':false,'transport':'webclient','status':code,'error':'oversized response','bytes':size(body)}
      end

      var pr = self.parse_response(body)
      pr['transport'] = 'webclient'
      pr['status'] = code
      return pr

    except .. as e,m
      try
        cl.close()
      except .. as e2,m2
      end
      return {'ok':false,'transport':'webclient','error':'request failed: '+str(m),'native_error':native_missing}
    end
  end

  def parse_response(body)
    try
      var o = json.load(body)

      if o.find('choices') == nil || size(o['choices']) == 0
        return {'ok':false,'error':'DeepSeek response missing choices','body':tasmoclaw_util.preview(body, 500)}
      end

      var msg = o['choices'][0]['message']

      if msg == nil || msg.find('content') == nil
        return {'ok':false,'error':'DeepSeek response missing message content','body':tasmoclaw_util.preview(body, 500)}
      end

      return {'ok':true,'content':msg['content'],'raw':o}

    except .. as e,m
      return {'ok':false,'error':'JSON parse failure: '+str(m),'body':tasmoclaw_util.preview(body, 500)}
    end
  end

end

var tasmoclaw_llm = module("tasmoclaw_llm")

tasmoclaw_llm.create = def()
  return TasmoClawLLM()
end

return tasmoclaw_llm
