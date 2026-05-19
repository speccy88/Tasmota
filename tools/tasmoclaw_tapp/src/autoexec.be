import sys
import tasmota
var wd = tasmota.wd
if size(wd) > 0
  sys.path().push(wd)
end
import tasmoclaw
tasmoclaw.start()
if size(wd) > 0
  sys.path().pop()
end
print('TasmoClaw autoexec complete')
