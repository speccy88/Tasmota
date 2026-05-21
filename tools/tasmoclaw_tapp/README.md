# TasmoClaw TAPP

TasmoClaw is a mostly Berry/TAPP Tasmota Application packaged as one locally generated `.tapp` file. It adds a small web chat page to a Tasmota ESP32 device, talks to DeepSeek or local OpenAI-compatible chat servers, and exposes tools for device status, sensors, power, UFS/SD, FlashFS files, Berry programs/scripts, memory, scheduler rules, event routing, web search, HTTP bridge calls, image URL inspection, display/LVGL, audio, MQTT, timers, rules, and command building.

TasmoClaw does not use MCP, streaming, Telegram, or instant messaging. It does include an MCP-lite HTTP bridge tool: the device can call a LAN/cloud HTTP endpoint with GET or POST and feed the response back into the agent. The portable default HTTPS path is Tasmota Berry `webclient()` using BearSSL. On ESP32 builds where that path fails, TasmoClaw can use the optional native HTTPS helper described below.

## Requirements

- Tasmota ESP32 build with Berry, webserver, filesystem, and TAPP support.
- Network and TLS support for `https://api.deepseek.com/chat/completions`.
- Recommended for this board: PSRAM enabled, `USE_SDCARD`, and `USE_SHT3X`.
- Optional for this custom RLCD build only: `USE_TASMOCLAW_HTTPS` if Berry `webclient()` HTTPS still fails and you want the native ESP-IDF fallback.
- A DeepSeek API key.
- Optional: Brave Search API key for cloud web search.
- Optional: SearXNG on a LAN host for local/private web search.

## Build the TAPP

Build from this directory:

```bash
cd tools/tasmoclaw_tapp
python3 build_tapp.py
```

With no arguments this creates two ZIP_STORED/uncompressed TAPP files:

- `dist/tasmoclaw_lite.tapp`: regular ESP32-friendly build with the nice UI and selected safe tools.
- `dist/tasmoclaw.tapp`: full ESP32-S3/PSRAM/Waveshare feature build.

It also exports Tasmota-Extensions-ready raw folders:

- `dist/tasmota_extensions/raw/TasmoClaw_Lite/`
- `dist/tasmota_extensions/raw/TasmoClaw_Full/`

You can also build one target with `python3 build_tapp.py --target lite`, `--target full`, or explicitly build everything with `--target all`.
Use `--skip-extension-export` only when you want to rebuild the standalone `.tapp` files without regenerating the extension raw folders.

TasmoClaw keeps its debug call sites in both artifacts. They only emit logs when Tasmota debug logging is enabled with `WebLog 4` or `SerialLog 4`, and keeping one debug-capable artifact per target keeps the install choices simple.

Upstream-style PRs should normally avoid generated `.tapp` artifacts and attach ready-to-upload builds to GitHub Releases. This side-project branch intentionally keeps the generated `dist/*.tapp` files tracked so they can be downloaded directly from GitHub while the board support work is in progress.

## Tasmota-Extensions Packaging

