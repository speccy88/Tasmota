import json
import string
import tasmoclaw_util

class TasmoClawLLM
  def call_chat(cfg, messages)
    if cfg.find('api_key') == nil || cfg['api_key'] == '' return {'ok':false,'error':'Missing DeepSeek API key'} end
    if cfg.find('api_url') == nil || cfg['api_url'] == '' return {'ok':false,'error':'Missing api_url'} end
    if cfg.find('model') == nil || cfg['model'] == '' return {'ok':false,'error':'Missing model'} end
    var max_tokens = cfg.find('max_tokens')
    if max_tokens == nil max_tokens = 900 end
    var payload = {'model':cfg['model'],'messages':messages,'temperature':cfg['temperature'],'max_tokens':max_tokens,'stream':false}
    var thinking = cfg.find('thinking')
    if thinking == nil thinking = 'disabled' end
    if thinking == 'enabled'
      payload['thinking'] = {'type':'enabled'}
      payload['reasoning_effort'] = cfg.find('reasoning_effort') == nil ? 'high' : cfg['reasoning_effort']
    elif thinking == 'disabled'
      payload['thinking'] = {'type':'disabled'}
    end
    var cl=nil
    try
      cl = webclient()
    except .. as e,m
      return {'ok':false,'error':'webclient unavailable'}
    end
    try
      cl.begin(cfg['api_url'])
      cl.add_header('Content-Type','application/json')
      cl.add_header('Authorization','Bearer '+cfg['api_key'])
      cl.add_header('User-Agent','TasmoClaw/0.1')
      var code = cl.POST(tasmoclaw_util.json_encode(payload))
      var body = cl.get_string()
      cl.close()
      if code < 200 || code >= 300 return {'ok':false,'error':'HTTP '+str(code),'body':body} end
      if body==nil || size(body)==0 return {'ok':false,'error':'empty response'} end
      if size(body)>24000 return {'ok':false,'error':'oversized response'} end
      return self.parse_response(body)
    except .. as e,m
      try cl.close() except .. as e2,m2 end
      return {'ok':false,'error':'request failed: '+str(m)}
    end
  end
  def parse_response(body)
    try
      var o = json.load(body)
      if o.find('choices') == nil || size(o['choices']) == 0 return {'ok':false,'error':'DeepSeek response missing choices'} end
      var msg = o['choices'][0]['message']
      if msg == nil || msg.find('content') == nil return {'ok':false,'error':'DeepSeek response missing message content'} end
      return {'ok':true,'content':msg['content'],'raw':o}
    except .. as e,m
      return {'ok':false,'error':'JSON parse failure: '+str(m),'body':tasmoclaw_util.preview(body, 240)}
    end
  end
end

var tasmoclaw_llm = module("tasmoclaw_llm")
tasmoclaw_llm.create = def()
  return TasmoClawLLM()
end
return tasmoclaw_llm
