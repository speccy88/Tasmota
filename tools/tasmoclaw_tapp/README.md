# TasmoClaw TAPP

TasmoClaw is a mostly Berry/TAPP Tasmota Application packaged as one locally generated `.tapp` file. It adds a small web chat page to a Tasmota ESP32 device, talks directly to the DeepSeek Chat Completions API, and exposes a demo-oriented set of tools for reading status, reading sensors and power, writing files, writing/reading/running/explaining Berry programs, writing SD-card markdown memory files, applying rules, and creating a simple Berry command file.

TasmoClaw does not use MCP, streaming, Telegram/instant messaging, or a local proxy. On ESP32 builds where Berry `webclient()` HTTPS fails, TasmoClaw can use the optional native HTTPS helper described below.

## Requirements

- Tasmota ESP32 build with Berry, webserver, filesystem, and TAPP support.
- Network and TLS support for `https://api.deepseek.com/chat/completions`.
- Recommended for this board: firmware built with `USE_TASMOCLAW_HTTPS`, PSRAM enabled, `USE_SDCARD`, and `USE_SHT3X`.
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

The config page also has a `Disable permission prompts` checkbox. When enabled, approval-gated tools run immediately. Keep it off for safer demos.

Default DeepSeek request settings are non-streaming:

- `model`: `deepseek-v4-flash`
- `temperature`: `0.2`
- `max_tokens`: `900`
- `stream`: `false`
- `thinking`: omitted by default

## Native HTTPS Bridge

On the ESP32-S3 RLCD build used for TasmoClaw, Berry `webclient()` HTTPS returned `-1` even for known-good HTTPS endpoints. TasmoClaw therefore prefers a small native helper named `idf_https_post(url, headers_json, body)` when it is available.

The helper is enabled in firmware with `USE_TASMOCLAW_HTTPS`. It exposes HTTPS POST to Berry and uses ESP-IDF/Tasmota-native TLS pieces rather than the failing Berry webclient path:

- `esp_http_client`
- `esp_tls`
- mbedTLS
- `esp_crt_bundle`

The `.tapp` still contains the TasmoClaw app. The native bridge is only the HTTPS transport helper. If `idf_https_post` is missing, TasmoClaw falls back to Berry `webclient()` and reports a clear rebuild hint if HTTPS still fails.

Diagnostic from the Berry console:

```berry
var headers = '{"Content-Type":"application/json","Accept":"application/json","Connection":"close","User-Agent":"TasmoClaw/0.1"}'
var body = '{"test":true}'
var r = idf_https_post("https://httpbin.org/post", headers, body)
print(r)
```

Diagnostic command:

```text
TasmoClawHttpsTest
```

Expected interpretation:

- HTTP 200, 400, 401, or 405 means HTTPS transport is working and the remote server answered.
- HTTP 401 from DeepSeek means the API key is missing or invalid.
- HTTP 400 from DeepSeek usually means payload or model mismatch.
- A transport error with `stage`, `esp_err`, or `error` means TLS, DNS, network, certificate bundle, or native bridge setup failed.

## PSRAM, SD Card, and SHTC3 on ESP32-S3-RLCD-4.2

The Waveshare ESP32-S3-RLCD-4.2 build used here has 16 MB flash and 8 MB PSRAM. Configure the PlatformIO board as an OPI PSRAM ESP32-S3 target and use the 16 MB flash / large filesystem partition. Runtime `Status 0` should show non-zero `PsrMax` and `PsrFree`; otherwise TasmoClaw can still run out of heap during HTTPS and JSON work.

The board SD slot is wired as 1-bit SDIO:

- `GPIO21`: SDIO CMD
- `GPIO38`: SDIO CLK
- `GPIO39`: SDIO D0

With `USE_SDCARD` and those template functions, `UfsType` should report `[1,3]`: SD card plus FlashFS. TasmoClaw exposes native SD helpers through the firmware bridge so SD files are accessible even when Berry `open()` is using the flash-backed filesystem.

The onboard temperature/humidity sensor is SHTC3 on I2C address `0x70`. Enable `USE_SHT3X`; `Status 8` should include `SHTC3` temperature, humidity, and dew point. TasmoClaw's `sensor_read` and `device_read` tools expose those readings.

