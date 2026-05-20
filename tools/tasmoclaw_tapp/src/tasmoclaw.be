import webserver
import json
import string
import tasmoclaw_store
import tasmoclaw_tools
import tasmoclaw_llm
import tasmoclaw_ui
import tasmoclaw_prompt
import tasmoclaw_util
import tasmoclaw_commands

var _driver = nil

class TasmoClawDriver : Driver
  var store, tools, llm, ui, cfg, history, pending

  def init()
    self.store = tasmoclaw_store.create()
    self.store.ensure_workspace()

    self.tools = tasmoclaw_tools.create(self.store)
    self.llm = tasmoclaw_llm.create()
    self.ui = tasmoclaw_ui.create()

    self.cfg = self.normalize_config(self.store.load_config())
    self.history = self.store.load_history()
    self.pending = self.store.load_pending()

    self.ensure_cmds()
  end

  def ensure_cmds()
    tasmota.add_cmd('TasmoClaw', /cmd,idx,payload -> self.cmd_status())
    tasmota.add_cmd('TasmoClawReset', /cmd,idx,payload -> self.cmd_reset())
    tasmota.add_cmd('TasmoClawTest', /cmd,idx,payload -> self.cmd_test())
    tasmota.add_cmd('TasmoClawHttpsTest', /cmd,idx,payload -> self.cmd_https_test())
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

  def cmd_https_test()
    tasmota.resp_cmnd(tasmoclaw_util.json_encode(self.https_test_obj()))
  end

  def https_test_obj()
    var out = {
      'ok':true,
      'configured_transport':self.cfg.find('https_transport') == nil ? 'webclient' : self.cfg['https_transport']
    }

    out['bearssl_deepseek'] = self.llm.probe_webclient('https://api.deepseek.com/chat/completions')
    out['bearssl_trmnl'] = self.llm.probe_webclient('https://trmnl.com/api/display')
    out['native_deepseek'] = self.llm.probe_native_get('https://api.deepseek.com/chat/completions')
    out['native_trmnl'] = self.llm.probe_native_get('https://trmnl.com/api/display')

    if self.cfg.find('api_key') != nil && self.cfg['api_key'] != ''
      var cfg2 = {}
      for k:self.cfg.keys()
        cfg2[k] = self.cfg[k]
      end
      cfg2['max_tokens'] = 100
      cfg2['thinking'] = 'omit'
      var r = self.llm.call_chat(cfg2, [
        {'role':'system','content':'You are TasmoClaw. Reply exactly as requested.'},
        {'role':'user','content':'Reply with exactly: TasmoClaw online.'}
      ])
      out['deepseek'] = {
        'ok':r['ok'],
        'transport':r.find('transport'),
        'status':r.find('status'),
        'error':r.find('error'),
        'stage':r.find('stage'),
        'esp_err':r.find('esp_err'),
        'webclient_error':r.find('webclient_error'),
        'content':tasmoclaw_util.preview(r.find('content'), 120),
        'body':tasmoclaw_util.preview(r.find('body'), 220)
      }
    end

    return out
  end

  def web_add_console_button()
    # Tasmota's Tools page dispatches both web_add_button and
    # web_add_console_button. Keep this empty so the button is emitted once.
  end

  def web_add_button()
    webserver.content_send('<form action="/tasmoclaw" method="get"><button>TasmoClaw</button></form><p></p>')
  end

  def web_add_management_button()
    # Kept empty for the same reason as web_add_button().
  end

  def stop()
    try
      tasmota.remove_driver(self)
    except .. as e,m
    end
  end

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
    webserver.content_start('TasmoClaw Config')
    webserver.content_send_style()

    webserver.content_send('<style>')
    webserver.content_send('.tcfg{max-width:760px;text-align:left;margin:0 auto;font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,sans-serif}.tcfg label{display:block;font-weight:bold;margin-top:8px}.tcfg input,.tcfg select,.tcfg textarea{width:100%;box-sizing:border-box;padding:8px;border:1px solid #2c3d53;border-radius:8px;background:#0f1722;color:#e8eff8}.tcfg textarea{height:96px}.tcfg .row{margin:8px 0}.tcfg .msg{min-height:22px;color:#2a2;background:#0f1722;padding:6px;border-radius:8px;border:1px solid #2c3d53}.tcfg .cfg-nav{margin-bottom:10px;display:flex;gap:8px;flex-wrap:wrap}.tcfg .cfg-nav button{min-height:38px;padding:0 10px}')
    webserver.content_send('</style>')

    webserver.content_send('<div class="tcfg">')
    webserver.content_send('<p class="cfg-nav"><a href="/mn"><button>Tools menu</button></a> <a href="/tasmoclaw"><button>Back to TasmoClaw</button></a></p>')
    webserver.content_send('<h2>TasmoClaw Config</h2>')
    webserver.content_send('<label>API URL</label><input id="api_url">')
    webserver.content_send('<label>Model</label><select id="model"><option>deepseek-v4-flash</option><option>deepseek-v4-pro</option></select>')
    webserver.content_send('<label>HTTPS transport</label><select id="https_transport"><option value="webclient">webclient / BearSSL</option><option value="auto">auto: BearSSL then native fallback</option><option value="native">native ESP-IDF bridge</option></select>')
    webserver.content_send('<label>API Key</label><input id="api_key" type="password">')
    webserver.content_send('<label>Temperature</label><input id="temperature" type="number" step="0.1" min="0" max="2">')
    webserver.content_send('<label>Max tokens</label><input id="max_tokens" type="number" min="1">')
    webserver.content_send('<label>Thinking</label><select id="thinking"><option>omit</option><option>disabled</option><option>enabled</option></select>')
    webserver.content_send('<label>Reasoning effort</label><select id="reasoning_effort"><option>high</option><option>max</option></select>')
    webserver.content_send('<label>Max tool iterations</label><input id="max_tool_iterations" type="number" min="1">')
    webserver.content_send('<label>History limit</label><input id="history_limit" type="number" min="1">')
    webserver.content_send('<p><label style="display:flex;gap:8px;align-items:center"><input id="auto_approve_tools" type="checkbox" style="width:auto"> Disable permission prompts</label></p>')
    webserver.content_send('<label>System extra</label><textarea id="system_extra"></textarea>')
    webserver.content_send('<p><button id="save">Save</button> <button id="test">Test API</button></p>')
    webserver.content_send('<div id="msg" class="msg"></div>')
    webserver.content_send('</div>')

    webserver.content_send('<script>const ids=["api_url","model","https_transport","api_key","temperature","max_tokens","thinking","reasoning_effort","max_tool_iterations","history_limit","system_extra"];const el=id=>document.getElementById(id);const note=t=>el("msg").textContent=t;function setCfg(c){ids.forEach(id=>{if(c[id]!=null)el(id).value=c[id];});el("auto_approve_tools").checked=!!c.auto_approve_tools;}function getCfg(){fetch("/tasmoclaw/api/config").then(r=>r.json()).then(x=>setCfg(x.config||{})).catch(e=>note(String(e)));}function body(){let c={};ids.forEach(id=>c[id]=el(id).value);c.temperature=parseFloat(c.temperature);c.max_tokens=parseInt(c.max_tokens);c.max_tool_iterations=parseInt(c.max_tool_iterations);c.history_limit=parseInt(c.history_limit);c.auto_approve_tools=el("auto_approve_tools").checked;return c;}function testText(x){if(x.ok)return (x.content||"OK")+" via "+(x.transport||"?")+" HTTP "+(x.status||"?");let p=[x.error||"Test failed"];if(x.transport)p.push("transport "+x.transport);if(x.status!=null)p.push("status "+x.status);if(x.stage)p.push("stage "+x.stage);if(x.hint)p.push(x.hint);if(x.fallback_hint)p.push(x.fallback_hint);if(x.webclient_error)p.push("webclient: "+x.webclient_error);return p.join(" | ");}el("save").onclick=()=>{note("Saving...");fetch("/tasmoclaw/api/config",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body())}).then(r=>r.json()).then(x=>{note(x.ok?"Saved":(x.error||"Save failed"));if(x.config)setCfg(x.config);}).catch(e=>note(String(e)));};el("test").onclick=()=>{note("Testing...");fetch("/tasmoclaw/api/test",{method:"POST"}).then(r=>r.json()).then(x=>note(testText(x))).catch(e=>note(String(e)));};getCfg();</script>')

    webserver.content_stop()
  end

  def masked_cfg()
    var c={}

    for k:self.cfg.keys()
      c[k]=self.cfg[k]
    end

    c['api_key']='********'

    return c
  end

  def status_obj()
    var out = {
      'ok':true,
      'model':self.cfg['model'],
      'api_url':self.cfg['api_url'],
      'https_transport':self.cfg.find('https_transport') == nil ? 'webclient' : self.cfg['https_transport'],
      'heap':tasmota.memory(),
      'wifi':tasmota.wifi(),
      'pending':self.pending!=nil,
      'workspace_fallback':self.store.workspace_fallback
    }

    try
      out['ufs'] = tasmota.cmd('Ufs', true)
      out['ufstype'] = tasmota.cmd('UfsType', true)
    except .. as e,m
      out['ufs_error'] = str(m)
    end

    return out
  end

  def normalize_config(cfg)
    var defaults = self.store.default_config()

    if cfg == nil
      cfg = {}
    end

    for k:defaults.keys()
      if cfg.find(k) == nil
        cfg[k] = defaults[k]
      end
    end

    if cfg['api_url'] == nil
      cfg['api_url'] = defaults['api_url']
    end

    if cfg['model'] != 'deepseek-v4-pro' && cfg['model'] != 'deepseek-v4-flash'
      cfg['model'] = defaults['model']
    end

    if cfg['https_transport'] != 'native' && cfg['https_transport'] != 'auto'
      cfg['https_transport'] = 'webclient'
    end

    if cfg['temperature'] == nil
      cfg['temperature'] = defaults['temperature']
    end

    if cfg['max_tokens'] == nil
      cfg['max_tokens'] = defaults['max_tokens']
    end

    if cfg['thinking'] != 'enabled' && cfg['thinking'] != 'disabled'
      cfg['thinking'] = 'omit'
    end

    if cfg['reasoning_effort'] != 'max'
      cfg['reasoning_effort'] = 'high'
    end

    if cfg['max_tool_iterations'] == nil || cfg['max_tool_iterations'] < 1
      cfg['max_tool_iterations'] = defaults['max_tool_iterations']
    end

    if cfg['history_limit'] == nil || cfg['history_limit'] < 1
      cfg['history_limit'] = defaults['history_limit']
    end

    if cfg['auto_approve_tools'] != true
      cfg['auto_approve_tools'] = false
    end

    if cfg['system_extra'] == nil
      cfg['system_extra'] = ''
    end

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
      if !webserver.has_arg('plain')
        self.api_json({'ok':false,'error':'missing JSON body'})
        return
      end

      var incoming=json.load(webserver.arg('plain'))
      var old=self.cfg['api_key']

      for k:incoming.keys()
        self.cfg[k]=incoming[k]
      end

      if self.cfg.find('api_key') == nil || self.cfg['api_key']=='' || self.cfg['api_key']=='********'
        self.cfg['api_key']=old
      end

      self.cfg = self.normalize_config(self.cfg)

      var r = self.store.save_config(self.cfg)

      if r['ok']
        self.api_json({'ok':true,'config':self.masked_cfg(),'storage':r})
      else
        self.api_json(r)
      end

    except .. as e,m
      self.api_json({'ok':false,'error':'config save failed: '+str(m)})
    end
  end

  def api_chat()
    var req=nil

    try
      req=json.load(webserver.arg('plain'))
    except .. as e,m
      self.api_json({'ok':false,'error':'invalid JSON body: '+str(m)})
      return
    end

    var user=req['message']

    if user == nil || user == ''
      self.api_json({'ok':false,'error':'missing message'})
      return
    end

    # Let the model choose tools first. The old direct router is kept below as
    # a fallback helper, but hard-coded intents should not preempt normal chat.
    var direct = nil
    if direct != nil
      self.history.push({'role':'user','content':user})

      if self.tools.requires_approval_for(direct['tool'], direct['args']) && self.cfg['auto_approve_tools'] != true
        var direct_now = tasmota.rtc().find('local')
        if direct_now == nil
          direct_now = tasmota.rtc().find('utc')
        end

        self.pending={
          'id':str(direct_now),
          'tool':direct['tool'],
          'args':direct['args'],
          'reason':direct.find('reason'),
          'created':str(direct_now),
          'assistant':'Direct TasmoClaw action prepared for approval.'
        }

        self.store.save_pending(self.pending)
        self.trim_history()
        self.store.save_history(self.history)
        self.api_json({
          'ok':true,
          'approval_required':true,
          'pending':self.pending,
          'content':'Approval required for '+direct['tool']
        })
        return
      end

      var direct_result = self.tools.run(direct['tool'], direct['args'])
      var direct_trace = self.format_tool_trace(direct['tool'], direct_result)
      var direct_content = self.format_tool_answer(user, direct['tool'], direct_result)
      if direct['tool'] == 'berry_program_explain' && direct_result.find('ok') == true
        var cfg2 = {}
        for k:self.cfg.keys()
          cfg2[k] = self.cfg[k]
        end
        cfg2['max_tokens'] = 180
        var er = self.llm.call_chat(cfg2, [
          {
            'role':'system',
            'content':'You are TasmoClaw. Explain Berry programs for Tasmota in at most five short sentences. Mention commands registered, files touched, and how to run or test it. Do not include a code block.'
          },
          {
            'role':'user',
            'content':'Explain this Berry source:\n' + str(direct_result.find('result'))
          }
        ])
        var explain_ok = false
        var explain_content = er.find('content')
        if er.find('ok') == true && explain_content != nil && explain_content != '' && size(explain_content) > 40
          var last_char = explain_content[size(explain_content)-1..size(explain_content)-1]
          explain_ok = last_char == '.' || last_char == '!' || last_char == '?'
        end
        if explain_ok
          direct_content = explain_content
        else
          direct_content = 'This Berry program registers a Tasmota command named HelloWorld. When the command runs, it responds with JSON: {"HelloWorld":"ok"}. It also prints "Hello World from TasmoClaw" when the file is loaded. The source file is ' + str(direct_result.find('path')) + '. Run it with the berry_program_run tool or Br load("' + str(direct_result.find('path')) + '"), then test it with the HelloWorld command.'
          if er.find('error') != nil
            direct_content += '\n\nModel explanation fallback reason: ' + str(er.find('error'))
          end
        end
      end
      if direct_trace != nil && direct_trace != ''
        self.history.push({'role':'tool','content':direct_trace})
      end
      self.history.push({'role':'assistant','content':direct_content})
      self.trim_history()
      self.store.save_history(self.history)
      self.api_json({'ok':true,'content':direct_content,'tool_trace':direct_trace,'tool_result':direct_result})
      return
    end

    self.history.push({'role':'user','content':user})

    var msgs=self.base_messages()
    var loops=self.cfg['max_tool_iterations']
    if loops < 6
      loops = 6
    end
    var tool_trace = ''
    var last_tool_result = nil
    var last_tool = nil
    var action_tool_seen = false
    var later_action_repair_used = false

    for _i:range(0,loops)
      var r=self.llm.call_chat(self.cfg,msgs)

      if !r['ok']
        self.api_json(r)
        return
      end

      var c=r['content']
      var tc=self.parse_tool_block(c)

      if tc==nil
        if c != nil && string.find(c, '<<<TASMOCLAW_TOOL>>>') != nil && string.find(c, '<<<TASMOCLAW_TOOL>>>') >= 0 && _i < loops - 1
          msgs.push({'role':'assistant','content':c})
          msgs.push({
            'role':'user',
            'content':'Your TasmoClaw tool block was incomplete or invalid JSON. Resend exactly one complete tool block with valid JSON and the closing <<<END_TASMOCLAW_TOOL>>> marker. For audio_rtttl_play, keep the RTTTL short but complete.'
          })
          continue
        end

        if last_tool_result == nil && self.request_needs_tool(user) && _i < loops - 1
          msgs.push({
            'role':'user',
            'content':'You answered without using a tool, but this request depends on current device state or files. Respond with exactly one TasmoClaw tool block using the listed tools. Do not answer from chat history.'
          })
          continue
        end

        if (c == nil || c == '') && last_tool_result == nil && _i < loops - 1
          msgs.push({
            'role':'user',
            'content':'Your previous response was empty. If this request needs current device state, filesystem data, SD-card data, sensors, power state, rules, or Berry files, respond with exactly one TasmoClaw tool block using the listed tools. Otherwise answer normally.'
          })
          continue
        end

        if last_tool_result != nil
          if self.request_has_later_action(user) && !action_tool_seen && !later_action_repair_used && _i < loops - 1
            later_action_repair_used = true
            msgs.push({
              'role':'user',
              'content':'The original user request has a later action step that is not complete yet. Call the next required TasmoClaw tool now. Do not give the final answer yet.'
            })
            continue
          end

          var use_fallback_summary = false
          if c == nil || c == ''
            use_fallback_summary = true
          elif size(c) < 24
            use_fallback_summary = true
          else
            var last_char = c[size(c)-1..size(c)-1]
            if last_char != '.' && last_char != '!' && last_char != '?' && last_char != '\n'
              use_fallback_summary = true
            end
          end

          if use_fallback_summary
            c = self.format_tool_answer(user, last_tool, last_tool_result)
          end
        end

        if c == nil || c == ''
          var fallback = self.direct_tool_for_user(user)
          if fallback != nil
            if self.tools.requires_approval_for(fallback['tool'], fallback['args']) && self.cfg['auto_approve_tools'] != true
              var fallback_now = tasmota.rtc().find('local')
              if fallback_now == nil
                fallback_now = tasmota.rtc().find('utc')
              end

              self.pending={
                'id':str(fallback_now),
                'tool':fallback['tool'],
                'args':fallback['args'],
                'reason':fallback.find('reason'),
                'created':str(fallback_now),
                'assistant':'Fallback TasmoClaw action prepared for approval.'
              }

              self.store.save_pending(self.pending)
              self.trim_history()
              self.store.save_history(self.history)
              self.api_json({
                'ok':true,
                'approval_required':true,
                'pending':self.pending,
                'content':'Approval required for '+fallback['tool']
              })
              return
            end

            var fallback_result = self.tools.run(fallback['tool'], fallback['args'])
            var fallback_trace = self.format_tool_trace(fallback['tool'], fallback_result)
            c = self.format_tool_answer(user, fallback['tool'], fallback_result)
            self.history.push({'role':'tool','content':fallback_trace})
            self.history.push({'role':'assistant','content':c})
            self.trim_history()
            self.store.save_history(self.history)
            self.api_json({'ok':true,'content':c,'tool_trace':fallback_trace,'tool_result':fallback_result,'fallback':'direct_router'})
            return
          end
        end

        self.history.push({'role':'assistant','content':c})
        self.trim_history()
        self.store.save_history(self.history)
        var resp = {'ok':true,'content':c}
        if tool_trace != ''
          resp['tool_trace'] = tool_trace
          resp['tool_result'] = last_tool_result
        end
        self.api_json(resp)
        return
      end

      var repair = self.tool_choice_repair(user, tc)
      if repair != nil && _i < loops - 1
        msgs.push({'role':'assistant','content':c})
        msgs.push({'role':'user','content':repair})
        continue
      end

      if self.tools.requires_approval_for(tc['tool'], tc['args']) && self.cfg['auto_approve_tools'] != true
        var now = tasmota.rtc().find('local')
        if now == nil
          now = tasmota.rtc().find('utc')
        end

        self.pending={
          'id':str(now),
          'tool':tc['tool'],
          'args':tc['args'],
          'reason':tc['reason'],
          'created':str(now),
          'assistant':c,
          'prior_trace':tool_trace
        }

        self.store.save_pending(self.pending)

        var approval_resp = {
          'ok':true,
          'approval_required':true,
          'pending':self.pending,
          'content':'Approval required for '+tc['tool']
        }
        if tool_trace != ''
          approval_resp['tool_trace'] = tool_trace
          approval_resp['tool_result'] = last_tool_result
        end

        self.api_json(approval_resp)

        return
      end

      var tr=self.tools.run(tc['tool'],tc['args'])
      if self.tools.requires_approval_for(tc['tool'], tc['args'])
        action_tool_seen = true
      end
      var trace = self.format_tool_trace(tc['tool'], tr)
      if trace != nil && trace != ''
        if tool_trace != ''
          tool_trace += '\n\n'
        end
        tool_trace += trace
      end
      last_tool_result = tr
      last_tool = tc['tool']

      msgs.push({'role':'assistant','content':c})
      msgs.push({
        'role':'user',
        'content':'Original user request:\n'+user+'\n\nTasmoClaw tool result for '+tc['tool']+':\n'+tasmoclaw_util.json_encode(tr)+'\nIf the original request still has uncompleted steps, call the next required TasmoClaw tool now. If all steps are complete, give the user the final answer. Summarize the relevant fields from the result instead of dumping raw JSON. Include ADC/analog values, sensor readings, power states, filenames, paths, byte counts, commands run, and errors when present.'
      })
    end

    self.api_json({'ok':false,'error':'max_tool_iterations reached'})
  end

  def map_find(obj, key)
    if obj == nil
      return nil
    end
    try
      return obj.find(key)
    except .. as e,m
    end
    return nil
  end

  def analog_summary(sns)
    var analog = self.map_find(sns, 'ANALOG')
    if analog == nil
      return ''
    end

    var out = ''
    try
      for k:analog.keys()
        if out != ''
          out += ', '
        end
        out += str(k) + '=' + str(self.map_find(analog, k))
      end
    except .. as e,m
    end

    return out
  end

  def rules_summary(result)
    if result == nil
      return ''
    end

    var r = result.find('result')
    if r == nil
      return ''
    end

    var out = ''
    for slot:['Rule1','Rule2','Rule3']
      var outer = self.map_find(r, slot)
      var inner = self.map_find(outer, slot)
      if inner != nil
        if out != ''
          out += '\n'
        end
        out += slot + ': ' + str(self.map_find(inner, 'State'))
        var rules = self.map_find(inner, 'Rules')
        if rules != nil && rules != ''
          out += '\n  ' + str(rules)
        else
          out += '\n  <empty>'
        end
      end
    end

    return out
  end

  def format_tool_trace(tool, result)
    var out = 'Tool call: ' + tool
    if result == nil
      return out
    end

    var ok = result.find('ok')
    out += ok == true ? '\nStatus: ok' : '\nStatus: error'
    var err = result.find('error')
    if err != nil
      out += '\nError: ' + str(err)
      return out
    end

    var cmd = result.find('command')
    if cmd != nil
      out += '\nCommand: ' + str(cmd)
      var safety = result.find('safety')
      if safety != nil
        out += '\nSafety: ' + str(safety)
      end
    end

    var r = result.find('result')
    if tool == 'command_build'
      out += '\nBuilt: ' + str(result.find('command'))
      out += '\nSafety: ' + str(result.find('safety'))
      out += '\nReason: ' + str(result.find('reason'))
    elif tool == 'command_catalog_search'
      out += '\nResult: ' + tasmoclaw_util.preview(tasmoclaw_util.json_encode(result), 700)
    elif tool == 'command_run' || tool == 'command_sequence_run' || tool == 'audio_rtttl_play' || tool == 'audio_file_play' || tool == 'audio_say' || tool == 'audio_control' || tool == 'display_control' || tool == 'power_control' || tool == 'rule_control' || tool == 'light_control' || tool == 'mqtt_control' || tool == 'telemetry_control' || tool == 'network_control' || tool == 'system_control' || tool == 'timer_control' || tool == 'filesystem_control'
      out += '\nResult: ' + tasmoclaw_util.preview(tasmoclaw_util.json_encode(r), 700)
    elif tool == 'berry_skill_template'
      out += '\nCommand: ' + str(result.find('command'))
      out += '\nResult: ' + tasmoclaw_util.preview(str(result.find('content')), 700)
    elif tool == 'sensor_read'
      var s8 = self.map_find(r, 'status8')
      var sns = self.map_find(s8, 'StatusSNS')
      var analog = self.analog_summary(sns)
      if analog != ''
        out += '\nAnalog: ' + analog
      end
      var sht = self.map_find(sns, 'SHTC3')
      if sht != nil
        out += '\nSHTC3: ' + str(self.map_find(sht, 'Temperature')) + ' C, ' + str(self.map_find(sht, 'Humidity')) + '% RH'
      end
      var scan = self.map_find(r, 'i2cscan')
      if scan != nil
        out += '\nI2C: ' + str(self.map_find(scan, 'I2CScan'))
      end
    elif tool == 'power_read'
      out += '\nPOWER1: ' + str(self.map_find(r, 'POWER1'))
      out += '\nPOWER2: ' + str(self.map_find(r, 'POWER2'))
    elif tool == 'device_read'
      var s82 = self.map_find(r, 'status8')
      var sns2 = self.map_find(s82, 'StatusSNS')
      var analog2 = self.analog_summary(sns2)
      if analog2 != ''
        out += '\nAnalog: ' + analog2
      end
      var sht2 = self.map_find(sns2, 'SHTC3')
      if sht2 != nil
        out += '\nSHTC3: ' + str(self.map_find(sht2, 'Temperature')) + ' C, ' + str(self.map_find(sht2, 'Humidity')) + '% RH'
      end
      var p = self.map_find(r, 'power')
      if p != nil
        out += '\nPOWER1: ' + str(self.map_find(p, 'POWER1'))
        out += '\nPOWER2: ' + str(self.map_find(p, 'POWER2'))
      end
      out += '\nSD mounted: ' + str(self.map_find(r, 'sd_mounted'))
    elif tool == 'tasmota_cmd_read'
      var rules = self.rules_summary(result)
      if rules != ''
        out += '\n' + rules
      else
        out += '\nResult: ' + tasmoclaw_util.preview(tasmoclaw_util.json_encode(result), 700)
      end
    else
      out += '\nResult: ' + tasmoclaw_util.preview(tasmoclaw_util.json_encode(result), 700)
    end
    return out
  end

  def format_entries(obj)
    if obj == nil
      return ''
    end

    var entries = self.map_find(obj, 'entries')
    if entries != nil
      var out = ''
      for e:entries
        if out != ''
          out += '\n'
        end
        out += '- ' + str(self.map_find(e, 'name')) + ' (' + str(self.map_find(e, 'size')) + ' bytes)'
      end
      return out
    end

    var ufs_items = self.map_find(obj, 'UfsList')
    if ufs_items != nil
      var out2 = ''
      for e2:ufs_items
        if out2 != ''
          out2 += '\n'
        end
        out2 += '- ' + str(e2[0]) + ' (' + str(e2[2]) + ' bytes)'
      end
      return out2
    end

    return ''
  end

  def format_tool_answer(user, tool, result)
    if result == nil || result.find('ok') != true
      var err = result == nil ? 'unknown error' : str(result.find('error'))
      return 'I tried to use ' + tool + ', but it failed: ' + err
    end

    var r = result.find('result')
    if tool == 'sensor_read'
      var s8 = self.map_find(r, 'status8')
      var sns = self.map_find(s8, 'StatusSNS')
      var sht = self.map_find(sns, 'SHTC3')
      var analog = self.analog_summary(sns)
      var answer = ''
      if analog != ''
        answer += 'Analog ADC: ' + analog + '. '
      end
      if sht != nil
        answer += 'Temperature is ' + str(self.map_find(sht, 'Temperature')) + ' C, with humidity at ' + str(self.map_find(sht, 'Humidity')) + '%.'
      end
      if answer != ''
        return answer
      end
      return 'I read the sensors, but I could not find analog or SHTC3 values in the response.'
    elif tool == 'power_read'
      return 'Power state: POWER1 is ' + str(self.map_find(r, 'POWER1')) + ', and POWER2 is ' + str(self.map_find(r, 'POWER2')) + '.'
    elif tool == 'device_read'
      var parts = ''
      var s82 = self.map_find(r, 'status8')
      var sns2 = self.map_find(s82, 'StatusSNS')
      var analog2 = self.analog_summary(sns2)
      if analog2 != ''
        parts += 'Analog ADC: ' + analog2 + '. '
      end
      var sht2 = self.map_find(sns2, 'SHTC3')
      if sht2 != nil
        parts += 'Temperature is ' + str(self.map_find(sht2, 'Temperature')) + ' C and humidity is ' + str(self.map_find(sht2, 'Humidity')) + '%. '
      end
      var p = self.map_find(r, 'power')
      if p != nil
        parts += 'POWER1 is ' + str(self.map_find(p, 'POWER1')) + ' and POWER2 is ' + str(self.map_find(p, 'POWER2')) + '. '
      end
      var sd = self.map_find(r, 'sd_mounted')
      if sd != nil
        parts += 'SD mounted: ' + str(sd) + '.'
      end
      if parts != ''
        return parts
      end
      return 'I read the device status successfully, but there was no compact sensor or power value to summarize.'
    elif tool == 'sd_markdown_read'
      return 'I read ' + str(result.find('path')) + ':\n' + str(result.find('body'))
    elif tool == 'sd_markdown_write'
      return 'I wrote ' + str(result.find('bytes')) + ' bytes to ' + str(result.find('path')) + ' on the SD card.'
    elif tool == 'sd_markdown_list'
      var entries = self.format_entries(result)
      if entries != ''
        return 'SD card contents:\n' + entries
      end
      return 'The SD card is mounted, but I did not find files to list.'
    elif tool == 'berry_program_read'
      return 'I read ' + str(result.find('path')) + ':\n' + str(result.find('result'))
    elif tool == 'berry_program_write'
      return 'I wrote the Berry program to ' + str(result.find('path')) + ' (' + str(result.find('bytes')) + ' bytes).'
    elif tool == 'berry_program_run'
      return 'I loaded and ran the Berry program. Result: ' + str(result.find('result')) + '.'
    elif tool == 'ufs_info'
      var sd_entries = self.format_entries(self.map_find(r, 'sd_list'))
      if sd_entries != ''
        return 'SD card contents:\n' + sd_entries
      end
      return 'Storage is available. SD mounted: ' + str(self.map_find(r, 'sd_mounted')) + '. UFS type: ' + str(self.map_find(r, 'type')) + '.'
    elif tool == 'command_build'
      return 'I built this Tasmota command: ' + str(result.find('command')) + '. Safety: ' + str(result.find('safety')) + '.'
    elif tool == 'command_catalog_search'
      return 'I found matching command families:\n' + tasmoclaw_util.preview(tasmoclaw_util.json_encode(result.find('families')), 700)
    elif tool == 'command_run'
      return 'I ran ' + str(result.find('command')) + '. Result: ' + tasmoclaw_util.preview(tasmoclaw_util.json_encode(r), 500)
    elif tool == 'command_sequence_run'
      var seq = result.find('results')
      var out = 'I ran the command sequence.'
      if seq != nil
        out = 'Command sequence result:'
        for step:seq
          out += '\n- ' + str(step.find('command')) + ': '
          if step.find('ok') == true
            out += 'ok'
          else
            out += 'failed: ' + str(step.find('error'))
          end
        end
      end
      return out
    elif tool == 'audio_rtttl_play'
      return 'I started the RTTTL tune with ' + str(result.find('command')) + '.'
    elif tool == 'audio_file_play'
      return 'I ran audio playback command ' + str(result.find('command')) + '.'
    elif tool == 'audio_say'
      return 'I sent the speech command: ' + str(result.find('command')) + '.'
    elif tool == 'audio_control'
      return 'I ran audio command ' + str(result.find('command')) + '.'
    elif tool == 'display_control'
      return 'I sent the display command: ' + str(result.find('command')) + '.'
    elif tool == 'power_control'
      return 'I ran power command ' + str(result.find('command')) + '. Result: ' + tasmoclaw_util.preview(tasmoclaw_util.json_encode(r), 500)
    elif tool == 'rule_control'
      return 'I ran the rule command. Result: ' + tasmoclaw_util.preview(tasmoclaw_util.json_encode(result), 700)
    elif tool == 'light_control' || tool == 'mqtt_control' || tool == 'telemetry_control' || tool == 'network_control' || tool == 'system_control' || tool == 'timer_control' || tool == 'filesystem_control'
      return 'I ran ' + str(result.find('command')) + '. Result: ' + tasmoclaw_util.preview(tasmoclaw_util.json_encode(r), 500)
    elif tool == 'berry_skill_template'
      return 'I prepared a Berry skill template for command ' + str(result.find('command')) + '.'
    elif tool == 'berry_skill_create'
      var msg = 'I wrote the Berry skill to ' + str(result.find('path')) + '. It registers command ' + str(result.find('command')) + '.'
      var lr = result.find('load')
      if lr != nil
        msg += ' Load result: ' + tasmoclaw_util.preview(tasmoclaw_util.json_encode(lr), 300)
      end
      return msg
    elif tool == 'berry_skill_run'
      return 'I loaded the Berry skill. Result: ' + str(result.find('result')) + '.'
    elif tool == 'berry_skill_explain'
      return 'I read the Berry skill source:\n' + str(result.find('result'))
    elif tool == 'tasmota_cmd_read'
      var rules = self.rules_summary(result)
      if rules != ''
        return 'Current Tasmota rules:\n' + rules
      end
      return 'Read-only command result:\n' + tasmoclaw_util.preview(tasmoclaw_util.json_encode(result), 700)
    end

    return 'Done. I used ' + tool + ' successfully.'
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
    var msgs=[
      {
        'role':'system',
        'content':tasmoclaw_prompt.build(self.tools.tool_lines(),self.cfg['system_extra'])
      }
    ]

    for m:self.history
      var role=m.find('role')
      var content=m.find('content')

      if role == 'user' || role == 'assistant' || role == 'system'
        var tool_marker = nil
        if content != nil
          tool_marker = string.find(content, 'TASMOCLAW_TOOL')
        end

        if content != nil && (tool_marker == nil || tool_marker < 0)
          msgs.push({'role':role,'content':content})
        end
      end
    end

    return msgs
  end

  def filename_from_text(user)
    if user == nil
      return nil
    end

    var lower = string.tolower(user)
    for ext:['.txt','.md','.json','.be','.tapp','.mp3','.wav','.opus','.webm','.aac','.m4a']
      var ei = string.find(lower, ext)
      if ei != nil && ei >= 0
        var start = ei
        while start > 0
          var ch = lower[start-1..start-1]
          if ch == ' ' || ch == '\n' || ch == '\t' || ch == '"' || ch == '\'' || ch == '`' || ch == ',' || ch == ':' || ch == ';' || ch == '/' || ch == '\\'
            break
          end
          start -= 1
        end

        var stop = ei + size(ext) - 1
        while stop + 1 < size(user)
          var ch2 = lower[stop+1..stop+1]
          if ch2 == ' ' || ch2 == '\n' || ch2 == '\t' || ch2 == '"' || ch2 == '\'' || ch2 == '`' || ch2 == ',' || ch2 == ':' || ch2 == ';'
            break
          end
          stop += 1
        end

        var sdp = string.find(lower, 'sd:')
        var flp = string.find(lower, 'flash:')
        if sdp != nil && sdp >= 0 && sdp < ei
          start = sdp
        elif flp != nil && flp >= 0 && flp < ei
          start = flp
        end

        return user[start..stop]
      end
    end

    return nil
  end

  def first_number_from_text(user)
    if user == nil
      return nil
    end

    var digits = '0123456789'
    var start = nil
    var stop = nil
    for i:range(0, size(user))
      var ch = user[i..i]
      var di = string.find(digits, ch)
      if di != nil && di >= 0
        if start == nil
          start = i
        end
        stop = i
      elif start != nil
        break
      end
    end

    if start == nil
      return nil
    end
    return user[start..stop]
  end

  def text_after_marker(user, markers)
    if user == nil
      return ''
    end

    var u = string.tolower(user)
    for marker:markers
      var mi = string.find(u, marker)
      if mi != nil && mi >= 0
        var start = mi + size(marker)
        if start < size(user)
          return user[start..size(user)-1]
        end
      end
    end

    return ''
  end

  def first_token(text)
    if text == nil || text == ''
      return ''
    end

    var stop = size(text) - 1
    for i:range(0, size(text))
      var ch = text[i..i]
      if ch == ' ' || ch == '\n' || ch == '\t' || ch == '"' || ch == '\'' || ch == '`' || ch == ',' || ch == ':' || ch == ';' || ch == '.'
        stop = i - 1
        break
      end
    end

    if stop < 0
      return ''
    end
    return text[0..stop]
  end

  def rtttl_preset_from_text(user)
    if user == nil
      return nil
    end

    var u = string.tolower(user)
    for avoid:['not happy','not happy birthday','another','different']
      var avoid_i = string.find(u, avoid)
      if avoid_i != nil && avoid_i >= 0
        return nil
      end
    end

    if string.find(u, 'happy birthday') != nil && string.find(u, 'happy birthday') >= 0
      return 'happy_birthday'
    elif string.find(u, 'mario') != nil && string.find(u, 'mario') >= 0
      return 'mario'
    elif string.find(u, 'twinkle') != nil && string.find(u, 'twinkle') >= 0
      return 'twinkle'
    elif string.find(u, 'ode') != nil && string.find(u, 'ode') >= 0
      return 'ode'
    elif string.find(u, 'scale') != nil && string.find(u, 'scale') >= 0
      return 'scale'
    elif string.find(u, 'success') != nil && string.find(u, 'success') >= 0
      return 'success'
    elif string.find(u, 'error') != nil && string.find(u, 'error') >= 0
      return 'error'
    elif string.find(u, 'startup') != nil && string.find(u, 'startup') >= 0
      return 'startup'
    end
    return nil
  end

  def request_needs_tool(user)
    if user == nil
      return false
    end

    var u = string.tolower(user)
    for kw:[
      'current','now','status','sensor','temperature','humidity','adc','analog','i2c',
      'power','relay','rule','sd','card','file','filesystem','ufs','berry','tasmota',
      'command','gpio','wifi','heap','memory','read','open','show','list','write',
      'create','save','run','toggle','switch','turn on','turn off','play','audio',
      'sound','song','rtttl','music','say','speak','volume','gain','beep','stop',
      'pause','resume','display','screen','message','record','light','dimmer',
      'brightness','color','colour','mqtt','publish','topic','teleperiod','weblog',
      'seriallog','event','backlog','skill','tool','timer','timers','pulsetime',
      'ruletimer','filesystem_control','network','hostname','ntp','timezone'
    ]
      var ki = string.find(u, kw)
      if ki != nil && ki >= 0
        return true
      end
    end

    if self.filename_from_text(user) != nil
      return true
    end

    return false
  end

  def request_has_later_action(user)
    if user == nil
      return false
    end

    var u = string.tolower(user)
    var has_sequence = false
    for sk:[' then ',' and then ',' after ',' next ']
      var si = string.find(u, sk)
      if si != nil && si >= 0
        has_sequence = true
      end
    end

    if !has_sequence
      return false
    end

    for ak:['toggle','switch','turn on','turn off','set ','write','create','save','run','apply','delete','remove','clear','enable','disable','play','say','speak','display','stop','pause','resume']
      var ai = string.find(u, ak)
      if ai != nil && ai >= 0
        return true
      end
    end

    return false
  end

  def is_rtttl_text(tune)
    if tune == nil
      return false
    end
    var s = str(tune)
    var colon = string.find(s, ':')
    var comma = string.find(s, ',')
    var defaults = string.find(s, 'd=')
    return colon != nil && colon >= 0 && comma != nil && comma >= 0 && defaults != nil && defaults >= 0
  end

  def known_rtttl_name(name)
    if name == nil
      return false
    end
    var key = string.tolower(str(name))
    key = string.replace(key, ' ', '_')
    key = string.replace(key, '-', '_')
    for item:['happy_birthday','happy','success','ok','error','startup','mario','scale','twinkle','ode']
      if key == item
        return true
      end
    end
    return false
  end

  def tool_choice_repair(user, tc)
    if user == nil || tc == nil
      return nil
    end

    var u = string.tolower(user)
    var chosen_tool = tc.find('tool')
    var chosen_args = tc.find('args')

    if chosen_tool == 'audio_rtttl_play'
      var has_valid_rtttl = false
      var has_known_preset = false
      var supplied_title = ''
      if chosen_args != nil
        var supplied_rtttl = chosen_args.find('rtttl')
        if supplied_rtttl == nil
          supplied_rtttl = chosen_args.find('tune')
        end
        if supplied_rtttl == nil
          supplied_rtttl = chosen_args.find('body')
        end
        if supplied_rtttl != nil
          supplied_title = str(supplied_rtttl)
          has_valid_rtttl = self.is_rtttl_text(supplied_rtttl)
          if !has_valid_rtttl && self.known_rtttl_name(supplied_rtttl)
            has_known_preset = true
          end
        end

        var supplied_preset = chosen_args.find('preset')
        if supplied_preset == nil
          supplied_preset = chosen_args.find('name')
        end
        if supplied_preset == nil
          supplied_preset = chosen_args.find('song')
        end
        if supplied_preset != nil && self.known_rtttl_name(supplied_preset)
          has_known_preset = true
        end
      end

      if !has_valid_rtttl && !has_known_preset
        return 'The audio_rtttl_play tool needs a complete RTTTL string in args.rtttl, not only a song title like "' + supplied_title + '". Compose a short valid RTTTL melody now and respond with exactly one complete TasmoClaw tool block, for example {"tool":"audio_rtttl_play","args":{"rtttl":"NewTune:d=8,o=5,b=140:c,e,g,c6,g,e,c,p"},"reason":"Play a generated RTTTL melody."}.'
      end
    end

    if chosen_tool == 'berry_skill_template'
      var wants_write_skill = false
      for sw:['create','write','save','install','load','run','make','register']
        var swi = string.find(u, sw)
        if swi != nil && swi >= 0
          wants_write_skill = true
        end
      end
      if wants_write_skill
        return 'The user asked to create or load a reusable Berry skill, not just preview a template. Call berry_skill_create now. Use the explicit skill name and command name from the original request, include content/code if the user supplied it, and set autoload:true if the user asked to load it.'
      end
    end

    if chosen_tool == 'berry_skill_create'
      var args2 = tc.find('args')
      var skill_default = false
      if args2 == nil
        skill_default = true
      else
        var sn = args2.find('name')
        var sc = args2.find('command')
        if sn == nil || sn == '' || string.tolower(str(sn)) == 'tasmo_skill'
          skill_default = true
        end
        if sc == nil || sc == '' || string.tolower(str(sc)) == 'tasmo_skill'
          skill_default = true
        end
      end
      if skill_default && (string.find(u, 'called') != nil || string.find(u, 'named') != nil || string.find(u, 'command') != nil)
        return 'The Berry skill tool call used the default name. Resend berry_skill_create with the explicit skill name and command name from the original request. If the user asked to load it, include autoload:true.'
      end

      if args2 != nil
        var asked_load = false
        for lw:['load','run','execute']
          var lwi = string.find(u, lw)
          if lwi != nil && lwi >= 0
            asked_load = true
          end
        end
        if asked_load && args2.find('autoload') != true
          return 'The user asked to create and load the Berry skill. Resend berry_skill_create with autoload:true.'
        end

        var provided_content = args2.find('content')
        if provided_content == nil
          provided_content = args2.find('code')
        end
        if provided_content != nil && provided_content != ''
          var pcs = string.tolower(str(provided_content))
          if string.find(pcs, 'def (') != nil || string.find(pcs, ' .. ') != nil
            return 'The generated Berry code looks invalid for Tasmota Berry. Unless the user supplied exact source code, omit content/code and let berry_skill_create generate the safe default command template for the requested name and command.'
          end
        end
      end
    end

    var asks_rule = false
    var has_rule = string.find(u, 'rule')
    var has_rules = string.find(u, 'rules')
    if (has_rule != nil && has_rule >= 0) || (has_rules != nil && has_rules >= 0)
      asks_rule = true
    end

    if asks_rule
      var is_change = false
      for rw:['add','set','create','make','apply','enable','disable','delete','remove','clear','erase','run']
        var rwi = string.find(u, rw)
        if rwi != nil && rwi >= 0
          is_change = true
        end
      end

      if !is_change
        var tool = tc.find('tool')
        var args = tc.find('args')
        var cmd = nil
        if args != nil
          try
            cmd = args.find('command')
            if cmd == nil
              cmd = args.find('cmd')
            end
          except .. as e,m
          end
        end

        var cmd_l = cmd == nil ? '' : string.tolower(str(cmd))
        var ok_rule_tool = false
        if tool == 'tasmota_cmd_read' && cmd_l == 'rules'
          ok_rule_tool = true
        elif tool == 'rule_control'
          var action = args == nil ? nil : args.find('action')
          if action == nil || action == '' || string.tolower(str(action)) == 'read'
            ok_rule_tool = true
          end
        end

        if !ok_rule_tool
          return 'The user asked to read Tasmota rules. Use rule_control with args {"action":"read","rule":"Rules"} or tasmota_cmd_read with args {"command":"Rules"}. Do not use device_read for rules. Respond with exactly one complete TasmoClaw tool block.'
        end
      end
    end

    return nil
  end

  def direct_tool_for_user(user)
    if user == nil
      return nil
    end

    var u=string.tolower(user)

    var audio_word = false
    for aw0:['audio','sound','music','song','rtttl','i2s','speaker','say','speak','volume','gain','beep']
      var awi0 = string.find(u, aw0)
      if awi0 != nil && awi0 >= 0
        audio_word = true
      end
    end

    if audio_word
      var wants_stop = string.find(u, 'stop')
      if wants_stop != nil && wants_stop >= 0
        return {'tool':'audio_control','args':{'action':'stop'},'reason':'Stop I2S audio playback.'}
      end

      var wants_pause = string.find(u, 'pause')
      if wants_pause != nil && wants_pause >= 0
        return {'tool':'audio_control','args':{'action':'pause'},'reason':'Pause I2S audio playback.'}
      end

      var wants_resume = string.find(u, 'resume')
      if wants_resume != nil && wants_resume >= 0
        return {'tool':'audio_control','args':{'action':'resume'},'reason':'Resume I2S audio playback.'}
      end

      var wants_gain = false
      for gw:['volume','gain']
        var gi = string.find(u, gw)
        if gi != nil && gi >= 0
          wants_gain = true
        end
      end
      if wants_gain
        var nv = self.first_number_from_text(user)
        if nv == nil
          nv = '25'
        end
        return {'tool':'audio_control','args':{'action':'gain','value':nv},'reason':'Set I2S audio gain/volume.'}
      end

      var wants_beep = string.find(u, 'beep')
      if wants_beep != nil && wants_beep >= 0
        return {'tool':'audio_control','args':{'action':'beep'},'reason':'Play a short I2S beep.'}
      end

      var say_pos = string.find(u, 'say ')
      var speak_pos = string.find(u, 'speak ')
      if say_pos != nil && say_pos >= 0
        return {'tool':'audio_say','args':{'text':user[say_pos + size('say ')..size(user)-1]},'reason':'Speak text with I2SSay.'}
      elif speak_pos != nil && speak_pos >= 0
        return {'tool':'audio_say','args':{'text':user[speak_pos + size('speak ')..size(user)-1]},'reason':'Speak text with I2SSay.'}
      end

      var named_audio = self.filename_from_text(user)
      if named_audio != nil
        var loop_audio = string.find(u, 'loop')
        return {
          'tool':'audio_file_play',
          'args':{'path':named_audio,'action':(loop_audio != nil && loop_audio >= 0) ? 'loop' : 'play'},
          'reason':'Play the requested audio file.'
        }
      end

      var wants_song = false
      for sw0:['play','song','rtttl','tune','happy']
        var swi0 = string.find(u, sw0)
        if swi0 != nil && swi0 >= 0
          wants_song = true
        end
      end
      if wants_song
        var preset = self.rtttl_preset_from_text(user)
        if preset == nil
          return nil
        end
        return {
          'tool':'audio_rtttl_play',
          'args':{'preset':preset},
          'reason':'Play an RTTTL tune through I2S audio.'
        }
      end
    end

    var display_word = false
    for dw0:['display','screen','show on screen','show text','message']
      var dwi0 = string.find(u, dw0)
      if dwi0 != nil && dwi0 >= 0
        display_word = true
      end
    end
    if display_word
      var dm = self.text_after_marker(user, ['display ', 'screen ', 'show ', 'message '])
      if dm == ''
        dm = user
      end
      return {'tool':'display_control','args':{'message':dm},'reason':'Show text on the device display.'}
    end

    var wants_toggle = false
    for tw:['toggle','switch','turn on','turn off']
      var ti = string.find(u, tw)
      if ti != nil && ti >= 0
        wants_toggle = true
      end
    end

    if wants_toggle
      var power_slot = '2'
      var has_one = string.find(u, '1')
      var has_two = string.find(u, '2')
      if has_one != nil && has_one >= 0 && (has_two == nil || has_two < 0)
        power_slot = '1'
      end

      var cmd = 'Power' + power_slot + ' 2'
      var turn_on = string.find(u, 'turn on')
      var turn_off = string.find(u, 'turn off')
      if turn_on != nil && turn_on >= 0
        cmd = 'Power' + power_slot + ' 1'
      elif turn_off != nil && turn_off >= 0
        cmd = 'Power' + power_slot + ' 0'
      end

      var action = 'toggle'
      if turn_on != nil && turn_on >= 0
        action = 'on'
      elif turn_off != nil && turn_off >= 0
        action = 'off'
      end

      return {
        'tool':'power_control',
        'args':{'slot':power_slot,'action':action},
        'reason':'Change relay POWER' + power_slot + ' state.'
      }
    end

    var asks_sensor = false
    for sw:['sensor','temperature','humidity','shtc3','i2c']
      var si = string.find(u, sw)
      if si != nil && si >= 0
        asks_sensor = true
      end
    end

    var asks_power = false
    for pw:['power','relay']
      var pi = string.find(u, pw)
      if pi != nil && pi >= 0
        asks_power = true
      end
    end

    if asks_sensor && asks_power
      return {'tool':'device_read','args':{},'reason':'Read sensors and power state together.'}
    end

    if asks_sensor
      return {'tool':'sensor_read','args':{},'reason':'Read sensor data and I2C status.'}
    end

    if asks_power
      return {'tool':'power_read','args':{},'reason':'Read relay and power state.'}
    end

    var asks_sd = string.find(u, 'sd')
    var asks_ufs = string.find(u, 'ufs')
    var asks_filesystem = string.find(u, 'filesystem')
    if (asks_sd != nil && asks_sd >= 0) || (asks_ufs != nil && asks_ufs >= 0) || (asks_filesystem != nil && asks_filesystem >= 0)
      var wants_write_sd = false
      for swr:['write','create','make','save','put']
        var swri = string.find(u, swr)
        if swri != nil && swri >= 0
          wants_write_sd = true
        end
      end

      if wants_write_sd
        var sd_content = ''
        var sd_name = 'note.txt'
        if string.find(u, 'hello world') != nil && string.find(u, 'hello world') >= 0
          sd_content = 'hello world\n'
          sd_name = 'hello_world.txt'
        else
          var marker_sd = string.find(u, 'with the text')
          var marker_sd_len = size('with the text')
          if marker_sd == nil || marker_sd < 0
            marker_sd = string.find(u, 'containing')
            marker_sd_len = size('containing')
          end
          if marker_sd != nil && marker_sd >= 0 && marker_sd + marker_sd_len < size(user)
            sd_content = user[marker_sd + marker_sd_len..size(user)-1]
          else
            sd_content = user
          end
        end

        return {'tool':'sd_markdown_write','args':{'name':sd_name,'content':sd_content},'reason':'Write a text file to the mounted SD card.'}
      end

      var wants_list = false
      for lw:['list','show','content','contents','files','what']
        var li = string.find(u, lw)
        if li != nil && li >= 0
          wants_list = true
        end
      end
      if wants_list
        return {'tool':'sd_markdown_list','args':{},'reason':'List files on the mounted SD card.'}
      end

      var wants_status = false
      for lws:['info','status','mounted','mount']
        var lsi = string.find(u, lws)
        if lsi != nil && lsi >= 0
          wants_status = true
        end
      end
      if wants_status
        return {'tool':'ufs_info','args':{},'reason':'Read filesystem and SD card status.'}
      end
    end

    var named_file = self.filename_from_text(user)
    if named_file != nil
      var wants_read_file = false
      for fr:['read','show','view','open','cat','display']
        var fri = string.find(u, fr)
        if fri != nil && fri >= 0
          wants_read_file = true
        end
      end

      if wants_read_file
        return {'tool':'sd_markdown_read','args':{'name':named_file,'max_bytes':8192},'reason':'Read the requested file from the mounted SD card.'}
      end
    end

    var md_name = nil
    for mn:['memory.md','agent.md','soul.md','user.md']
      var mi = string.find(u, mn)
      if mi != nil && mi >= 0
        md_name = mn
      end
    end

    if md_name != nil
      var wants_write_md = false
      for mw:['write','create','make','save','put']
        var mwi = string.find(u, mw)
        if mwi != nil && mwi >= 0
          wants_write_md = true
        end
      end

      if wants_write_md
        var content = '# ' + md_name + '\n'
        var marker = string.find(u, 'with the text')
        var marker_len = size('with the text')
        if marker == nil || marker < 0
          marker = string.find(u, 'containing')
          marker_len = size('containing')
        end
        if marker != nil && marker >= 0 && marker + marker_len < size(user)
          content = user[marker + marker_len..size(user)-1]
        end
        return {'tool':'sd_markdown_write','args':{'name':md_name,'content':content},'reason':'Write markdown memory file on the mounted SD card.'}
      end

      return {'tool':'sd_markdown_read','args':{'name':md_name},'reason':'Read markdown memory file from the mounted SD card.'}
    end

    var says_berry = string.find(u, 'berry')
    var says_skill = string.find(u, 'skill')
    if says_berry != nil && says_berry >= 0 && says_skill != nil && says_skill >= 0
      var create_skill = false
      for cs:['create','write','make','save','install','register']
        var csi = string.find(u, cs)
        if csi != nil && csi >= 0
          create_skill = true
        end
      end

      var load_skill = false
      for ls:['load','run','execute']
        var lsi = string.find(u, ls)
        if lsi != nil && lsi >= 0
          load_skill = true
        end
      end

      var explain_skill = false
      for es:['explain','describe','what does']
        var esi = string.find(u, es)
        if esi != nil && esi >= 0
          explain_skill = true
        end
      end

      var skill_name = self.first_token(self.text_after_marker(user, ['called ', 'named ']))
      if skill_name == ''
        skill_name = self.first_token(self.text_after_marker(user, ['skill ']))
      end
      if skill_name == ''
        skill_name = 'tasmo_skill'
      end

      var skill_cmd = self.first_token(self.text_after_marker(user, ['command ']))
      if skill_cmd == ''
        skill_cmd = skill_name
      end

      if create_skill
        return {
          'tool':'berry_skill_create',
          'args':{'name':skill_name,'command':skill_cmd,'autoload':load_skill},
          'reason':'Create a reusable Berry skill that registers a Tasmota command.'
        }
      elif explain_skill
        return {'tool':'berry_skill_explain','args':{'name':skill_name},'reason':'Read and explain the requested Berry skill.'}
      elif load_skill
        return {'tool':'berry_skill_run','args':{'name':skill_name},'reason':'Load the requested Berry skill.'}
      end
    end

    var says_hello_world = string.find(u, 'hello world')
    var says_file = string.find(u, 'file')
    if says_berry != nil && says_berry >= 0 && says_hello_world != nil && says_hello_world >= 0 && says_file != nil && says_file >= 0
      return {
        'tool':'berry_program_write',
        'args':{
          'name':'hello_world'
        },
        'reason':'Create a runnable Hello World Berry program in the TasmoClaw workspace.'
      }
    end

    if says_berry != nil && says_berry >= 0
      var run_berry = false
      for rb:['run','load','execute']
        var rbi = string.find(u, rb)
        if rbi != nil && rbi >= 0
          run_berry = true
        end
      end

      var explain_berry = false
      for eb:['explain','describe','what does']
        var ebi = string.find(u, eb)
        if ebi != nil && ebi >= 0
          explain_berry = true
        end
      end

      var read_berry = false
      for bb:['read','show','view','list']
        var bbi = string.find(u, bb)
        if bbi != nil && bbi >= 0
          read_berry = true
        end
      end

      var berry_name = 'hello_world'
      if explain_berry
        return {'tool':'berry_program_explain','args':{'name':berry_name},'reason':'Read Berry program source so it can be explained.'}
      end
      if run_berry
        return {'tool':'berry_program_run','args':{'name':berry_name},'reason':'Load and run the Berry program.'}
      end
      if read_berry
        return {'tool':'berry_program_read','args':{'name':berry_name},'reason':'Read the Berry program source.'}
      end
    end

    var has_rule = string.find(u, 'rule')
    if has_rule != nil && has_rule >= 0
      var add_rule = false
      for aw:['add','set','create','make','apply']
        var ai = string.find(u, aw)
        if ai != nil && ai >= 0
          add_rule = true
        end
      end

      var says_hello = string.find(u, 'hello')
      var says_5 = string.find(u, '5 second')
      if add_rule && says_hello != nil && says_hello >= 0 && says_5 != nil && says_5 >= 0
        return {
          'tool':'rule_apply',
          'args':{
            'rule':'Rule3',
            'definition':'ON Rules#Timer=1 DO Backlog Br print(\'hello\'); RuleTimer1 5 ENDON',
            'enable':true,
            'start_timer1':true,
            'timer1_seconds':5
          },
          'reason':'Create Rule3 to print hello every 5 seconds and start RuleTimer1 immediately.'
        }
      end

      var target_hello_rule = false
      for hw:['hello','that rule','timer rule','rule3']
        var hi = string.find(u, hw)
        if hi != nil && hi >= 0
          target_hello_rule = true
        end
      end

      if target_hello_rule
        for dw:['disable','stop','turn off']
          var di = string.find(u, dw)
          if di != nil && di >= 0
            return {
              'tool':'rule_control',
              'args':{'rule':'Rule3','action':'disable'},
              'reason':'Disable the Rule3 hello timer rule.'
            }
          end
        end

        for rw:['remove','delete','clear','erase']
          var ri_remove = string.find(u, rw)
          if ri_remove != nil && ri_remove >= 0
            return {
              'tool':'rule_clear',
              'args':{'rule':'Rule3','stop_timer1':true},
              'reason':'Disable and clear the Rule3 hello timer rule.'
            }
          end
        end
      end

      var is_write = false
      for w:['add','set','change','create','make','apply','enable','disable','delete','remove','update','run']
        var wi = string.find(u, w)
        if wi != nil && wi >= 0
          is_write = true
        end
      end

      if is_write
        return nil
      end

      for w2:['show','view','give','current','read','list','what']
        var ri = string.find(u, w2)
        if ri != nil && ri >= 0
          return {'tool':'rule_control','args':{'rule':'Rules','action':'read'},'reason':'Read all Tasmota rules.'}
        end
      end

      if u == 'rules'
        return {'tool':'rule_control','args':{'rule':'Rules','action':'read'},'reason':'Read all Tasmota rules.'}
      end
    end

    return nil
  end

  def parse_tool_block(c)
    var a='<<<TASMOCLAW_TOOL>>>'
    var b='<<<END_TASMOCLAW_TOOL>>>'

    if c == nil
      return nil
    end

    var i=string.find(c,a)
    var j=string.find(c,b)

    if i == nil
      return nil
    end

    if i < 0
      return nil
    end

    var s = ''

    if j != nil && j >= 0 && j > i
      s=c[i+size(a)..j-1]
    else
      s=c[i+size(a)..size(c)-1]
      var k=string.find(s,'<<<')
      if k != nil && k >= 0
        s=s[0..k-1]
      end
    end

    var open_brace=string.find(s,'{')
    if open_brace == nil || open_brace < 0
      return nil
    end
    s=s[open_brace..size(s)-1]

    try
      return json.load(s)
    except .. as e,m
      return nil
    end
  end

  def api_approve()
    if self.pending==nil
      self.api_json({'ok':false,'error':'no pending action'})
      return
    end

    var p=self.pending

    self.pending=nil
    self.store.save_pending(nil)

    var r=self.tools.run(p['tool'],p['args'])

    self.history.push({
      'role':'assistant',
      'content':'Approved TasmoClaw tool '+p['tool']+' result:\n'+tasmoclaw_util.json_encode(r)
    })

    self.trim_history()
    self.store.save_history(self.history)

    self.api_json({'ok':true,'result':r})
  end

  def api_test()
    var cfg2 = {}
    for k:self.cfg.keys()
      cfg2[k] = self.cfg[k]
    end
    cfg2['max_tokens'] = 100
    cfg2['thinking'] = 'omit'

    var msgs=[
      {'role':'system','content':'You are TasmoClaw. Reply exactly as requested.'},
      {'role':'user','content':'Reply with exactly: TasmoClaw online.'}
    ]

    var r=self.llm.call_chat(cfg2,msgs)

    if r['ok']
      self.api_json({'ok':true,'content':r['content'],'transport':r.find('transport'),'status':r.find('status')})
    else
      self.api_json({
        'ok':false,
        'error':r['error'],
        'transport':r.find('transport'),
        'status':r.find('status'),
        'stage':r.find('stage'),
        'esp_err':r.find('esp_err'),
        'body':r.find('body'),
        'hint':r.find('hint'),
        'fallback_hint':r.find('fallback_hint'),
        'webclient_error':r.find('webclient_error')
      })
    end
  end

  def trim_history()
    var lim=self.cfg['history_limit']*2

    while size(self.history)>lim
      self.history.remove(0)
    end
  end

end

def start()
  try
    if global.tasmoclaw_driver
      global.tasmoclaw_driver.stop()
    end
  except .. as e0,m0
  end

  _driver = TasmoClawDriver()

  # Register web handlers directly.
  # This avoids depending on Driver callback discovery.
  try
    _driver.web_add_handler()
    print('TasmoClaw web handlers registered')
  except .. as e,m
    print('TasmoClaw web handler registration failed: ' + str(m))
  end

  # Keep driver registration for buttons/commands/lifecycle.
  try
    tasmota.add_driver(_driver)
    global.tasmoclaw_driver = _driver
    print('TasmoClaw driver registered')
  except .. as e2,m2
    print('TasmoClaw driver registration failed: ' + str(m2))
  end

  print('TasmoClaw started')

  return _driver
end

start()

var tasmoclaw = module("tasmoclaw")
tasmoclaw.start = start

return tasmoclaw
