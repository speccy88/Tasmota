import tasmota
import tasmoclaw_util

class TasmoClawTools
  var tool_defs
  def init()
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
    for k:v in self.tool_defs out += '- ' + k + ': ' + v['desc'] + '\n' end
    return out
  end
  def requires_approval(name)
    if self.tool_defs.contains(name) return self.tool_defs[name]['approval'] end
    return true
  end
  def run(name, args)
    if name=='tasmota_status' return self.run_status(args) end
    if name=='tasmota_cmd_read' return self.run_cmd_read(args) end
    if name=='file_read' return self.file_read(args) end
    if name=='file_list' return {'ok':false,'error':'file_list unavailable in this build'} end
    if name=='berry_check' return {'ok':true,'result':'syntax-check requires compile approval on this build'} end
    if name=='tasmota_cmd' return {'ok':true,'result':tasmota.cmd(args['command'], false)} end
    if name=='file_write' return self.file_write(args) end
    if name=='berry_load' return {'ok':true,'result':load(args['path'])} end
    if name=='berry_compile' return {'ok':true,'result':tasmota.compile(args['path'])} end
    if name=='rule_apply' return self.rule_apply(args) end
    if name=='display_message' return {'ok':true,'result':tasmota.cmd('DisplayText ' + args['message'], false)} end
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
    for a:allowed if l.starts_with(a) ok=true; break end end
    if !ok return {'ok':false,'error':'command not read-only; request tasmota_cmd with approval'} end
    return {'ok':true,'result':tasmota.cmd(c, true)}
  end
  def file_read(args)
    var p=args['path']; var m=args.find('max_bytes')
    if m==nil m=4096 end
    if m>16384 return {'ok':false,'error':'max_bytes too large'} end
    try
      var f=open(p,'r'); var s=f.read(m); f.close()
      return {'ok':true,'result':s}
    except .. as e,msg
      return {'ok':false,'error':'read failed: '+str(msg)}
    end
  end
  def file_write(args)
    try var f=open(args['path'],'w'); f.write(args['content']); f.close(); return {'ok':true,'result':'written'}
    except .. as e,msg return {'ok':false,'error':'write failed: '+str(msg)} end
  end
  def rule_apply(args)
    var r=args['rule']; var d=args['definition']
    var r1=tasmota.cmd(r+' '+d,false)
    var r2=''
    if args.find('enable') r2=tasmota.cmd(r+' 1',false) end
    return {'ok':true,'result':{'set':r1,'enable':r2}}
  end
  def create_demo_berry(args)
    var n=args['name']
    var p='/tasmoclaw/berry/'+n+'.be'
    var c="import tasmota\n\ndef ai_status_cmd(cmd, idx, payload)\n  var out = '{\\\"heap\\\":' + str(tasmota.memory()) + ',\\\"wifi\\\":' + str(tasmota.wifi()) + '}'\n  tasmota.resp_cmnd(out)\nend\n\ntasmota.add_cmd('AIStatus', ai_status_cmd)\n"
    return self.file_write({'path':p,'content':c})
  end
end
