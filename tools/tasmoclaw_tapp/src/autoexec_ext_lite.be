# Tasmota-Extensions Lite entrypoint.
#
# Lite is a smaller no-PSRAM friendly driver.  It imports the module and
# registers the returned driver object with Extension Manager.

do
  import introspect
  var app = introspect.module('tasmoclaw_lite', true)
  tasmota.add_extension(app)
end
