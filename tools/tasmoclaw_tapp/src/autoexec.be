import sys

def tcl_load_file(wd, name)
  if size(wd) > 0
    load(wd + name)
  else
    load(name)
  end
end

var wd = tasmota.wd
var mem = tasmota.memory()

if !mem.contains("psram") && !mem.contains("psram_free")
  print("TasmoClaw Full skipped: PSRAM is required. Tasmota is left running so the TAPP can be removed or replaced with tasmoclaw_lite.tapp.")
  return
end

if size(wd) > 0
  sys.path().push(wd)
end

tcl_load_file(wd, "tasmoclaw_util.be")
tcl_load_file(wd, "tasmoclaw_commands.be")
tcl_load_file(wd, "tasmoclaw_store.be")
tcl_load_file(wd, "tasmoclaw_tools.be")
tcl_load_file(wd, "tasmoclaw_llm.be")
tcl_load_file(wd, "tasmoclaw_ui.be")
tcl_load_file(wd, "tasmoclaw_prompt.be")
tcl_load_file(wd, "tasmoclaw.be")

print('TasmoClaw autoexec complete')
