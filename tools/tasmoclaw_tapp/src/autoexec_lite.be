import sys

var wd = tasmota.wd

if size(wd) > 0
  sys.path().push(wd)
  load(wd + "tasmoclaw_lite.be")
else
  load("tasmoclaw_lite.be")
end

if size(wd) > 0
  sys.path().pop()
end

print('TasmoClaw Lite autoexec complete')
