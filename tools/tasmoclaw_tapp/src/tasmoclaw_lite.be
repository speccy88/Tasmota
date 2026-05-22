# Tiny Lite module wrapper used by both standalone and extension entrypoints.

import tasmoclaw_common
import tasmoclaw_ui

return tasmoclaw_common.start('lite', tasmoclaw_ui.create())
