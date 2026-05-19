# TasmoClaw TAPP

TasmoClaw is a Berry-only Tasmota Application packaged as one locally generated `.tapp` file. It adds a small web chat page to a Tasmota ESP32 device, talks directly to the DeepSeek Chat Completions API, and exposes a demo-oriented set of approval-gated tools for reading status, running safe read commands, writing files, loading/compiling Berry files, applying rules, and creating a simple Berry command file.

TasmoClaw does not use native C++ changes, MCP, streaming, Telegram/instant messaging, or a local proxy/bridge.

## Requirements

- Tasmota ESP32 build with Berry, webserver, filesystem, TAPP support, and webclient.
- Network and TLS support for `https://api.deepseek.com/chat/completions`.
- A DeepSeek API key.

## Build the TAPP

Build from this directory:

```bash
cd tools/tasmoclaw_tapp
python3 build_tapp.py
```

This creates `dist/tasmoclaw.tapp` using ZIP_STORED/uncompressed entries for Tasmota.

Do not commit `dist/tasmoclaw.tapp`. The generated `dist/`, `*.tapp`, `*.zip`, and `*.gz` artifacts are ignored by git. If you want to distribute a ready-to-upload `.tapp`, attach it to a GitHub Release, not the PR.

## Install

1. Upload `dist/tasmoclaw.tapp` to Tasmota filesystem.
2. Reboot device.
3. Open `http://<device-ip>/tasmoclaw`.

Direct URLs:

- Chat: `http://<device-ip>/tasmoclaw`
- Config: `http://<device-ip>/tasmoclaw/config`

## Configure DeepSeek

Open `/tasmoclaw/config` and set:

- API URL: `https://api.deepseek.com/chat/completions`
- Model default: `deepseek-v4-flash`
- Model optional: `deepseek-v4-pro`
- API key: your DeepSeek key
- Temperature, max tokens, thinking mode, reasoning effort, tool iteration limit, history limit, and optional extra system instructions.

The config page loads `/tasmoclaw/api/config`, saves with `POST /tasmoclaw/api/config`, and tests the current API settings with `POST /tasmoclaw/api/test`. The API key is masked as `********` when read back. Saving an empty key or `********` preserves the existing key; entering a new non-empty value replaces it.

Default DeepSeek request settings are non-streaming:

- `model`: `deepseek-v4-flash`
- `temperature`: `0.2`
- `max_tokens`: `900`
- `stream`: `false`
- `thinking`: disabled by default

## Example prompts

- Show me device status.
- Scan I2C and explain what you see.
- Run Status 0.
- Create a Berry command called AIStatus that reports memory and Wi-Fi.
- Create a Tasmota Rule1 that refreshes the display every 5 minutes.
- Write a Berry file that displays heap, RSSI, and time on the screen.

Read-only tools such as status and whitelisted commands can run immediately. Write tools and unsafe Tasmota commands create a pending approval shown on the TasmoClaw page.

## API Routes

- `GET /tasmoclaw`
- `GET /tasmoclaw/config`
- `POST /tasmoclaw/api/chat`
- `GET /tasmoclaw/api/config`
- `POST /tasmoclaw/api/config`
- `GET /tasmoclaw/api/status`
- `GET /tasmoclaw/api/tools`
- `GET /tasmoclaw/api/history`
- `POST /tasmoclaw/api/clear`
- `GET /tasmoclaw/api/pending`
- `POST /tasmoclaw/api/approve`
- `POST /tasmoclaw/api/reject`
- `POST /tasmoclaw/api/test`

Sample chat request:

```text
POST /tasmoclaw/api/chat
{"message":"Show me status"}
```

Response is either a normal answer:

```json
{"ok":true,"content":"..."}
```

or an approval request:

```json
{"ok":true,"approval_required":true,"pending":{...}}
```

## Storage Files

TasmoClaw prefers real filesystem JSON files:

- `/tasmoclaw_config.json`
- `/tasmoclaw_history.json`
- `/tasmoclaw_pending.json`

If file storage fails, the store attempts a `persist` fallback and returns/logs the storage error where possible.

## Generated Berry Workspace

At startup TasmoClaw tries to create:

- `/tasmoclaw/`
- `/tasmoclaw/berry/`
- `/tasmoclaw/logs/`

The `create_demo_berry` tool writes generated Berry files to `/tasmoclaw/berry/<name>.be` when those directories are available. If directory creation is unavailable or fails, it falls back to root-level files named `/tasmoclaw_demo_<name>.be`.

## Known limitations

- No streaming, MCP, Telegram/IM, native C++ integration, or local proxy.
- DeepSeek Chat Completions only.
- HTTPS support depends on the Tasmota build and device memory.
- DeepSeek response bodies are limited to roughly 24 KB for ESP32 webclient reliability.
- API key is stored on-device.
- Simple single-pending approval model.
- Browser UI is intentionally compact for Tasmota web pages.
- File storage falls back to `persist` only when direct filesystem access fails.

## Troubleshooting

- `Missing DeepSeek API key`: open `/tasmoclaw/config` and save a key.
- `webclient unavailable`: the firmware build may not include webclient support.
- HTTP 401/403: check the API key.
- HTTP 404: check the API URL.
- HTTPS or request failures: confirm TLS/network support in the Tasmota build.
- JSON parse failure: the API returned an unexpected body; the error includes a short preview.
- Storage write failure: confirm filesystem support and free space.
- No button visible in the Tasmota menu: browse directly to `/tasmoclaw`.

## Manual smoke checklist

1. Load `/tasmoclaw` and `/tasmoclaw/config`.
2. Save config and confirm the API key reads back as `********`.
3. Use Test API and expect `TasmoClaw online.` from DeepSeek.
4. Ask `Show me status` and confirm the model can use `tasmota_status`.
5. Ask `Run Status 0` and confirm `tasmota_cmd_read` is used without approval.
6. Ask for a `Rule1` change and confirm a pending approval appears.
7. Approve the pending action and confirm the result is shown.
8. Ask TasmoClaw to write a small file and confirm approval is required.
9. Ask TasmoClaw to create the demo Berry command file and confirm the returned path.
10. Run `TasmoClaw`, `TasmoClawReset`, and `TasmoClawTest` commands.

## Local Verification

```bash
git status
git diff --stat
git diff --binary --stat
cd tools/tasmoclaw_tapp
python3 build_tapp.py
unzip -l dist/tasmoclaw.tapp
```

The archive should contain only:

- `autoexec.be`
- `tasmoclaw.be`
- `tasmoclaw_ui.be`
- `tasmoclaw_tools.be`
- `tasmoclaw_llm.be`
- `tasmoclaw_store.be`
- `tasmoclaw_util.be`
- `tasmoclaw_prompt.be`
