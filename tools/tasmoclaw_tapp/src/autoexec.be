import sys

var wd = tasmota.wd

if size(wd) > 0
  sys.path().push(wd)
  load(wd + "tasmoclaw.be")
else
  load("tasmoclaw.be")
end

if size(wd) > 0
  sys.path().pop()
end

print('TasmoClaw autoexec complete')