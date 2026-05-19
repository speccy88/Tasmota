import tasmota
import webserver
import json
import tasmoclaw_store
import tasmoclaw_tools
import tasmoclaw_llm
import tasmoclaw_ui
import tasmoclaw_prompt
import tasmoclaw_util

var _driver = nil

class TasmoClawDriver : Driver
  var store, tools, llm, ui, cfg, history, pending
  def init()
    self.store = TasmoClawStore(); self.tools = TasmoClawTools(); self.llm = TasmoClawLLM(); self.ui = TasmoClawUI()
    self.cfg = self.store.load_config(); self.history = self.store.load_history(); self.pending=self.store.load_pending()
    self.ensure_cmds()
  end
  def ensure_cmds()
    tasmota.add_cmd('TasmoClaw', /cmd,idx,payload -> tasmota.resp_cmnd(tasmoclaw_util.json_encode(self.status_obj())))
    tasmota.add_cmd('TasmoClawReset', /cmd,idx,payload -> self.history=[]; self.pending=nil; self.store.save_history(self.history); self.store.save_pending(nil); tasmota.resp_cmnd('OK'))
    tasmota.add_cmd('TasmoClawTest', /cmd,idx,payload -> tasmota.resp_cmnd('{"ok":true}'))
  end
  def web_add_console_button() webserver.content_button('TasmoClaw','/tasmoclaw') end
  def web_add_management_button() webserver.content_button('TasmoClaw','/tasmoclaw') end
  def web_add_handler()
    webserver.on('/tasmoclaw', / -> self.ui.chat_page())
    webserver.on('/tasmoclaw/config', / -> self.page_config())
    webserver.on('/tasmoclaw/api/chat', / -> self.api_chat(), webserver.HTTP_POST)
    webserver.on('/tasmoclaw/api/config', / -> self.api_config())
    webserver.on('/tasmoclaw/api/status', / -> self.api_json(self.status_obj()))
    webserver.on('/tasmoclaw/api/tools', / -> self.api_json({'ok':true,'tools':self.tools.registry()}))
    webserver.on('/tasmoclaw/api/history', / -> self.api_json({'ok':true,'history':self.history}))
    webserver.on('/tasmoclaw/api/clear', / -> self.history=[]; self.store.save_history(self.history); self.api_json({'ok':true}), webserver.HTTP_POST)
    webserver.on('/tasmoclaw/api/pending', / -> self.api_json({'ok':true,'pending':self.pending}))
    webserver.on('/tasmoclaw/api/approve', / -> self.api_approve(), webserver.HTTP_POST)
    webserver.on('/tasmoclaw/api/reject', / -> self.pending=nil; self.store.save_pending(nil); self.api_json({'ok':true}), webserver.HTTP_POST)
  end
  def page_config()
    webserver.content_start('TasmoClaw Config'); webserver.content_send_style()
    webserver.content_send('<h2>TasmoClaw Config</h2><p><a href="/tasmoclaw">Back</a></p><p>Use API endpoint, model, and key.</p>')
    webserver.content_send('<pre>'+webserver.html_escape(tasmoclaw_util.json_encode(self.masked_cfg()))+'</pre>')
    webserver.content_stop()
  end
  def masked_cfg() var c=copy(self.cfg); c['api_key']='********'; return c end
  def status_obj() return {'ok':true,'model':self.cfg['model'],'api_url':self.cfg['api_url'],'heap':tasmota.memory(),'wifi':tasmota.wifi(),'pending':self.pending!=nil} end
  def api_json(o) webserver.content_response(tasmoclaw_util.json_encode(o),'application/json') end
  def api_config()
    if webserver.has_arg('plain')
      var in=json.load(webserver.arg('plain')); var old=self.cfg['api_key']
      for k:v in in self.cfg[k]=v end
      if self.cfg['api_key']=='' || self.cfg['api_key']=='********' self.cfg['api_key']=old end
      self.store.save_config(self.cfg)
      self.api_json({'ok':true,'config':self.masked_cfg()})
    else
      self.api_json({'ok':true,'config':self.masked_cfg()})
    end
  end
  def api_chat()
    var req=json.load(webserver.arg('plain')); var user=req['message']
    self.history.push({'role':'user','content':user})
    var msgs=[{'role':'system','content':tasmoclaw_prompt.build(self.tools.tool_lines(),self.cfg['system_extra'])}]
    for m:self.history msgs.push(m) end
    var loops=self.cfg['max_tool_iterations']
    for _i:range(0,loops)
      var r=self.llm.call_chat(self.cfg,msgs)
      if !r['ok'] self.api_json(r); return end
      var c=r['content']
      var tc=self.parse_tool_block(c)
      if tc==nil
        self.history.push({'role':'assistant','content':c}); self.trim_history(); self.store.save_history(self.history); self.api_json({'ok':true,'content':c}); return
      end
      if self.tools.requires_approval(tc['tool'])
        self.pending={'id':str(tasmota.rtc()['Local']),'tool':tc['tool'],'args':tc['args'],'reason':tc['reason'],'created':str(tasmota.rtc()['Local'])}
        self.store.save_pending(self.pending)
        self.api_json({'ok':true,'approval_required':true,'pending':self.pending,'content':'Approval required for '+tc['tool']}); return
      end
      var tr=self.tools.run(tc['tool'],tc['args'])
      msgs.push({'role':'assistant','content':c})
      msgs.push({'role':'tool','content':'Tool '+tc['tool']+' result: '+tasmoclaw_util.json_encode(tr)})
    end
    self.api_json({'ok':false,'error':'max_tool_iterations reached'})
  end
  def parse_tool_block(c)
    var a='<<<TASMOCLAW_TOOL>>>'; var b='<<<END_TASMOCLAW_TOOL>>>'
    var i=string.find(c,a); var j=string.find(c,b)
    if i<0 || j<0 || j<=i return nil end
    var s=string.slice(c,i+size(a),j)
    try return json.load(s) except .. as e,m return nil end
  end
  def api_approve()
    if self.pending==nil self.api_json({'ok':false,'error':'no pending action'}); return end
    var p=self.pending; self.pending=nil; self.store.save_pending(nil)
    var r=self.tools.run(p['tool'],p['args'])
    self.history.push({'role':'tool','content':tasmoclaw_util.json_encode(r)}); self.trim_history(); self.store.save_history(self.history)
    self.api_json({'ok':true,'result':r})
  end
  def trim_history()
    var lim=self.cfg['history_limit']*2
    while size(self.history)>lim self.history.remove(0) end
  end
end

def start()
  if _driver == nil
    _driver = TasmoClawDriver()
    tasmota.add_driver(_driver)
    print('TasmoClaw started')
  end
  return _driver
end
