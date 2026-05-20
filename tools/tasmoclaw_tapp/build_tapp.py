#!/usr/bin/env python3
from pathlib import Path
from zipfile import ZipFile, ZIP_STORED

root = Path(__file__).resolve().parent
src = root / 'src'
dist = root / 'dist'
dist.mkdir(exist_ok=True)
out = dist / 'tasmoclaw.tapp'
required = [
 'autoexec.be','tasmoclaw.be','tasmoclaw_ui.be','tasmoclaw_tools.be','tasmoclaw_llm.be','tasmoclaw_store.be','tasmoclaw_util.be','tasmoclaw_prompt.be'
]
for f in required:
    if not (src / f).exists():
        raise SystemExit(f'Missing required file: {f}')
with ZipFile(out, 'w', compression=ZIP_STORED) as zf:
    for f in required:
        zf.write(src / f, arcname=f)
print('Created', out)
with ZipFile(out) as zf:
    for i in zf.infolist():
        print(f'- {i.filename}: {i.file_size} bytes, compress_type={i.compress_type}')
print('Archive size:', out.stat().st_size, 'bytes')
