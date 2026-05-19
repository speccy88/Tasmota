import tasmoclaw_util

class TasmoClawLLM
  def call_chat(cfg, messages)
    if cfg['api_key']=='' return {'ok':false,'error':'Missing API key'} end
    if cfg['api_url']=='' return {'ok':false,'error':'Missing api_url'} end
    if cfg['model']=='' return {'ok':false,'error':'Missing model'} end
    var payload = {'model':cfg['model'],'messages':messages,'temperature':cfg['temperature'],'max_tokens':cfg['max_tokens'],'stream':false,'thinking':{'type':cfg['thinking']}}
    if cfg['thinking']=='enabled' payload['reasoning_effort']=cfg['reasoning_effort'] end
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
      import json
      var o = json.load(body)
      return {'ok':true,'content':o['choices'][0]['message']['content'],'raw':o}
    except .. as e,m
    end
    var s='"content":"'
    var i=string.find(body,s)
    if i<0 return {'ok':false,'error':'JSON parse failure'} end
    var start=i+size(s); var j=string.find(body,'"',start)
    if j<0 return {'ok':false,'error':'JSON parse failure'} end
    return {'ok':true,'content':string.slice(body,start,j)}
  end
end