Native SD/UFS diagnostic from Berry:

```berry
print(tasmo_ufs_list("/"))
print(tasmo_ufs_write("/memory.md", "# Memory\nTasmoClaw can write to SD.\n"))
print(tasmo_ufs_read("/memory.md", 2048))
```

## Example prompts

- Show me device status.
- Scan I2C and explain what you see.
- Read sensors and power.
- Run Status 0.
- Create a Hello World file in Berry in the file system.
- Run the Berry hello_world program.
- Explain the Berry hello_world program.
- Create memory.md on the SD card with the text: # Memory
- Read memory.md.
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

The Berry program helpers use:

- `berry_program_write`: writes a `.be` file, defaulting to `/tasmoclaw/berry/hello_world.be`.
- `berry_program_read`: reads the source.
- `berry_program_run`: loads and executes the source with `load(path)`.
- `berry_program_explain`: reads the source and returns either a concise DeepSeek explanation or a deterministic fallback explanation.

After running the generated hello-world file, test it from the Tasmota console with:

```text
HelloWorld
```

Expected response:

```json
{"HelloWorld":"ok"}
```

## SD Markdown Memory Files

TasmoClaw can write and read markdown files on the mounted SD card through the native UFS bridge. Suggested files include:

- `/memory.md`
- `/agent.md`
- `/soul.md`
- `/user.md`

Use prompts such as `Create memory.md on the SD card with the text ...` and `Read memory.md`.

## Known limitations

- No streaming, MCP, Telegram/IM, or local proxy.
- DeepSeek Chat Completions only.
- HTTPS support depends on the Tasmota build and device memory. Use `USE_TASMOCLAW_HTTPS` on this ESP32-S3 build.
- PSRAM must be enabled for comfortable HTTPS/JSON operation on this board.
- SD markdown access requires a mounted SD card and the SDIO template pins above.
- SHTC3 readings require `USE_SHT3X` and a working I2C bus.
- Native HTTPS response bodies are capped to keep memory use reasonable on-device.
- API key is stored on-device.
- Simple single-pending approval model.
- Browser UI is intentionally compact for Tasmota web pages.
- File storage falls back to `persist` only when direct filesystem access fails.

## Troubleshooting

- `Missing DeepSeek API key`: open `/tasmoclaw/config` and save a key.
- `webclient unavailable`: the firmware build may not include webclient support.
- `ESP-IDF HTTPS bridge idf_https_post is not available`: rebuild firmware with `USE_TASMOCLAW_HTTPS`.
- HTTP 401/403: check the API key.
- HTTP 404: check the API URL.
- HTTPS or request failures: run `TasmoClawHttpsTest` and inspect `stage`, `esp_err`, and the body preview.
- `malloc failed`: check `Status 0`; `PsrMax` should be `8192` on the Waveshare board and `PsrFree` should be non-zero.
- SD not visible: check `UfsType`, `Ufs`, and the SDIO template pins. `UfsType` should include `1`.
- No SHTC3 sensor: check `I2CScan` for `0x70` and confirm `USE_SHT3X` is enabled.
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
10. Ask TasmoClaw to create, read, run, and explain the Berry hello-world program; `HelloWorld` should return `{"HelloWorld":"ok"}` after it is run.
11. Ask TasmoClaw to create and read `memory.md` on the SD card.
12. Ask `Read sensors and power`; confirm `SHTC3`, `POWER1`, and `POWER2` appear.
13. Run `TasmoClaw`, `TasmoClawReset`, `TasmoClawTest`, and `TasmoClawHttpsTest` commands.

Recent live smoke results on the ESP32-S3-RLCD-4.2 test board:

- `PsrMax`: `8192`, `PsrFree`: non-zero.
- `UfsType`: `[1,3]` after SDIO template setup.
- `Status 8`: includes `SHTC3`.
- `TasmoClawHttpsTest`: DeepSeek replied `TasmoClaw online.`
- SD markdown write/read: `/memory.md` and `/agent.md` succeeded.
- Berry program write/read/run/explain: `/tasmoclaw/berry/hello_world.be` succeeded.

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
