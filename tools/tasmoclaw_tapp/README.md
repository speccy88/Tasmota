# TasmoClaw TAPP

TasmoClaw is a mostly Berry/TAPP Tasmota Application packaged as one locally generated `.tapp` file. It adds a small web chat page to a Tasmota ESP32 device, talks directly to the DeepSeek Chat Completions API, and exposes a demo-oriented set of tools for reading status, reading sensors and power, writing files, writing/reading/running/explaining Berry programs, writing SD-card markdown memory files, applying rules, and creating a simple Berry command file.

TasmoClaw does not use MCP, streaming, Telegram/instant messaging, or a local proxy. The portable default HTTPS path is Tasmota Berry `webclient()` using BearSSL. On ESP32 builds where that path fails, TasmoClaw can use the optional native HTTPS helper described below.

## Requirements

- Tasmota ESP32 build with Berry, webserver, filesystem, and TAPP support.
- Network and TLS support for `https://api.deepseek.com/chat/completions`.
- Recommended for this board: PSRAM enabled, `USE_SDCARD`, and `USE_SHT3X`.
- Optional for this custom RLCD build only: `USE_TASMOCLAW_HTTPS` if Berry `webclient()` HTTPS still fails and you want the native ESP-IDF fallback.
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
- HTTPS transport:
  - `webclient / BearSSL`: default and portable for normal Tasmota builds.
  - `auto`: try BearSSL first, then the optional native helper only if no HTTP response is received.
  - `native ESP-IDF bridge`: force the optional mbedTLS helper.
- Temperature, max tokens, thinking mode, reasoning effort, tool iteration limit, history limit, and optional extra system instructions.

The config page loads `/tasmoclaw/api/config`, saves with `POST /tasmoclaw/api/config`, and tests the current API settings with `POST /tasmoclaw/api/test`. The API key is masked as `********` when read back. Saving an empty key or `********` preserves the existing key; entering a new non-empty value replaces it.

The config page also has a `Disable permission prompts` checkbox. When enabled, approval-gated tools run immediately. Keep it off for safer demos.

Default DeepSeek request settings are non-streaming:

- `model`: `deepseek-v4-flash`
- `temperature`: `0.2`
- `max_tokens`: `900`
- `stream`: `false`
- `thinking`: omitted by default

## HTTPS Transport

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

With `USE_SDCARD` and those template functions, `UfsType` should report `[1,3]`: SD card plus FlashFS. TasmoClaw exposes native SD helpers through the firmware bridge so SD files are accessible even when Berry `open()` is using the flash-backed filesystem.

Filesystem paths can be made explicit with prefixes:

- `sd:/memory.md` reads or writes the SD card.
- `flash:/autoexec.be` reads or writes internal FlashFS.
- `/file.txt` keeps Tasmota's default UFS behavior, which is SD when SD is mounted.

The Tasmota Manage File System page has a FlashFS/SDCard selector and Copy/Move buttons for regular files when both filesystems are mounted. Audio playback uses the same explicit syntax, for example `I2SPlay sd:/music/file.mp3` or `I2SPlay flash:/startup.wav`.

The onboard temperature/humidity sensor is SHTC3 on I2C address `0x70`. Enable `USE_SHT3X`; `Status 8` should include `SHTC3` temperature, humidity, and dew point. TasmoClaw's `sensor_read` and `device_read` tools expose those readings.

Native SD/UFS diagnostic from Berry:

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
| `filesystem_control` | Run UFS status/list/mkdir/delete/rename/run commands. | `List sd:/ with UfsList.` |
| `file_list` | List files with `sd:`/`flash:` prefixes. | `List sd:/ files.` |
| `file_read` | Read text files up to 16 KB from SD or FlashFS. | `Read sd:/hello_world.txt.` |
| `file_write` | Write exact text content to SD or FlashFS. | `Write hello world to sd:/hello_world.txt.` |
| `ufs_info` | Read UFS type, size, free space, and FlashFS/SD root listings. | `Is the SD card mounted?` |
| `sd_markdown_list` | List SD-card markdown/memory files. | `List SD card content.` |
| `sd_markdown_read` | Read a named SD markdown/text file. | `Read memory.md.` |
| `sd_markdown_write` | Write SD markdown/text memory files. | `Create agent.md on the SD card.` |
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
- `tasmoclaw_commands.be`
