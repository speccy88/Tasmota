# TasmoClaw TAPP

TasmoClaw is a Berry-only Tasmota application packaged as one `.tapp` file. It provides a web chat UI, config UI, DeepSeek API calls, and approval-based tool actions.

## Requirements
- Tasmota ESP32 build with Berry, webserver, filesystem, and webclient.
- HTTPS support for `https://api.deepseek.com/chat/completions` (or use `http://` endpoint).

## Build
```bash
python3 build_tapp.py
```

## Install
1. Upload `dist/tasmoclaw.tapp` to Tasmota filesystem.
2. Reboot device.
3. Open `http://<device-ip>/tasmoclaw`.

## Configure
- API URL: `https://api.deepseek.com/chat/completions`
- Model: `deepseek-v4-flash` (default) or `deepseek-v4-pro`
- API key: your DeepSeek key

## Example prompts
- Show me device status.
- Scan I2C and explain what you see.
- Run Status 0.
- Create a Berry command called AIStatus that reports memory and Wi-Fi.
- Create a Tasmota Rule1 that refreshes the display every 5 minutes.
- Write a Berry file that displays heap, RSSI, and time on the screen.

## Sample API flow
Request:
`POST /tasmoclaw/api/chat` body `{"message":"Show me status"}`

Response:
`{"ok":true,"content":"..."}` or `{"ok":true,"approval_required":true,"pending":{...}}`

## Known limitations
- no streaming, no MCP, no Telegram/IM
- DeepSeek Chat Completions only
- HTTPS support depends on build
- large responses may fail on ESP32 webclient
- API key is stored on-device
- simple single-pending approval model

## Troubleshooting
- missing webclient
- 401 unauthorized
- 404 wrong API URL
- HTTPS unsupported
- JSON parse error
- no filesystem
- no button visible: browse directly to `/tasmoclaw`

## Manual smoke checklist
1. Load `/tasmoclaw` and `/tasmoclaw/config`.
2. Save config with masked key behavior.
3. Send chat prompt and receive response.
4. Trigger approval-required tool and approve/reject.
5. Run `TasmoClaw`, `TasmoClawReset`, `TasmoClawTest` commands.

## Build artifact policy
To build the TAPP locally:

```bash
python3 build_tapp.py
```

This creates:

`tools/tasmoclaw_tapp/dist/tasmoclaw.tapp`

Do not commit the generated `.tapp` file. Attach it to a GitHub Release if you want to distribute a ready-to-upload binary artifact.