TasmoClaw can be submitted to [Tasmota-Extensions](https://github.com/tasmota/Tasmota-Extensions) as two Extension Manager entries:

- `TasmoClaw Lite`: stock ESP32-friendly package for no-PSRAM devices.
- `TasmoClaw Full`: PSRAM-oriented package with the broad tool catalog, filesystem/SD helpers, Berry programming, and advanced workflows.

The extension exports use Tasmota's standard pattern: each raw folder contains `manifest.json`, an extension-style `autoexec.be`, and the Berry modules needed by that variant. The extension `autoexec.be` registers the returned driver object with `tasmota.add_extension(...)`, and the driver exposes `unload()` so Extension Manager can remove drivers, commands, and web routes cleanly.

For the full repeatable PR workflow, use [TASMOTA_EXTENSIONS_PR.md](TASMOTA_EXTENSIONS_PR.md). That file is the handoff playbook for future agents: it documents the source-of-truth files, `build_tapp.py`, `gen.py`, expected PR file layout, hardware smoke checks, commit/push commands, and PR creation command.

To prepare an upstream PR:

```bash
cd tools/tasmoclaw_tapp
python3 build_tapp.py

git clone https://github.com/tasmota/Tasmota-Extensions.git /tmp/Tasmota-Extensions
cp -R dist/tasmota_extensions/raw/TasmoClaw_Lite /tmp/Tasmota-Extensions/raw/
cp -R dist/tasmota_extensions/raw/TasmoClaw_Full /tmp/Tasmota-Extensions/raw/
cd /tmp/Tasmota-Extensions
python3 gen.py
```

The PR to `tasmota/Tasmota-Extensions` should add the two `raw/TasmoClaw_*` folders. That repository's `gen.py` creates `extensions/tapp/*.tapp` and `extensions/extensions.jsonl` from the raw folders.

### Standard ESP32 / no-PSRAM builds

Regular Tasmota32 builds on classic ESP32 modules without PSRAM have much less Berry heap than the Waveshare ESP32-S3 board. The full TasmoClaw package can fail at boot with:

```text
BRY: *** MEMORY ALLOCATION FAILED ***
failed to run compiled code (import_error - module 'tasmoclaw_ui' not found)
```

That failure happens while importing the `.tapp`, before any HTTPS request. For standard firmware from the Tasmota web installer, upload the Lite build instead:

- `tasmoclaw_lite.tapp`: the nicer TasmoClaw UI, compact chat, read tools, file read/list where standard filesystem access works, and approval-gated safe actions for power, `DisplayText`, and `I2SRtttl`.

The Full package has a defensive autoexec guard: if it is loaded on a board without PSRAM, it prints a message and returns before importing the heavy modules. This keeps Tasmota bootable so you can open the normal file manager and remove or replace the `.tapp`. It does not make Full usable on no-PSRAM ESP32 boards; use Lite there.

Load Lite with:

```text
Br load("/tasmoclaw_lite.tapp")
```

If the device already has a boot rule such as `Rule2 ON System#Boot DO Br load("/tasmoclaw.tapp") ENDON`, either change that rule to load `/tasmoclaw_lite.tapp`, or upload Lite under the filename `/tasmoclaw.tapp` so the existing boot rule loads the smaller app.

Lite is built from shared source modules in `src/`. It intentionally does not import the full app's large command catalog, native bridge helpers, SD helpers, or Berry programming tools.

Lite stores its configuration with Tasmota's documented Berry `persist` module (`persist.find`, `persist.setmember`, `persist.save(true)`) instead of ad-hoc JSON files. This keeps the small build compatible with stock ESP32 firmware where full file-backed storage may be too heavy or unavailable.

## Install

1. Upload the chosen artifact to Tasmota filesystem: `dist/tasmoclaw_lite.tapp` or `dist/tasmoclaw.tapp`.
2. Reboot device.
3. Open `http://<device-ip>/tasmoclaw`.

On this Waveshare board, the stock SD web upload endpoint can leave large binary `.tapp` files as zero-byte or partially zero-filled files. For repeatable hardware tests, upload the `.tapp` with Berry `open(..., "w"/"a")` in 256-byte chunks, then download it back through `/ufsd?fs=sd&download=/tasmoclaw.tapp` and compare SHA-256 before `Br load("/sd/tasmoclaw.tapp")`.

Direct URLs:

- Chat: `http://<device-ip>/tasmoclaw`
- Config: `http://<device-ip>/tasmoclaw/config`

## Configure DeepSeek Or A Local Model

Open `/tasmoclaw/config` and set:

- Provider:
  - `DeepSeek` for the hosted DeepSeek API.
  - `Local OpenAI-compatible` for MLX-LM, Ollama, llama.cpp server, or another local `/v1/chat/completions` endpoint.
- API URL: `https://api.deepseek.com/chat/completions`
- Model default: `deepseek-v4-flash`
- Model optional: `deepseek-v4-pro`
- API key: your DeepSeek key. Local servers usually do not need a key; leaving it blank clears the saved key for local mode.
- HTTPS transport:
  - `webclient / BearSSL`: default and portable for normal Tasmota builds.
  - `auto`: try BearSSL first, then the optional native helper only if no HTTP response is received.
  - `native ESP-IDF bridge`: force the optional mbedTLS helper.
- Temperature, max tokens, thinking mode, reasoning effort, tool iteration limit, history limit, prompt mode, context byte limit, and optional extra system instructions.

TasmoClaw defaults to `prompt mode: compact` and a bounded context byte limit so normal ESP32 builds can use the portable BearSSL `webclient()` path. Use `prompt mode: full` only on boards with enough free heap/PSRAM, or when debugging tool-selection behavior.

The config page loads `/tasmoclaw/api/config`, saves with `POST /tasmoclaw/api/config`, and tests the current API settings with `POST /tasmoclaw/api/test`. The API key is masked as `********` when read back. Saving an empty key or `********` preserves the existing key; entering a new non-empty value replaces it.

When `Test API` succeeds, TasmoClaw records the current provider, API URL, model, and transport as a known-good model profile. The main chat page then shows those tested profiles in a small model switcher, so you can jump between DeepSeek and local OpenAI-compatible servers without reopening the config page. Untested models are not listed there.

For a local OpenAI-compatible server, use the server's LAN-reachable URL. From a Mac running MLX-LM on the same network, an example is:

```text
Provider: Local OpenAI-compatible
API URL: http://<mac-lan-ip>:8080/v1/chat/completions
Model: <local model id>
API key: blank unless your local server requires one
```

For Ollama's OpenAI-compatible API, the URL is usually:

```text
http://<mac-lan-ip>:11434/v1/chat/completions
```

TasmoClaw does not hard-code local model names. Put whatever model/deployment name your local server expects in the Model field.

## Search: Brave Cloud Or Local SearXNG

TasmoClaw has one `web_search` tool with two providers:

- `brave`: Brave Search API. Configure `Search provider = brave` and set the Brave Search API key.
- `searxng`: local/LAN SearXNG JSON endpoint. Configure `Search provider = searxng` and set `SearXNG URL`, for example `http://<mac-lan-ip>:8888/search`.

Stock Tasmota's Berry `webclient()` may fail against some HTTPS APIs even when other HTTPS endpoints work. If direct Brave HTTPS returns a negative webclient status, run a small LAN bridge and set `Brave proxy URL` to the bridge endpoint. In proxy mode, keep the Brave API key on the host proxy; TasmoClaw talks to the bridge over LAN HTTP without adding custom auth headers. For plain HTTP LAN URLs, TasmoClaw uses a low-level `tcpclient` GET path so the search proxies and `http_bridge_call` still work when the full app leaves too little heap for `webclient()`.

```text
Search provider: brave
Brave Search API key: blank when the proxy holds the key
Brave proxy URL: http://<mac-lan-ip>:8767/res/v1/web/search
```

The included helper can run that bridge without storing the key in the repo:

```bash
BRAVE_SEARCH_API_KEY=<key> \
python3 tools/tasmoclaw_tapp/brave_search_proxy.py --host 0.0.0.0 --port 8767
```

If the host Python certificate store cannot verify Brave's TLS chain, add `--no-verify-tls` or fix the host CA store. Do not commit API keys.

For the most reliable stock-firmware path, run the combined compact proxy on a LAN port the ESP32 can already reach:

```bash
BRAVE_SEARCH_API_KEY=<key> \
python3 tools/tasmoclaw_tapp/tasmoclaw_search_proxy.py \
  --host 0.0.0.0 \
  --port 8766 \
  --searxng-upstream http://127.0.0.1:8888/search \
  --no-verify-tls
```

Then configure:

```text
Brave proxy URL: http://<mac-lan-ip>:8766/brave
SearXNG URL: http://<mac-lan-ip>:8766/searxng
```

The local SearXNG path was tested with SearXNG installed from source in a Python virtualenv, with JSON enabled in `settings.yml`:

```bash
git clone --depth 1 https://github.com/searxng/searxng.git /tmp/searxng
python3 -m venv /tmp/searxng/.venv
/tmp/searxng/.venv/bin/pip install -U pip wheel setuptools
/tmp/searxng/.venv/bin/pip install -r /tmp/searxng/requirements.txt -r /tmp/searxng/requirements-server.txt
/tmp/searxng/.venv/bin/pip install --no-build-isolation -e /tmp/searxng
```

Use a settings copy with `server.bind_address: "0.0.0.0"` and `search.formats` containing `json`, then run:

```bash
SEARXNG_SETTINGS_PATH=/tmp/searxng/settings-local.yml \
SEARXNG_SECRET=<local-secret> \
/tmp/searxng/.venv/bin/python -m searx.webapp
```

From the board, use the LAN IP, not `127.0.0.1`, because `127.0.0.1` would be the ESP32 itself.

If stock `webclient()` struggles with the full SearXNG JSON response and you do not want the combined proxy, run the included compact bridge and point `SearXNG URL` at it:

```bash
python3 tools/tasmoclaw_tapp/searxng_compact_proxy.py \
  --host 0.0.0.0 \
  --port 8768 \
  --upstream http://127.0.0.1:8888/search
```

```text
SearXNG URL: http://<mac-lan-ip>:8768/search
```

## Stock Firmware UFS/SD Limits

TasmoClaw stays within stock Tasmota firmware APIs. For SD cards, stock UFS commands support status, listing, delete, rename, and run-style operations, and the web file manager can upload/download content from a browser or host. Stock Berry does not expose reliable SD file-content read/write/copy/move APIs. TasmoClaw therefore:

- supports FlashFS text read/write/copy/move/delete through Berry where available;
- supports stock UFS status/list/delete/rename/run commands for `sd:` and `flash:` paths;
- returns an explicit stock-firmware limitation when a tool asks Berry to copy/read/write SD file content;
- reports the matching web endpoints, such as `GET /ufsd?fs=sd&download=/file.txt` and `POST /ufse`, for host/browser SD content work.

For this board, the tested SD SPI pins were GPIO21 MOSI, GPIO38 SCK, and GPIO39 MISO. Chip-select must still match the board/template wiring.

## Full Tool Catalog

Full TasmoClaw currently includes these ESP-Claw-inspired capabilities:

- skills: list, activate, deactivate, and reset capability groups;
- memory: local FlashFS memory read/search/write/append/forget;
- scheduler: one-shot and interval schedules with manual trigger and periodic `every_second()` tick;
- router: event rules that call tools, run commands, append memory, display text, or emit nested events;
- web search: Brave cloud or SearXNG local/LAN;
- HTTP bridge: GET/POST calls to local/cloud services;
- image inspection: OpenAI-compatible `image_url` vision endpoint, with clear stock-firmware limits for SD/base64 file uploads;
- files/scripts: FlashFS file copy/move/delete plus reusable Berry script create/read/list/run.

The config page also has a `Disable permission prompts` checkbox. When enabled, approval-gated tools run immediately. Keep it off for safer demos.

Default DeepSeek request settings are non-streaming:

- `model`: `deepseek-v4-flash`
- `temperature`: `0.2`
- `max_tokens`: `900`
- `stream`: `false`
- `thinking`: omitted by default

## HTTPS Transport

## Local MLX-LM Example

On a Mac, MLX-LM can expose an OpenAI-compatible endpoint that TasmoClaw can call over plain HTTP:

```bash
python3 -m venv ~/mlx-agent
source ~/mlx-agent/bin/activate
pip install -U mlx-lm openai
mlx_lm.server \
  --model mlx-community/Qwen3-8B-4bit \
  --host 0.0.0.0 \
  --port 8080 \
  --chat-template-args '{"enable_thinking":false}'
```

The `enable_thinking:false` template argument is useful for Qwen3-style models because it makes the OpenAI response return normal `message.content` instead of only reasoning text.

Quick test from the Mac:

```bash
curl http://127.0.0.1:8080/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -d '{"model":"mlx-community/Qwen3-8B-4bit","messages":[{"role":"user","content":"Reply exactly: MLX local ok."}],"max_tokens":80,"stream":false}'
```

Then configure TasmoClaw with `http://<mac-lan-ip>:8080/v1/chat/completions`. Use the same idea for Ollama with `http://<mac-lan-ip>:11434/v1/chat/completions`.

### Qwen3 8B MLX-LM tool-calling notes

Qwen3 8B 4-bit through MLX-LM is usable for some TasmoClaw demos, but it is not as reliable as DeepSeek for the custom text-based tool protocol yet.

Observed working prompts on the test board:

- `Read the current device status and answer with heap and wifi IP.` called `device_read` and returned Wi-Fi, heap, ADC, SHTC3, power, UFS, and SD state.
- `What are all current sensor values?` called `sensor_read` and returned ADC, SHTC3 temperature/humidity, and I2C scan.
- `Read the current power state.` called `power_read` and returned `POWER1=ON, POWER2=ON`.
- `List the current Tasmota rules.` called `rule_control` and returned Rule1/Rule2/Rule3.
- `List the files on flash.` called `file_list` and returned FlashFS files.
- `Read all sensor values, then read power state...` worked by choosing the aggregate `device_read` tool.

Observed failures and cautions:

- One explicit `Status 0` command-read test hit `HTTP -11` read timeout through the local MLX-LM/webclient path.
- `Read flash:/display.ini...` was misclassified as `display_control` and sent the prompt text to `DisplayText` instead of using `file_read`.
- Qwen may ignore exact-output instructions or select surprising tools. Keep approval prompts enabled for actions when testing local models.

Recommendation: use Qwen/MLX-LM for read-only status, sensor, power, rules, and file-list demos. Use DeepSeek for broader tool use, file reads/writes, Berry programming, rule changes, display/audio actions, or any unattended run until TasmoClaw has a stricter local tool-router/validator layer.

TasmoClaw defaults to the standard Tasmota Berry `webclient()` HTTPS path. This is the route that should make the `.tapp` usable by regular Tasmota users without firmware changes.

The quick BearSSL diagnostic from the Berry console is:

```berry
var cl = webclient()
cl.begin("https://api.deepseek.com/chat/completions")
var r = cl.GET()
print("r="+str(r))
var s = cl.get_string()
print("s="+s)
```

Expected interpretation:

- `r=401` from DeepSeek means TLS worked and DeepSeek rejected the missing authorization header.
- `r=200` from `https://trmnl.com/api/display` with a JSON error body also means TLS worked.
- A negative code or no body means the failure happened before a real HTTP response, usually in DNS, Wi-Fi, timeout, heap, TLS, certificate, cipher, or the webclient build.

On the ESP32-S3 RLCD build used for TasmoClaw, Berry `webclient()` HTTPS failed before receiving a real HTTP response, while the same URLs worked through a native ESP-IDF client. TasmoClaw keeps that native helper separate and optional so the main `.tapp` can remain portable.

The optional helper is named `idf_https_post(url, headers_json, body)`.

The helper is enabled in firmware with `USE_TASMOCLAW_HTTPS`; this checkout keeps it in a separate opt-in PlatformIO environment named `tasmota32s3-lvgl-tasmoclaw-native`. It exposes HTTPS POST to Berry and uses ESP-IDF/Tasmota-native TLS pieces as a fallback for builds where the Berry webclient path fails:

- `esp_http_client`
- `esp_tls`
- mbedTLS
- `esp_crt_bundle`

The `.tapp` still contains the TasmoClaw app. The native bridge is only a fallback HTTPS transport helper. It is not required for normal Tasmota builds. In config, leave `HTTPS transport` at `webclient / BearSSL` unless your firmware shows the failing negative-code behavior; use `auto` or `native` only for those builds.

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

- HTTP 200, 400, 401, or 405 from either transport means HTTPS transport is working and the remote server answered.
- HTTP 401 from DeepSeek means the API key is missing or invalid.
- HTTP 400 from DeepSeek usually means payload or model mismatch.
- A transport error with `stage`, `esp_err`, or `error` means TLS, DNS, network, certificate bundle, or native bridge setup failed.

## PSRAM, SD Card, and SHTC3 on ESP32-S3-RLCD-4.2

The Waveshare ESP32-S3-RLCD-4.2 build used here has 16 MB flash and 8 MB PSRAM. Configure the PlatformIO board as an OPI PSRAM ESP32-S3 target and use the 16 MB flash / large filesystem partition. Runtime `Status 0` should show non-zero `PsrMax` and `PsrFree`; otherwise TasmoClaw can still run out of heap during HTTPS and JSON work.

The board SD slot is wired as 1-bit SDIO:

- `GPIO21`: SDIO CMD
- `GPIO38`: SDIO CLK
- `GPIO39`: SDIO D0

With `USE_SDCARD` and those template functions, `UfsType` should report `[1,3]`: SD card plus FlashFS. On upstream firmware, TasmoClaw first uses standard Tasmota UFS commands such as `UfsList` plus Berry `open()` for explicit `flash:/...` and `sd:/...` paths. Optional native helpers named `tasmo_ufs_list`, `tasmo_ufs_read`, and `tasmo_ufs_write` are used only when the firmware provides them; they are not required for normal upstream compatibility.

Filesystem paths can be made explicit with prefixes:

- `sd:/memory.md` reads or writes the SD card.
- `flash:/autoexec.be` reads or writes internal FlashFS.
- `/file.txt` keeps Tasmota's default UFS behavior, which is SD when SD is mounted.

The Tasmota Manage File System page has a FlashFS/SDCard selector and Copy/Move buttons for regular files when both filesystems are mounted. Audio playback uses the same explicit syntax, for example `I2SPlay sd:/music/file.mp3` or `I2SPlay flash:/startup.wav`.

The onboard temperature/humidity sensor is SHTC3 on I2C address `0x70`. Enable `USE_SHT3X`; `Status 8` should include `SHTC3` temperature, humidity, and dew point. TasmoClaw's `sensor_read` and `device_read` tools expose those readings.

SD/UFS diagnostic from Berry when native helpers are available:

```berry
print(tasmo_ufs_list("sd:/"))
print(tasmo_ufs_write("sd:/memory.md", "# Memory\nTasmoClaw can write to SD.\n"))
print(tasmo_ufs_read("sd:/memory.md", 2048))
print(tasmo_ufs_list("flash:/"))
```

## Example prompts

- Show me device status.
- Scan I2C and explain what you see.
- Read sensors and power.
- Run Status 0.
- Play a happy birthday RTTTL song.
- Say hello from TasmoClaw.
- Set speaker volume to 20.
- Stop the audio.
- Play `sd:/music/test.mp3`.
- Show hello on the display.
- Toggle power 2.
- Create a Hello World file in Berry in the file system.
- Run the Berry hello_world program.
- Explain the Berry hello_world program.
- Create memory.md on the SD card with the text: # Memory
- Read memory.md.
- Create a Tasmota Rule1 that refreshes the display every 5 minutes.
- Write a Berry file that displays heap, RSSI, and time on the screen.

Read-only tools such as status and whitelisted commands can run immediately. Write tools and unsafe Tasmota commands create a pending approval shown on the TasmoClaw page.

## Command Coverage

TasmoClaw includes a small command catalog for common Tasmota families instead of hard-coding only exact prompt phrases. The prompt can search the catalog, build a command, safety-classify it, and then execute it through structured tools.

Supported first-class command families include:

- Status, Wi-Fi, memory, GPIO, template/module, I2C, sensors, ADC/analog, and UFS/SD reads.
- Power relay reads and `PowerN 0/1/2` off/on/toggle actions.
- Rules reads plus rule enable, disable, clear, and apply actions.
- Display text through `DisplayText`.
- I2S audio commands: `I2SRtttl`, `I2SPlay`, `I2SLoop`, `I2SPause`, `I2SStop`, `I2SGain`, `I2SSay`, `I2SBeep`, `I2SCodec`, `I2STime`, and `I2SRec`.
- Light/dimmer/color commands, MQTT publish/config, telemetry/log levels, network/time settings, events, Backlog, module/template, PulseTime, and RuleTimer commands through the generic command builder.
- Multi-command prompts through `command_sequence_run`, so TasmoClaw can execute steps such as “read sensors, read power, then toggle POWER2” in order.

Safety classes:

- `read`: can run without approval.
- `action` or `write`: asks permission unless `Disable permission prompts` is enabled.
- `dangerous`: high-impact commands such as restart/reset/upgrade are refused by the generic runner unless explicitly confirmed.

## Working Tool Reference

TasmoClaw exposes these tools to DeepSeek. Read-only tools can run immediately; write/action tools ask for approval unless `Disable permission prompts` is enabled.

| Tool | Purpose | Example prompt |
| --- | --- | --- |
| `tasmota_status` | Read memory, Wi-Fi, architecture, sensors, and `Status 0`. | `Show me device status.` |
| `tasmota_cmd_read` | Run arbitrary clearly read-only Tasmota commands. `Rules` reads Rule1/2/3. | `Run Status 0.` / `Show all rules.` |
| `device_read` | Read sensors, power, SD/UFS, heap, and Wi-Fi together. | `Read current sensors and power states.` |
| `sensor_read` | Read `I2CScan`, `Status 8`, ADC/analog, SHTC3, and sensor JSON. | `What is the temperature and ADC value?` |
| `power_read` | Read `POWER`, `POWER1`, `POWER2`, and power status. | `Get the power value.` |
| `power_control` | Read, turn on/off, or toggle relays with dynamic approval. | `Toggle power 2.` |
| `rule_control` | Read, enable, disable, clear, or set rules. | `Disable Rule3.` |
| `rule_apply` | Apply a rule definition and optionally enable/start a timer. | `Create Rule3 that prints hello every 5 seconds.` |
| `rule_clear` | Disable and clear a rule slot. | `Remove the hello timer rule.` |
| `light_control` | Control lights, dimmer, color, CT, white, scheme, fade, and speed. | `Set dimmer to 35.` |
| `display_control` / `display_message` | Send `DisplayText`. | `Show hello on the screen.` |
| `audio_rtttl_play` | Play a complete RTTTL string or known preset with `I2SRtttl`. | `Compose and play a short original RTTTL tune.` |
| `audio_file_play` | Play or loop an audio file/URL with `I2SPlay`/`I2SLoop`. | `Play sd:/music/test.mp3.` |
| `audio_say` | Speak text with `I2SSay` when supported. | `Say hello from TasmoClaw.` |
| `audio_control` | Stop, pause, resume, beep, set gain, read codec/time, or record. | `Set speaker volume to 40.` |
| `mqtt_control` | Read MQTT config or publish with `Publish`/`Publish2`. | `Publish hello to stat/tasmoclaw/test.` |
| `telemetry_control` | Read/set `TelePeriod`, `WebLog`, `SerialLog`, `SysLog`, or `Status`. | `Set TelePeriod to 300.` |
| `network_control` | Read/change Wi-Fi, hostname, IP, NTP, timezone. | `Show hostname and Wi-Fi state.` |
| `system_control` | Run `State`, `Status`, `Event`, `Backlog`, module/template, or confirmed restart. | `Trigger event hello=1.` |
| `timer_control` | Read/set `Timers`, `TimerN`, `RuleTimerN`, and `PulseTimeN`. | `Start RuleTimer1 for 5 seconds.` |
| `filesystem_control` | Run stock UFS status/list/delete/rename/run commands. | `List sd:/ with UfsList.` |
| `file_list` | List files with `sd:`/`flash:` prefixes. | `List sd:/ files.` |
| `file_read` | Read text files up to 16 KB from FlashFS. Stock Berry cannot read SD file contents. | `Read flash:/hello_world.txt.` |
| `file_write` | Write exact text content to FlashFS. Stock Berry cannot write SD file contents. | `Write hello world to flash:/hello_world.txt.` |
| `ufs_info` | Read UFS type, size, free space, and FlashFS/SD root listings. | `Is the SD card mounted?` |
| `sd_markdown_list` | List SD-card markdown/memory files. | `List SD card content.` |
| `berry_program_write` | Write a Berry `.be` program file. | `Create a Hello World Berry file.` |
| `berry_program_read` | Read a Berry program. | `Read hello_world.be.` |
| `berry_program_run` / `berry_load` | Load and run a Berry file. | `Run the hello_world Berry program.` |
| `berry_program_explain` | Read Berry source so TasmoClaw can explain it. | `Explain hello_world.be.` |
| `berry_compile` | Compile a Berry file with `tasmota.compile(path)`. | `Compile /tasmoclaw/berry/hello_world.be.` |
| `berry_check` | Report Berry syntax-check availability. | `Can you syntax-check Berry?` |
| `berry_skill_template` | Generate a reusable Berry command/skill template without writing it. | `Show me a Berry skill template for EchoSkill.` |
| `berry_skill_create` | Create a reusable Berry skill file that registers a Tasmota command. | `Create a Berry skill called EchoSkill that echoes payload.` |
| `berry_skill_run` | Load a Berry skill so its command becomes available. | `Load the EchoSkill Berry skill.` |
| `berry_skill_explain` | Read and explain a Berry skill file. | `Explain the EchoSkill skill.` |
| `create_demo_berry` | Create the demo `AIStatus` Berry command file. | `Create the demo Berry status command.` |
| `command_catalog_search` | Search known Tasmota command families. | `What tool should I use for MQTT?` |
| `command_build` | Build and classify a command without executing it. | `Build a command to set WebLog to 2.` |
| `command_run` | Execute a built/raw Tasmota command with safety checks. | `Run Event hello=1.` |
| `command_sequence_run` | Execute multiple built Tasmota command steps in order. | `Read sensors, read power, then toggle POWER2.` |
| `tasmota_cmd` | Last-resort approved raw Tasmota command execution. | `Run this exact command: Backlog ...` |

### Berry Skill Workflow

Use Berry skills when you want TasmoClaw to create reusable on-device behavior instead of only running one command. A skill is just a Berry file in the TasmoClaw workspace that registers a Tasmota console command with `tasmota.add_cmd`.

Example prompts:

```text
Create a Berry skill called EchoSkill with command EchoSkill that echoes its payload.
Load the EchoSkill Berry skill.
Run EchoSkill hello from the Tasmota console.
Explain the EchoSkill skill.
```

Default generated skills are written under `/tasmoclaw/berry/<name>.be` when the workspace is available, or `/tasmoclaw_demo_<name>.be` when the fallback workspace is active.

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

Full TasmoClaw prefers real filesystem JSON files:

- `/tasmoclaw_config.json`
- `/tasmoclaw_history.json`
- `/tasmoclaw_pending.json`

Full TasmoClaw also mirrors those values through Tasmota Berry `persist` as a fallback, using keys such as `tasmoclaw_config_json`, `tasmoclaw_history_json`, and `tasmoclaw_pending_json`. Lite uses `persist` directly with the `tasmoclaw_lite_config_json` key. If file storage fails, the store attempts the `persist` value and returns/logs the storage error where possible.

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
- HTTPS defaults to Berry `webclient()`/BearSSL. Use `auto` or `native` only on builds where BearSSL fails before getting an HTTP response.
- PSRAM must be enabled for comfortable HTTPS/JSON operation on this board.
- SD markdown access requires a mounted SD card and the SDIO template pins above.
- SHTC3 readings require `USE_SHT3X` and a working I2C bus.
- HTTPS response bodies are capped to keep memory use reasonable on-device.
- API key is stored on-device.
- Simple single-pending approval model.
- Browser UI is intentionally compact for Tasmota web pages.
- File storage falls back to `persist` only when direct filesystem access fails.

## Troubleshooting

- `Missing DeepSeek API key`: open `/tasmoclaw/config` and save a key.
- `webclient unavailable`: the firmware build may not include webclient support.
- `ESP-IDF HTTPS bridge idf_https_post is not available`: native transport was selected without firmware support; use `webclient / BearSSL` or rebuild firmware with `USE_TASMOCLAW_HTTPS`.
- HTTP 401/403: check the API key.
- HTTP 404: check the API URL.
- HTTPS or request failures: run `TasmoClawHttpsTest`. If BearSSL returns HTTP 401 from DeepSeek or HTTP 200 from TRMNL, the portable transport works. If BearSSL returns a negative code but native succeeds, use `auto`/`native` only for that custom build.
- Detailed debug logs: run `WebLog 4` for web console logs or `SerialLog 4` for serial logs, then reproduce the issue. TasmoClaw emits `TCL:` debug lines for chat loop steps, tool selection, storage fallbacks, webclient/native HTTPS start/result/failure, HTTP status, body byte counts, retry attempts, and native TLS stages. API keys and Authorization headers are not logged.
- `HTTP -8 from Tasmota webclient before receiving a server response (too little RAM)`: BearSSL can usually still work; the request is too large for available heap. Keep `prompt mode` on `compact`, lower `context byte limit` to around `3500`, reduce `history limit`, clear chat history, and retry. `TCL: llm call start ... payload_bytes=...` in `WebLog 4` shows the request size.
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
13. Ask `Play a happy birthday RTTTL song`; confirm approval appears, then approve and listen for `I2SRtttl`.
14. Ask `Set speaker volume to 20`; confirm it maps to `I2SGain 20`.
15. Run `TasmoClaw`, `TasmoClawReset`, `TasmoClawTest`, and `TasmoClawHttpsTest` commands.

Recent live smoke results on the ESP32-S3-RLCD-4.2 test board:

- `PsrMax`: `8192`, `PsrFree`: non-zero.
- `UfsType`: `[1,3]` after SDIO template setup.
- `Status 8`: includes `SHTC3`.
- `TasmoClawHttpsTest`: native transport reached DeepSeek; BearSSL webclient still failed on this custom RLCD firmware at the time of testing.
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
unzip -l dist/tasmoclaw_lite.tapp
find dist/tasmota_extensions/raw -maxdepth 2 -type f | sort
```

The full archive should contain only:

- `autoexec.be`
- `tasmoclaw.be`
- `tasmoclaw_ui.be`
- `tasmoclaw_tools.be`
- `tasmoclaw_llm.be`
- `tasmoclaw_store.be`
- `tasmoclaw_util.be`
- `tasmoclaw_prompt.be`
- `tasmoclaw_commands.be`

The Lite archive should contain `autoexec.be`, `tasmoclaw_lite.be`, `tasmoclaw_common.be`, and `tasmoclaw_ui.be`.

The extension raw folders should each contain `manifest.json`, extension-style `autoexec.be`, and the same Berry modules as the matching standalone variant. In a Tasmota-Extensions checkout, `python3 gen.py` should build `TasmoClaw_Lite.tapp` and `TasmoClaw_Full.tapp` without compression.
