import webserver
import json
import string
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
    self.store = tasmoclaw_store.create()
    self.store.ensure_workspace()
    self.tools = tasmoclaw_tools.create(self.store)
    self.llm = tasmoclaw_llm.create()
    self.ui = tasmoclaw_ui.create()
    self.cfg = self.normalize_config(self.store.load_config()); self.history = self.store.load_history(); self.pending=self.store.load_pending()
    self.ensure_cmds()
  end
  def ensure_cmds()
    tasmota.add_cmd('TasmoClaw', /cmd,idx,payload -> self.cmd_status())
    tasmota.add_cmd('TasmoClawReset', /cmd,idx,payload -> self.cmd_reset())
    tasmota.add_cmd('TasmoClawTest', /cmd,idx,payload -> self.cmd_test())
  end
  def cmd_status()
    tasmota.resp_cmnd(tasmoclaw_util.json_encode(self.status_obj()))
  end
  def cmd_reset()
    self.history=[]
    self.pending=nil
    self.store.save_history(self.history)
    self.store.save_pending(nil)
    tasmota.resp_cmnd('OK')
  end
  def cmd_test()
    tasmota.resp_cmnd('{"ok":true}')
  end
  def web_add_console_button() webserver.content_button('TasmoClaw','/tasmoclaw') end
  def web_add_management_button() webserver.content_button('TasmoClaw','/tasmoclaw') end
  def web_add_handler()
    webserver.on('/tasmoclaw', / -> self.ui.chat_page(), webserver.HTTP_GET)
    webserver.on('/tasmoclaw/config', / -> self.page_config(), webserver.HTTP_GET)
    webserver.on('/tasmoclaw/api/chat', / -> self.api_chat(), webserver.HTTP_POST)
    webserver.on('/tasmoclaw/api/config', / -> self.api_config_get(), webserver.HTTP_GET)
    webserver.on('/tasmoclaw/api/config', / -> self.api_config_post(), webserver.HTTP_POST)
    webserver.on('/tasmoclaw/api/status', / -> self.api_json(self.status_obj()))
    webserver.on('/tasmoclaw/api/tools', / -> self.api_json({'ok':true,'tools':self.tools.registry()}))
    webserver.on('/tasmoclaw/api/history', / -> self.api_json({'ok':true,'history':self.history}))
    webserver.on('/tasmoclaw/api/clear', / -> self.api_clear(), webserver.HTTP_POST)
    webserver.on('/tasmoclaw/api/pending', / -> self.api_json({'ok':true,'pending':self.pending}))
    webserver.on('/tasmoclaw/api/approve', / -> self.api_approve(), webserver.HTTP_POST)
    webserver.on('/tasmoclaw/api/reject', / -> self.api_reject(), webserver.HTTP_POST)
    webserver.on('/tasmoclaw/api/test', / -> self.api_test(), webserver.HTTP_POST)
  end
  def page_config()
    webserver.content_start('TasmoClaw Config'); webserver.content_send_style()
    webserver.content_send('<style>.tcfg{text-align:left}.tcfg label{display:block;font-weight:bold;margin-top:8px}.tcfg input,.tcfg select,.tcfg textarea{width:100%;box-sizing:border-box}.tcfg textarea{height:100px}.tcfg .row{margin:8px 0}.tcfg .msg{min-height:22px;color:#2a2}</style>')
    webserver.content_send('<h2>TasmoClaw Config</h2><div class="tcfg"><label>API URL</label><input id="api_url"><label>Model</label><select id="model"><option>deepseek-v4-flash</option><option>deepseek-v4-pro</option></select><label>API Key</label><input id="api_key" type="password"><label>Temperature</label><input id="temperature" type="number" step="0.1" min="0" max="2"><label>Max tokens</label><input id="max_tokens" type="number" min="1"><label>Thinking</label><select id="thinking"><option>disabled</option><option>enabled</option></select><label>Reasoning effort</label><select id="reasoning_effort"><option>high</option><option>max</option></select><label>Max tool iterations</label><input id="max_tool_iterations" type="number" min="1"><label>History limit</label><input id="history_limit" type="number" min="1"><label>System extra</label><textarea id="system_extra"></textarea><p><button id="save">Save</button> <button id="test">Test API</button> <a href="/tasmoclaw"><button>Back to TasmoClaw</button></a></p><div id="msg" class="msg"></div></div>')
    webserver.content_send('<script>const ids=["api_url","model","api_key","temperature","max_tokens","thinking","reasoning_effort","max_tool_iterations","history_limit","system_extra"];const el=id=>document.getElementById(id);const note=t=>el("msg").textContent=t;function getCfg(){fetch("/tasmoclaw/api/config").then(r=>r.json()).then(x=>{const c=x.config||{};ids.forEach(id=>{if(c[id]!=null)el(id).value=c[id];});}).catch(e=>note(String(e)));}function body(){let c={};ids.forEach(id=>c[id]=el(id).value);c.temperature=parseFloat(c.temperature);c.max_tokens=parseInt(c.max_tokens);c.max_tool_iterations=parseInt(c.max_tool_iterations);c.history_limit=parseInt(c.history_limit);return c;}el("save").onclick=()=>{note("Saving...");fetch("/tasmoclaw/api/config",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body())}).then(r=>r.json()).then(x=>{note(x.ok?"Saved":(x.error||"Save failed"));if(x.config){ids.forEach(id=>{if(x.config[id]!=null)el(id).value=x.config[id];});}}).catch(e=>note(String(e)));};el("test").onclick=()=>{note("Testing...");fetch("/tasmoclaw/api/test",{method:"POST"}).then(r=>r.json()).then(x=>note(x.ok?x.content:(x.error||"Test failed"))).catch(e=>note(String(e)));};getCfg();</script>')
    webserver.content_stop()
  end
  def masked_cfg()
    var c={}
    for k:self.cfg.keys() c[k]=self.cfg[k] end
    c['api_key']='********'
    return c
  end
  def status_obj() return {'ok':true,'model':self.cfg['model'],'api_url':self.cfg['api_url'],'heap':tasmota.memory(),'wifi':tasmota.wifi(),'pending':self.pending!=nil,'workspace_fallback':self.store.workspace_fallback} end
  def normalize_config(cfg)
    var defaults = self.store.default_config()
    if cfg == nil cfg = {} end
    for k:defaults.keys()
      if cfg.find(k) == nil cfg[k] = defaults[k] end
    end
    if cfg['api_url'] == nil cfg['api_url'] = defaults['api_url'] end
    if cfg['model'] != 'deepseek-v4-pro' && cfg['model'] != 'deepseek-v4-flash' cfg['model'] = defaults['model'] end
    if cfg['temperature'] == nil cfg['temperature'] = defaults['temperature'] end
    if cfg['max_tokens'] == nil cfg['max_tokens'] = defaults['max_tokens'] end
    if cfg['thinking'] != 'enabled' cfg['thinking'] = 'disabled' end
    if cfg['reasoning_effort'] != 'max' cfg['reasoning_effort'] = 'high' end
    if cfg['max_tool_iterations'] == nil || cfg['max_tool_iterations'] < 1 cfg['max_tool_iterations'] = defaults['max_tool_iterations'] end
    if cfg['history_limit'] == nil || cfg['history_limit'] < 1 cfg['history_limit'] = defaults['history_limit'] end
    if cfg['system_extra'] == nil cfg['system_extra'] = '' end
    return cfg
  end
  def api_json(o)
    webserver.content_open(200, 'application/json')
    webserver.content_send(tasmoclaw_util.json_encode(o))
    webserver.content_close()
  end
  def api_config_get()
    self.api_json({'ok':true,'config':self.masked_cfg()})
  end
  def api_config_post()
    try
      if !webserver.has_arg('plain') self.api_json({'ok':false,'error':'missing JSON body'}); return end
      var incoming=json.load(webserver.arg('plain')); var old=self.cfg['api_key']
      for k:incoming.keys() self.cfg[k]=incoming[k] end
      if self.cfg.find('api_key') == nil || self.cfg['api_key']=='' || self.cfg['api_key']=='********' self.cfg['api_key']=old end
      self.cfg = self.normalize_config(self.cfg)
      var r = self.store.save_config(self.cfg)
      if r['ok'] self.api_json({'ok':true,'config':self.masked_cfg(),'storage':r}) else self.api_json(r) end
    except .. as e,m
      self.api_json({'ok':false,'error':'config save failed: '+str(m)})
    end
  end
  def api_chat()
    var req=nil
    try req=json.load(webserver.arg('plain')) except .. as e,m self.api_json({'ok':false,'error':'invalid JSON body: '+str(m)}); return end
    var user=req['message']
    if user == nil || user == '' self.api_json({'ok':false,'error':'missing message'}); return end
    self.history.push({'role':'user','content':user})
    var msgs=self.base_messages()
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
        self.pending={'id':str(tasmota.rtc()['Local']),'tool':tc['tool'],'args':tc['args'],'reason':tc['reason'],'created':str(tasmota.rtc()['Local']),'assistant':c}
        self.store.save_pending(self.pending)
        self.api_json({'ok':true,'approval_required':true,'pending':self.pending,'content':'Approval required for '+tc['tool']}); return
      end
      var tr=self.tools.run(tc['tool'],tc['args'])
      msgs.push({'role':'assistant','content':c})
      msgs.push({'role':'user','content':'TasmoClaw tool result for '+tc['tool']+':\n'+tasmoclaw_util.json_encode(tr)+'\nContinue and give the user the final answer.'})
    end
    self.api_json({'ok':false,'error':'max_tool_iterations reached'})
  end
  def api_clear()
    self.history=[]
    self.store.save_history(self.history)
    self.api_json({'ok':true})
  end
  def api_reject()
    self.pending=nil
    self.store.save_pending(nil)
    self.api_json({'ok':true})
  end
  def base_messages()
    var msgs=[{'role':'system','content':tasmoclaw_prompt.build(self.tools.tool_lines(),self.cfg['system_extra'])}]
    for m:self.history
      var role=m.find('role')
      if role == 'user' || role == 'assistant' || role == 'system'
        msgs.push({'role':role,'content':m['content']})
      end
    end
    return msgs
  end
  def parse_tool_block(c)
    var a='<<<TASMOCLAW_TOOL>>>'; var b='<<<END_TASMOCLAW_TOOL>>>'
    var i=string.find(c,a); var j=string.find(c,b)
    if i<0 || j<0 || j<=i return nil end
    var s=c[i+size(a)..j-1]
    try return json.load(s) except .. as e,m return nil end
  end
  def api_approve()
    if self.pending==nil self.api_json({'ok':false,'error':'no pending action'}); return end
    var p=self.pending; self.pending=nil; self.store.save_pending(nil)
    var r=self.tools.run(p['tool'],p['args'])
    self.history.push({'role':'assistant','content':'Approved TasmoClaw tool '+p['tool']+' result:\n'+tasmoclaw_util.json_encode(r)}); self.trim_history(); self.store.save_history(self.history)
    self.api_json({'ok':true,'result':r})
  end
  def api_test()
    var msgs=[{'role':'system','content':'You are TasmoClaw. Reply exactly as requested.'},{'role':'user','content':'Reply with exactly: TasmoClaw online.'}]
    var r=self.llm.call_chat(self.cfg,msgs)
    if r['ok'] self.api_json({'ok':true,'content':r['content']}) else self.api_json({'ok':false,'error':r['error'],'body':r.find('body')}) end
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

start()

var tasmoclaw = module("tasmoclaw")
tasmoclaw.start = start
return tasmoclaw
