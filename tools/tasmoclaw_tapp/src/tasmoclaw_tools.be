import string
import path
import json
import tasmoclaw_util

class TasmoClawTools
  var tool_defs, store

  def init(store)
    self.store = store

    self.tool_defs = {
      'tasmota_status':{'approval':false,'desc':'Read memory/wifi/arch/sensors and Status 0'},
      'tasmota_cmd_read':{'approval':false,'desc':'Run clearly read-only Tasmota command, including Rule1/Rule2/Rule3 reads. Rules is mapped to all rule slots.'},
      'device_read':{'approval':false,'desc':'Read sensors, power state, filesystem/SD status, memory, and Wi-Fi in one compact call'},
      'sensor_read':{'approval':false,'desc':'Read I2C scan and Status 8 sensor data'},
      'power_read':{'approval':false,'desc':'Read Power, Power1, Power2, and Status 0 power state'},
      'file_read':{'approval':false,'desc':'Read text file with byte limit'},
      'file_list':{'approval':false,'desc':'List files in the active Tasmota filesystem using UfsList'},
      'ufs_info':{'approval':false,'desc':'Read Tasmota UFS/SD card filesystem type, size, free space, and root listing'},
      'sd_markdown_read':{'approval':false,'desc':'Read a markdown file from the mounted SD card'},
      'sd_markdown_list':{'approval':false,'desc':'List markdown files in the mounted SD card root'},
      'berry_program_read':{'approval':false,'desc':'Read a Berry program file'},
      'berry_program_explain':{'approval':false,'desc':'Read a Berry program so TasmoClaw can explain it'},
      'berry_check':{'approval':false,'desc':'Check berry syntax availability'},
      'tasmota_cmd':{'approval':true,'desc':'Execute Tasmota command'},
      'file_write':{'approval':true,'desc':'Write file content'},
      'sd_markdown_write':{'approval':true,'desc':'Write a markdown file such as memory.md, agent.md, soul.md, or user.md to the mounted SD card'},
      'berry_program_write':{'approval':true,'desc':'Write a Berry program to /tasmoclaw/berry/ or the workspace fallback'},
      'berry_program_run':{'approval':true,'desc':'Load and run a Berry program file'},
      'berry_load':{'approval':true,'desc':'load(path)'},
      'berry_compile':{'approval':true,'desc':'tasmota.compile(path)'},
      'rule_apply':{'approval':true,'desc':'Apply and optionally enable rule'},
      'rule_clear':{'approval':true,'desc':'Disable and clear a rule slot'},
      'display_message':{'approval':true,'desc':'DisplayText message'},
      'create_demo_berry':{'approval':true,'desc':'Create demo Berry command file'}
    }
  end

  def registry()
    return self.tool_defs
  end

  def tool_lines()
    var out = ''
    for k:self.tool_defs.keys()
      out += '- ' + k + ': ' + self.tool_defs[k]['desc'] + '\n'
    end
    return out
  end

  def requires_approval(name)
    if self.tool_defs.find(name) != nil
      return self.tool_defs[name]['approval']
    end
    return true
  end

  def get_command_arg(args)
    var c = args.find('command')
    if c == nil || c == '' c = args.find('cmd') end
    if c == nil || c == '' c = args.find('cmnd') end
    return c
  end

  def run(name, args)
    if args == nil
      args = {}
    end

    if name=='tasmota_status' return self.run_status(args) end
    if name=='tasmota_cmd_read' return self.run_cmd_read(args) end
    if name=='device_read' return self.device_read(args) end
    if name=='sensor_read' return self.sensor_read(args) end
    if name=='power_read' return self.power_read(args) end
    if name=='file_read' return self.file_read(args) end
    if name=='file_list' return self.file_list(args) end
    if name=='ufs_info' return self.ufs_info(args) end
    if name=='sd_markdown_read' return self.sd_markdown_read(args) end
    if name=='sd_markdown_list' return self.sd_markdown_list(args) end
    if name=='berry_program_read' return self.berry_program_read(args) end
    if name=='berry_program_explain' return self.berry_program_explain(args) end
    if name=='berry_check' return {'ok':true,'result':'syntax-check requires compile approval on this build'} end
    if name=='tasmota_cmd' return self.tasmota_cmd(args) end
    if name=='file_write' return self.file_write(args) end
    if name=='sd_markdown_write' return self.sd_markdown_write(args) end
    if name=='berry_program_write' return self.berry_program_write(args) end
    if name=='berry_program_run' return self.berry_program_run(args) end
    if name=='berry_load' return self.berry_load(args) end
    if name=='berry_compile' return self.berry_compile(args) end
    if name=='rule_apply' return self.rule_apply(args) end
    if name=='rule_clear' return self.rule_clear(args) end
    if name=='display_message' return self.display_message(args) end
    if name=='create_demo_berry' return self.create_demo_berry(args) end

    return {'ok':false,'error':'Unknown tool: ' + name}
  end

  def normalize_name(name, ext)
    if name == nil || name == ''
      name = 'memory'
    end

    var n = str(name)
    n = string.replace(n, '/', '_')
    n = string.replace(n, '\\', '_')
    n = string.replace(n, ' ', '_')
    n = string.replace(n, '..', '_')

    var has_dot = string.find(n, '.')
    if ext == '.md' && has_dot != nil && has_dot >= 0
      return n
    end

    var ei = string.find(n, ext)
    if ei == nil || ei < 0 || ei != size(n) - size(ext)
      n += ext
    end

    return n
  end

  def sd_mounted()
    try
      var r = tasmota.cmd('UfsType', true)
      var t = r.find('UfsType')
      if t == 1
        return true
      end
      var ts = str(t)
      if string.find(ts, '1') != nil && string.find(ts, '1') >= 0
        return true
      end
    except .. as e,m
    end
    return false
  end

  def ufs_read_file(p, max_bytes)
    try
      var raw = tasmo_ufs_read(p, max_bytes)
      return json.load(raw)
    except .. as e,m
      return {'ok':false,'error':'native SD read failed: '+str(m)}
    end
  end

  def ufs_write_file(p, content)
    try
      var raw = tasmo_ufs_write(p, content)
      return json.load(raw)
    except .. as e,m
      return {'ok':false,'error':'native SD write failed: '+str(m)}
    end
  end

  def ufs_list_dir(p)
    try
      var raw = tasmo_ufs_list(p)
      return json.load(raw)
    except .. as e,m
      return {'ok':false,'error':'native SD list failed: '+str(m)}
    end
  end

  def power_snapshot()
    var out = {}
    try
      var status0 = tasmota.cmd('Status 0', true)
      out['status0'] = status0
      var sts = status0.find('StatusSTS')
      if sts != nil
        out['POWER1'] = sts.find('POWER1')
        out['POWER2'] = sts.find('POWER2')
        out['Power'] = sts.find('Power')
        if out['Power'] == nil && (out['POWER1'] != nil || out['POWER2'] != nil)
          out['Power'] = 'POWER1=' + str(out['POWER1']) + ', POWER2=' + str(out['POWER2'])
        end
      end
    except .. as e,m
      out['status0_error'] = str(m)
    end
    return out
  end

  def berry_path(name)
    var n = self.normalize_name(name, '.be')
    if self.store != nil && self.store.workspace_fallback
      return '/tasmoclaw_demo_' + n
    end
    return '/tasmoclaw/berry/' + n
  end

  def markdown_path(name)
    return '/' + self.normalize_name(name, '.md')
  end

  def run_status(args)
    var out = {
      'memory':tasmota.memory(),
      'wifi':tasmota.wifi(),
      'arch':tasmota.arch()
    }

    try
      out['sensors']=tasmota.read_sensors(false)
    except .. as e,m
    end

    try
      out['status0']=tasmota.cmd('Status 0', true)
    except .. as e,m
    end

    return {'ok':true, 'result':out}
  end

  def run_cmd_read(args)
    var c = self.get_command_arg(args)

    if c == nil || c == ''
      return {'ok':false,'error':'missing command'}
    end

    var lower = string.tolower(c)

    var first_space = string.find(lower, ' ')
    var first = lower
    var rest = ''

    if first_space != nil && first_space >= 0
      first = lower[0..first_space-1]
      rest = lower[first_space+1..size(lower)-1]
    end

    var ok = false

    if first == 'status'
      ok = true
    elif first == 'time' && rest == ''
      ok = true
    elif first == 'uptime' && rest == ''
      ok = true
    elif first == 'mem' && rest == ''
      ok = true
    elif first == 'module' && rest == ''
      ok = true
    elif first == 'template' && rest == ''
      ok = true
    elif first == 'gpio' && rest == ''
      ok = true
    elif first == 'i2cscan' && rest == ''
      ok = true
    elif first == 'sensor'
      ok = true
    elif first == 'wifi'
      ok = true
    elif first == 'ipaddress' && rest == ''
      ok = true
    elif first == 'teleperiod' && rest == ''
      ok = true
    elif first == 'power' && rest == ''
      ok = true
    elif (first == 'power1' || first == 'power2') && rest == ''
      ok = true
    elif first == 'ufs' && rest == ''
      ok = true
    elif first == 'ufstype' && rest == ''
      ok = true
    elif first == 'ufssize' && rest == ''
      ok = true
    elif first == 'ufsfree' && rest == ''
      ok = true
    elif first == 'ufslist'
      ok = true
    elif first == 'rules' && rest == ''
      ok = true
    elif (first == 'rule1' || first == 'rule2' || first == 'rule3') && rest == ''
      ok = true
    end

    if !ok
      return {'ok':false,'error':'command is not clearly read-only; request tasmota_cmd with approval'}
    end

    try
      if first == 'rules'
        return {
          'ok':true,
          'result':{
            'Rule1':tasmota.cmd('Rule1', true),
            'Rule2':tasmota.cmd('Rule2', true),
            'Rule3':tasmota.cmd('Rule3', true)
          }
        }
      end
      return {'ok':true,'result':tasmota.cmd(c, true)}
    except .. as e,m
      return {'ok':false,'error':'command failed: '+str(m)}
    end
  end

  def sensor_read(args)
    var out = {}

    try
      out['i2cscan'] = tasmota.cmd('I2CScan', true)
    except .. as e,m
      out['i2cscan_error'] = str(m)
    end

    try
      out['status8'] = tasmota.cmd('Status 8', true)
    except .. as e2,m2
      out['status8_error'] = str(m2)
    end

    try
      out['sensors'] = tasmota.read_sensors(false)
    except .. as e4,m4
      out['sensors_error'] = str(m4)
    end

    return {'ok':true,'result':out}
  end

  def device_read(args)
    var out = {
      'memory':tasmota.memory(),
      'wifi':tasmota.wifi(),
      'sd_mounted':self.sd_mounted()
    }

    try
      out['i2cscan'] = tasmota.cmd('I2CScan', true)
    except .. as e,m
      out['i2cscan_error'] = str(m)
    end

    try
      out['status8'] = tasmota.cmd('Status 8', true)
    except .. as e1,m1
      out['status8_error'] = str(m1)
    end

    try
      out['power'] = self.power_snapshot()
    except .. as e2,m2
      out['power_error'] = str(m2)
    end

    try
      out['ufs'] = tasmota.cmd('Ufs', true)
    except .. as e5,m5
      out['ufs_error'] = str(m5)
    end

    return {
      'ok':true,
      'result':out
    }
  end

  def power_read(args)
    var out = self.power_snapshot()

    return {'ok':true,'result':out}
  end

  def tasmota_cmd(args)
    var c = self.get_command_arg(args)

    if c == nil || c == ''
      return {'ok':false,'error':'missing command'}
    end

    try
      return {'ok':true,'result':tasmota.cmd(c, false)}
    except .. as e,m
      return {'ok':false,'error':'command failed: '+str(m)}
    end
  end

  def file_read(args)
    var p=args.find('path')
    var m=args.find('max_bytes')

    if p == nil || p == ''
      return {'ok':false,'error':'missing path'}
    end

    if m==nil
      m=4096
    end

    if m<1
      m=4096
    end

    if m>16384
      return {'ok':false,'error':'max_bytes too large'}
    end

    try
      if path.exists(p) != true
        return {'ok':false,'error':'path not found: '+p}
      end

      var f=open(p,'r')
      var s=f.read(m)
      f.close()

      return {'ok':true,'path':p,'bytes':size(s),'result':s}

    except .. as e,msg
      return {'ok':false,'error':'read failed: '+str(msg)}
    end
  end

  def file_write(args)
    var p=args.find('path')
    var c=args.find('content')

    if p == nil || p == ''
      return {'ok':false,'error':'missing path'}
    end

    if c == nil
      c = ''
    end

    try
      var f=open(p,'w')
      f.write(c)
      f.close()
      return {'ok':true,'path':p,'bytes':size(c)}
    except .. as e,msg
      return {'ok':false,'error':'write failed: '+str(msg)}
    end
  end

  def file_list(args)
    var p = args.find('path')
    if p == nil || p == ''
      p = '/'
    end

    try
      return {'ok':true,'path':p,'result':tasmota.cmd('UfsList ' + p, true)}
    except .. as e,m
      return {'ok':false,'error':'file list failed: '+str(m)}
    end
  end

  def ufs_info(args)
    var out = {}

    try
      out['ufs'] = tasmota.cmd('Ufs', true)
    except .. as e,m
      out['ufs_error'] = str(m)
    end

    try
      out['type'] = tasmota.cmd('UfsType', true)
    except .. as e1,m1
      out['type_error'] = str(m1)
    end

    try
      out['size'] = tasmota.cmd('UfsSize', true)
    except .. as e2,m2
      out['size_error'] = str(m2)
    end

    try
      out['free'] = tasmota.cmd('UfsFree', true)
    except .. as e3,m3
      out['free_error'] = str(m3)
    end

    try
      out['list'] = tasmota.cmd('UfsList', true)
    except .. as e4,m4
      out['list_error'] = str(m4)
    end
    out['sd_mounted'] = self.sd_mounted()
    if out['sd_mounted']
      out['sd_list'] = self.ufs_list_dir('/')
    end

    return {'ok':true,'result':out}
  end

  def sd_markdown_read(args)
    if !self.sd_mounted()
      return {'ok':false,'error':'SD card is not mounted. Check USE_SDCARD and SDIO pins CMD=GPIO21, CLK=GPIO38, D0=GPIO39.'}
    end

    var n = args.find('name')
    if n == nil || n == ''
      n = args.find('filename')
    end
    if n == nil || n == ''
      n = args.find('path')
    end

    var p = self.markdown_path(n)
    return self.ufs_read_file(p, args.find('max_bytes') == nil ? 8192 : args.find('max_bytes'))
  end

  def sd_markdown_write(args)
    if !self.sd_mounted()
      return {'ok':false,'error':'SD card is not mounted. Check USE_SDCARD and SDIO pins CMD=GPIO21, CLK=GPIO38, D0=GPIO39.'}
    end

    var n = args.find('name')
    if n == nil || n == ''
      n = args.find('filename')
    end
    if n == nil || n == ''
      n = args.find('path')
    end

    var p = self.markdown_path(n)
    var c = args.find('content')
    if c == nil
      c = ''
    end

    var r = self.ufs_write_file(p, c)
    if r['ok']
      r['sd'] = true
    end
    return r
  end

  def sd_markdown_list(args)
    if !self.sd_mounted()
      return {'ok':false,'error':'SD card is not mounted. Check USE_SDCARD and SDIO pins CMD=GPIO21, CLK=GPIO38, D0=GPIO39.'}
    end

    try
      return self.ufs_list_dir('/')
    except .. as e,m
      return {'ok':false,'error':'SD markdown list failed: '+str(m)}
    end
  end

  def berry_program_write(args)
    var p = args.find('path')
    if p == nil || p == ''
      p = self.berry_path(args.find('name'))
    end

    var c = args.find('content')
    if c == nil || c == ''
      c = "def hello_world_cmd(cmd, idx, payload, payload_json)\n"
      c += "  tasmota.resp_cmnd('{\"HelloWorld\":\"ok\"}')\n"
      c += "end\n\n"
      c += "tasmota.add_cmd('HelloWorld', hello_world_cmd)\n"
      c += "print('Hello World from TasmoClaw')\n"
    end

    var r = self.file_write({'path':p,'content':c})
    if r['ok']
      r['berry_program'] = true
    end
    return r
  end

  def berry_program_read(args)
    var p = args.find('path')
    if p == nil || p == ''
      p = self.berry_path(args.find('name'))
    end
    return self.file_read({'path':p,'max_bytes':args.find('max_bytes') == nil ? 8192 : args.find('max_bytes')})
  end

  def berry_program_explain(args)
    var r = self.berry_program_read(args)
    if r['ok']
      r['instruction'] = 'Explain this Berry source to the user, including commands it registers, filesystem side effects, and how to run it.'
    end
    return r
  end

  def berry_program_run(args)
    var p = args.find('path')
    if p == nil || p == ''
      p = self.berry_path(args.find('name'))
    end
    return self.berry_load({'path':p})
  end

  def berry_load(args)
    var p=args.find('path')

    if p == nil || p == ''
      return {'ok':false,'error':'missing path'}
    end

    try
      var r = load(p)
      return {'ok':true,'result':str(r)}
    except .. as e,m
      return {'ok':false,'error':'load failed: '+str(m)}
    end
  end

  def berry_compile(args)
    var p=args.find('path')

    if p == nil || p == ''
      return {'ok':false,'error':'missing path'}
    end

    try
      var r = tasmota.compile(p)
      return {'ok':true,'result':str(r)}
    except .. as e,m
      return {'ok':false,'error':'compile failed: '+str(m)}
    end
  end

  def rule_apply(args)
    var r=args.find('rule')
    var d=args.find('definition')

    if r == nil || r == ''
      r = 'Rule1'
    end

    if d == nil || d == ''
      return {'ok':false,'error':'missing definition'}
    end

    try
      var cmd1 = r + ' ' + d
      var r1 = tasmota.cmd(cmd1, false)

      var result = {
        'set_command':cmd1,
        'set':r1
      }

      if args.find('enable') == true
        var cmd2 = r + ' 1'
        var r2 = tasmota.cmd(cmd2, false)

        result['enable_command'] = cmd2
        result['enable'] = r2
      end

      if args.find('start_timer1') == true
        var seconds = args.find('timer1_seconds')
        if seconds == nil || seconds < 1
          seconds = 5
        end
        var cmd3 = 'RuleTimer1 ' + str(seconds)
        var r3 = tasmota.cmd(cmd3, false)

        result['start_timer_command'] = cmd3
        result['start_timer'] = r3
      end

      return {'ok':true,'result':result}

    except .. as e,m
      return {'ok':false,'error':'rule apply failed: '+str(m)}
    end
  end

  def rule_clear(args)
    var r=args.find('rule')

    if r == nil || r == ''
      r = 'Rule3'
    end

    try
      var cmd1 = r + ' 0'
      var r1 = tasmota.cmd(cmd1, false)
      var result = {
        'disable_command':cmd1,
        'disable':r1
      }

      if args.find('stop_timer1') == true
        var cmd_timer = 'RuleTimer1 0'
        result['timer_command'] = cmd_timer
        result['timer'] = tasmota.cmd(cmd_timer, false)
      end

      # Tasmota clears a rule when the payload begins with a double quote.
      var cmd2 = r + ' "'
      var r2 = tasmota.cmd(cmd2, false)
      result['clear_command'] = cmd2
      result['clear'] = r2

      return {'ok':true,'result':result}

    except .. as e,m
      return {'ok':false,'error':'rule clear failed: '+str(m)}
    end
  end

  def display_message(args)
    var msg=args.find('message')

    if msg == nil
      msg = ''
    end

    try
      var r = tasmota.cmd('DisplayText ' + msg, false)
      return {'ok':true,'result':r}
    except .. as e,m
      return {'ok':false,'error':'DisplayText failed; display may not be configured: '+str(m)}
    end
  end

  def create_demo_berry(args)
    var n=args.find('name')

    if n == nil || n == ''
      n = 'ai_status'
    end

    n = string.replace(n, '/', '_')
    n = string.replace(n, '\\', '_')
    n = string.replace(n, ' ', '_')

    var p = self.berry_path(n)

    var c = "import string\n\n"
    c += "def ai_status_cmd(cmd, idx, payload)\n"
    c += "  var mem = str(tasmota.memory())\n"
    c += "  var wifi = str(tasmota.wifi())\n"
    c += "  mem = string.replace(mem, '\"', '\\\\\"')\n"
    c += "  wifi = string.replace(wifi, '\"', '\\\\\"')\n"
    c += "  var out = '{\"AIStatus\":{\"memory\":\"' + mem + '\",\"wifi\":\"' + wifi + '\"}}'\n"
    c += "  tasmota.resp_cmnd(out)\n"
    c += "end\n\n"
    c += "tasmota.add_cmd('AIStatus', ai_status_cmd)\n"

    var r = self.file_write({'path':p,'content':c})

    if r['ok']
      r['fallback_workspace'] = self.store != nil ? self.store.workspace_fallback : false
    end

    return r
  end

end

var tasmoclaw_tools = module("tasmoclaw_tools")

tasmoclaw_tools.create = def(store)
  return TasmoClawTools(store)
end

return tasmoclaw_tools
