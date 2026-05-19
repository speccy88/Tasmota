import string
import path
import tasmoclaw_util

class TasmoClawTools
  var tool_defs, store
  def init(store)
    self.store = store
    self.tool_defs = {
      'tasmota_status':{'approval':false,'desc':'Read memory/wifi/arch/sensors and Status 0'},
      'tasmota_cmd_read':{'approval':false,'desc':'Run read-only Tasmota command'},
      'file_read':{'approval':false,'desc':'Read text file with byte limit'},
      'file_list':{'approval':false,'desc':'List files in path if supported'},
      'berry_check':{'approval':false,'desc':'Check berry syntax availability'},
      'tasmota_cmd':{'approval':true,'desc':'Execute Tasmota command'},
      'file_write':{'approval':true,'desc':'Write file content'},
      'berry_load':{'approval':true,'desc':'load(path)'},
      'berry_compile':{'approval':true,'desc':'tasmota.compile(path)'},
      'rule_apply':{'approval':true,'desc':'Apply and enable rule'},
      'display_message':{'approval':true,'desc':'DisplayText message'},
      'create_demo_berry':{'approval':true,'desc':'Create demo Berry command file'}
    }
  end
  def registry() return self.tool_defs end
  def tool_lines()
    var out = ''
    for k:self.tool_defs.keys() out += '- ' + k + ': ' + self.tool_defs[k]['desc'] + '\n' end
    return out
  end
  def requires_approval(name)
    if self.tool_defs.contains(name) return self.tool_defs[name]['approval'] end
    return true
  end
  def run(name, args)
    if args == nil args = {} end
    if name=='tasmota_status' return self.run_status(args) end
    if name=='tasmota_cmd_read' return self.run_cmd_read(args) end
    if name=='file_read' return self.file_read(args) end
    if name=='file_list' return {'ok':false,'error':'file_list unavailable in this build'} end
    if name=='berry_check' return {'ok':true,'result':'syntax-check requires compile approval on this build'} end
    if name=='tasmota_cmd' return self.tasmota_cmd(args) end
    if name=='file_write' return self.file_write(args) end
    if name=='berry_load' return self.berry_load(args) end
    if name=='berry_compile' return self.berry_compile(args) end
    if name=='rule_apply' return self.rule_apply(args) end
    if name=='display_message' return self.display_message(args) end
    if name=='create_demo_berry' return self.create_demo_berry(args) end
    return {'ok':false,'error':'Unknown tool: ' + name}
  end
  def run_status(args)
    var out = {'memory':tasmota.memory(), 'wifi':tasmota.wifi(), 'arch':tasmota.arch()}
    try out['sensors']=tasmota.read_sensors(false) except .. as e,m end
    try out['status0']=tasmota.cmd('Status 0', true) except .. as e,m end
    return {'ok':true, 'result':out}
  end
  def run_cmd_read(args)
    var c = args.find('command')
    if c == nil return {'ok':false,'error':'missing command'} end
    var l = string.lower(c)
    var allowed=['status','time','uptime','mem','module','template','gpio','i2cscan','sensor','wifi','ipaddress','teleperiod']
    var ok=false
    for a:allowed
      if string.find(l, a) == 0 ok=true; break end
    end
    if !ok return {'ok':false,'error':'command not read-only; request tasmota_cmd with approval'} end
    try
      return {'ok':true,'result':tasmota.cmd(c, true)}
    except .. as e,m
      return {'ok':false,'error':'command failed: '+str(m)}
    end
  end
  def tasmota_cmd(args)
    var c = args.find('command')
    if c == nil || c == '' return {'ok':false,'error':'missing command'} end
    try
      return {'ok':true,'result':tasmota.cmd(c, false)}
    except .. as e,m
      return {'ok':false,'error':'command failed: '+str(m)}
    end
  end
  def file_read(args)
    var p=args['path']; var m=args.find('max_bytes')
    if p == nil || p == '' return {'ok':false,'error':'missing path'} end
    if m==nil m=4096 end
    if m<1 m=4096 end
    if m>16384 return {'ok':false,'error':'max_bytes too large'} end
    try
      if path.exists(p) != true return {'ok':false,'error':'path not found: '+p} end
      var f=open(p,'r'); var s=f.read(m); f.close()
      return {'ok':true,'path':p,'bytes':size(s),'result':s}
    except .. as e,msg
      return {'ok':false,'error':'read failed: '+str(msg)}
    end
  end
  def file_write(args)
    var p=args.find('path'); var c=args.find('content')
    if p == nil || p == '' return {'ok':false,'error':'missing path'} end
    if c == nil c = '' end
    try var f=open(p,'w'); f.write(c); f.close(); return {'ok':true,'path':p,'bytes':size(c)}
    except .. as e,msg return {'ok':false,'error':'write failed: '+str(msg)} end
  end
  def berry_load(args)
    var p=args.find('path')
    if p == nil || p == '' return {'ok':false,'error':'missing path'} end
    try
      var r = load(p)
      return {'ok':true,'result':str(r)}
    except .. as e,m
      return {'ok':false,'error':'load failed: '+str(m)}
    end
  end
  def berry_compile(args)
    var p=args.find('path')
    if p == nil || p == '' return {'ok':false,'error':'missing path'} end
    try
      var r = tasmota.compile(p)
      return {'ok':true,'result':str(r)}
    except .. as e,m
      return {'ok':false,'error':'compile failed: '+str(m)}
    end
  end
  def rule_apply(args)
    var r=args.find('rule'); var d=args.find('definition')
    if r == nil || r == '' r = 'Rule1' end
    if d == nil return {'ok':false,'error':'missing definition'} end
    try
      var cmd1 = r + ' ' + d
      var r1=tasmota.cmd(cmd1,false)
      var cmd2 = r + ' 1'
      var r2=tasmota.cmd(cmd2,false)
      return {'ok':true,'result':{'set_command':cmd1,'set':r1,'enable_command':cmd2,'enable':r2}}
    except .. as e,m
      return {'ok':false,'error':'rule apply failed: '+str(m)}
    end
  end
  def display_message(args)
    var msg=args.find('message')
    if msg == nil msg = '' end
    try
      var r = tasmota.cmd('DisplayText ' + msg, false)
      return {'ok':true,'result':r}
    except .. as e,m
      return {'ok':false,'error':'DisplayText failed; display may not be configured: '+str(m)}
    end
  end
  def create_demo_berry(args)
    var n=args.find('name')
    if n == nil || n == '' n = 'ai_status' end
    n = string.replace(n, '/', '_')
    n = string.replace(n, '\\', '_')
    var p='/tasmoclaw/berry/'+n+'.be'
    if self.store != nil && self.store.workspace_fallback p='/tasmoclaw_demo_'+n+'.be' end
    var c="def ai_status_cmd(cmd, idx, payload)\n  var heap = str(tasmota.memory('heap_free'))\n  var wifi = str(tasmota.wifi('quality'))\n  var out = '{\"AIStatus\":{\"heap\":\"' + heap + '\",\"wifi\":\"' + wifi + '\"}}'\n  tasmota.resp_cmnd(out)\nend\n\ntasmota.add_cmd('AIStatus', ai_status_cmd)\n"
    var r = self.file_write({'path':p,'content':c})
    if r['ok'] r['fallback_workspace'] = self.store != nil ? self.store.workspace_fallback : false end
    return r
  end
end

var tasmoclaw_tools = module("tasmoclaw_tools")
tasmoclaw_tools.create = def(store)
  return TasmoClawTools(store)
end
return tasmoclaw_tools
