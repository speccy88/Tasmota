# TasmoClaw Tasmota-Extensions PR Playbook

This file is the handoff guide for updating TasmoClaw and preparing a pull request to
[`tasmota/Tasmota-Extensions`](https://github.com/tasmota/Tasmota-Extensions).

Use it when TasmoClaw changes in this Tasmota checkout need to be exported as
Extension Manager packages.

## Source Of Truth

The source of truth is this repository:

```text
/Users/fred/Documents/Code/Tasmota/tools/tasmoclaw_tapp/
```

Edit only the shared source files here:

```text
tools/tasmoclaw_tapp/src/
tools/tasmoclaw_tapp/build_tapp.py
tools/tasmoclaw_tapp/README.md
```

Do not edit copied files directly inside a `Tasmota-Extensions` checkout. Those
files are generated/exported copies and should be replaced by rerunning the
TasmoClaw builder.

TasmoClaw currently exports two Extension Manager variants:

- `TasmoClaw Lite`: stock ESP32-friendly package.
- `TasmoClaw Full`: PSRAM-oriented package.

The extension metadata lives in `build_tapp.py`:

```python
EXT_VERSION = '0x1A051500'
EXT_MIN_TASMOTA = '0x0F040000'
EXT_AUTHOR = 'Frederick Blais'
EXTENSIONS = { ... }
```

When updating the extension version, update `EXT_VERSION` in `build_tapp.py`.
The current format is a hex-encoded date-like project version used by the
Extension Manager manifest.

## Build TasmoClaw Locally

From the Tasmota checkout:

```bash
cd /Users/fred/Documents/Code/Tasmota
python3 tools/tasmoclaw_tapp/build_tapp.py
```

This produces standalone local artifacts:

```text
tools/tasmoclaw_tapp/dist/tasmoclaw_lite.tapp
tools/tasmoclaw_tapp/dist/tasmoclaw.tapp
```

It also exports raw folders for `Tasmota-Extensions`:

```text
tools/tasmoclaw_tapp/dist/tasmota_extensions/raw/TasmoClaw_Lite/
tools/tasmoclaw_tapp/dist/tasmota_extensions/raw/TasmoClaw_Full/
```

Each raw extension folder contains:

- `manifest.json`
- extension-style `autoexec.be`
- the Berry modules needed by that variant

The extension `autoexec.be` must register an extension object with
`tasmota.add_extension(...)`. The object must expose `unload()` so Extension
Manager can remove drivers, commands, web routes, and other resources.

## Local TasmoClaw Checks

Run these before copying into `Tasmota-Extensions`:

```bash
cd /Users/fred/Documents/Code/Tasmota
python3 tools/tasmoclaw_tapp/build_tapp.py
python3 -m py_compile tools/tasmoclaw_tapp/build_tapp.py
git diff --check
```

Confirm the local standalone archives are uncompressed ZIP_STORED packages:

```bash
python3 - <<'PY'
from pathlib import Path
from zipfile import ZipFile, ZIP_STORED

for p in [
    Path("tools/tasmoclaw_tapp/dist/tasmoclaw_lite.tapp"),
    Path("tools/tasmoclaw_tapp/dist/tasmoclaw.tapp"),
]:
    with ZipFile(p) as z:
        stored = all(i.compress_type == ZIP_STORED for i in z.infolist())
        print(p, p.stat().st_size, "ZIP_STORED=", stored)
        for i in z.infolist():
            print(" ", i.filename, i.file_size, i.compress_type)
PY
```

Optional secret scan before export:

```bash
rg -n 'sk-[A-Za-z0-9]|api_key":\s*"sk-|Bearer sk-' \
  tools/tasmoclaw_tapp/src \
  tools/tasmoclaw_tapp/dist/tasmota_extensions/raw || true
```

The code may contain `Authorization` header construction, but it must not
contain a real API key.

## Prepare A Tasmota-Extensions Checkout

Use a separate checkout so the main Tasmota working tree stays independent:

```bash
cd /Users/fred/Documents/Code
gh repo fork tasmota/Tasmota-Extensions --clone=false --remote=false
rm -rf Tasmota-Extensions-TasmoClaw
git clone https://github.com/tasmota/Tasmota-Extensions.git Tasmota-Extensions-TasmoClaw
cd Tasmota-Extensions-TasmoClaw
git remote add fork https://github.com/speccy88/Tasmota-Extensions.git
git checkout -b codex/add-tasmoclaw-extension
```

If the fork already exists, `gh repo fork` may simply print the fork URL. That
is fine.

For later update PRs, use a more specific branch name, for example:

```bash
git checkout -b codex/update-tasmoclaw-extension
```

## Copy Raw Folders

From the `Tasmota-Extensions-TasmoClaw` checkout:

```bash
rm -rf raw/TasmoClaw_Lite raw/TasmoClaw_Full
cp -R /Users/fred/Documents/Code/Tasmota/tools/tasmoclaw_tapp/dist/tasmota_extensions/raw/TasmoClaw_Lite raw/
cp -R /Users/fred/Documents/Code/Tasmota/tools/tasmoclaw_tapp/dist/tasmota_extensions/raw/TasmoClaw_Full raw/
```

Do not copy the standalone `dist/tasmoclaw*.tapp` files from the Tasmota repo
into `Tasmota-Extensions`. The extension repository generates its own `.tapp`
files from `raw/`.

## Run gen.py

From the `Tasmota-Extensions-TasmoClaw` checkout:

```bash
python3 gen.py
```

`gen.py` reads all folders under `raw/`, then writes:

```text
extensions/extensions.jsonl
extensions/tapp/TasmoClaw_Lite.tapp
extensions/tapp/TasmoClaw_Full.tapp
```

The expected TasmoClaw diff in the extension PR is:

```text
raw/TasmoClaw_Lite/*
raw/TasmoClaw_Full/*
extensions/tapp/TasmoClaw_Lite.tapp
extensions/tapp/TasmoClaw_Full.tapp
extensions/extensions.jsonl
```

If `gen.py` rewrites other existing `.tapp` files without content changes, Git
should normally ignore them. Review `git status` and `git diff --stat` before
staging.

## Verify The Extension Checkout

Run:

```bash
git status -sb
git diff --stat
git diff --check
```

Confirm both generated extension archives are ZIP_STORED:

```bash
python3 - <<'PY'
from pathlib import Path
from zipfile import ZipFile, ZIP_STORED

for p in [
    Path("extensions/tapp/TasmoClaw_Lite.tapp"),
    Path("extensions/tapp/TasmoClaw_Full.tapp"),
]:
    with ZipFile(p) as z:
        stored = all(i.compress_type == ZIP_STORED for i in z.infolist())
        print(p, p.stat().st_size, "ZIP_STORED=", stored)
        print([i.filename for i in z.infolist()])
PY
```

Confirm the extension index contains both entries:

```bash
sed -n '/TasmoClaw/p' extensions/extensions.jsonl
```

Check manifests:

```bash
cat raw/TasmoClaw_Lite/manifest.json
cat raw/TasmoClaw_Full/manifest.json
```

Run a secret scan in the extension checkout:

```bash
rg -n 'sk-[A-Za-z0-9]|api_key":\s*"sk-|Bearer sk-' \
  raw/TasmoClaw_Lite raw/TasmoClaw_Full extensions/extensions.jsonl extensions/tapp || true
```

Expected result: no real API key. Header-building code is fine.

## Runtime Smoke Test On A Board

Use one package at a time. Do not leave both Lite and Full uploaded under
different filenames when testing Extension Manager behavior.

Example board details used during initial testing:

```text
URL:    http://192.168.1.69/
Serial: /dev/cu.usbmodem1101
```

Some Tasmota builds deny `/cm` calls without a Referer header. Use:

```bash
-e 'http://192.168.1.69/'
```

Upload Lite as `/tasmoclaw.tapp`:

```bash
size=$(stat -f%z extensions/tapp/TasmoClaw_Lite.tapp)
curl -sS -i --max-time 90 \
  -e 'http://192.168.1.69/' \
  -F "ufsu=@extensions/tapp/TasmoClaw_Lite.tapp;filename=tasmoclaw.tapp" \
  "http://192.168.1.69/ufsu?download=/&fsz=$size"
```

Load and test:

```bash
curl -fsS --max-time 15 \
  -e 'http://192.168.1.69/' \
  -G --data-urlencode 'cmnd=Br load("/tasmoclaw.tapp")' \
  'http://192.168.1.69/cm'

curl -fsS --max-time 12 \
  -e 'http://192.168.1.69/' \
  'http://192.168.1.69/tasmoclaw/api/status'

curl -fsS --max-time 12 \
  -e 'http://192.168.1.69/' \
  'http://192.168.1.69/mn' | rg -o 'TasmoClaw' | wc -l
```

Expected Lite results:

- `/tasmoclaw/api/status` returns `ok:true` and `variant:"lite"`.
- Tools menu count is exactly `1`.

Unload Lite:

```bash
curl -fsS --max-time 10 \
  -e 'http://192.168.1.69/' \
  -G --data-urlencode 'cmnd=Br tasmota.unload_extension(global.tasmoclaw_common_driver)' \
  'http://192.168.1.69/cm'
```

Then upload and test Full the same way, using:

```text
extensions/tapp/TasmoClaw_Full.tapp
```

Expected Full results:

- `/tasmoclaw/api/status` returns `ok:true`.
- Status includes PSRAM/UFS information on PSRAM boards.
- Tools menu count is exactly `1`.

Unload Full:

```bash
curl -fsS --max-time 10 \
  -e 'http://192.168.1.69/' \
  -G --data-urlencode 'cmnd=Br tasmota.unload_extension(global.tasmoclaw_driver)' \
  'http://192.168.1.69/cm'
```

Reboot/autoload test for Full:

```bash
python3 - <<'PY'
import serial, time

ser = serial.Serial('/dev/cu.usbmodem1101', 115200, timeout=.2)
ser.write(b'Restart 1\r\n')
end = time.time() + 18
buf = b''
while time.time() < end:
    buf += ser.read(8192)
ser.close()
text = buf.decode('utf-8', 'replace')
for line in text.splitlines():
    if any(s in line for s in ['TAP:', 'TasmoClaw', 'TCL:', 'HTP:', 'HDW:', 'BRY:', 'WIF:', 'Project tasmota']):
        print(line)
PY
```

Expected boot log:

- `TAP: Loaded Tasmota App '/tasmoclaw.tapp'`
- `TasmoClaw driver registered`
- `TasmoClaw started`
- `HTP: Web server active ...`

After reboot:

```bash
curl -fsS --max-time 12 -e 'http://192.168.1.69/' \
  'http://192.168.1.69/tasmoclaw/api/status'

curl -fsS --max-time 12 -e 'http://192.168.1.69/' \
  'http://192.168.1.69/mn' | rg -o 'TasmoClaw' | wc -l
```

## Commit And Push

From the `Tasmota-Extensions-TasmoClaw` checkout:

```bash
git add \
  extensions/extensions.jsonl \
  extensions/tapp/TasmoClaw_Full.tapp \
  extensions/tapp/TasmoClaw_Lite.tapp \
  raw/TasmoClaw_Full \
  raw/TasmoClaw_Lite

git commit -m "Add TasmoClaw Tasmota extensions"
git push -u fork codex/add-tasmoclaw-extension
```

For update PRs, use an update-oriented commit message, for example:

```bash
git commit -m "Update TasmoClaw extensions"
```

## Open The PR

Create a draft PR against `tasmota/Tasmota-Extensions:main`:

```bash
cat > /tmp/tasmoclaw_pr_body.md <<'EOF'
## Summary

Add TasmoClaw as two Tasmota Extension Manager packages:

- **TasmoClaw Lite**: stock ESP32-friendly AI chat TAPP with the web UI, read tools, and approval-gated safe actions.
- **TasmoClaw Full**: PSRAM-targeted package with broader Tasmota tools, filesystem helpers, Berry programming helpers, and advanced workflows.

Both packages are Berry/TAPP-only from the extension repository point of view.

## What changed

- Added `raw/TasmoClaw_Lite/` with manifest, extension autoexec, shared Lite backend, Lite entrypoint, and UI.
- Added `raw/TasmoClaw_Full/` with manifest, extension autoexec, Full backend, tools, prompt, store, LLM, command catalog, and UI.
- Added generated `extensions/tapp/TasmoClaw_Lite.tapp` and `extensions/tapp/TasmoClaw_Full.tapp`.
- Updated `extensions/extensions.jsonl` through `python3 gen.py`.

## Validation

- Ran `python3 gen.py` in a fresh `Tasmota-Extensions` checkout.
- Verified both generated `.tapp` archives are ZIP_STORED/uncompressed.
- Tested Lite and Full on hardware by loading `/tasmoclaw.tapp`, checking `/tasmoclaw/api/status`, checking exactly one Tools menu entry, and testing unload.
- Tested Full reboot/autoload on a PSRAM board.
- Ran `git diff --check`.

## Notes

TasmoClaw Full targets PSRAM-capable devices. TasmoClaw Lite is the safer default for stock ESP32/no-PSRAM boards.
EOF

gh pr create \
  --repo tasmota/Tasmota-Extensions \
  --base main \
  --head speccy88:codex/add-tasmoclaw-extension \
  --draft \
  --title "Add TasmoClaw extensions" \
  --body-file /tmp/tasmoclaw_pr_body.md
```

For update PRs, adjust `--head`, title, and body to match the branch and change.

## Existing Initial PR

The first TasmoClaw extension PR was opened as:

```text
https://github.com/tasmota/Tasmota-Extensions/pull/2
```

Use this as a reference for expected file layout and PR wording.
