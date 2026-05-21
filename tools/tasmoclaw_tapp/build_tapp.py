#!/usr/bin/env python3
import argparse
import json
import shutil
from pathlib import Path
from zipfile import ZipFile, ZIP_STORED

root = Path(__file__).resolve().parent
src = root / 'src'
dist = root / 'dist'
dist.mkdir(exist_ok=True)

EXT_RAW = dist / 'tasmota_extensions' / 'raw'
EXT_VERSION = '0x1A051500'
EXT_MIN_TASMOTA = '0x0F040000'
EXT_AUTHOR = 'Frederick Blais'

TARGETS = {
    'lite': {
        'out': 'tasmoclaw_lite.tapp',
        'files': [
            ('autoexec_lite.be', 'autoexec.be'),
            ('tasmoclaw_lite.be', 'tasmoclaw_lite.be'),
            ('tasmoclaw_common.be', 'tasmoclaw_common.be'),
            ('tasmoclaw_ui.be', 'tasmoclaw_ui.be'),
        ],
    },
    'full': {
        'out': 'tasmoclaw.tapp',
        'files': [
            ('autoexec.be', 'autoexec.be'),
            ('tasmoclaw_util.be', 'tasmoclaw_util.be'),
            ('tasmoclaw_commands.be', 'tasmoclaw_commands.be'),
            ('tasmoclaw_store.be', 'tasmoclaw_store.be'),
            ('tasmoclaw_tools.be', 'tasmoclaw_tools.be'),
            ('tasmoclaw_llm.be', 'tasmoclaw_llm.be'),
            ('tasmoclaw_ui.be', 'tasmoclaw_ui.be'),
            ('tasmoclaw_prompt.be', 'tasmoclaw_prompt.be'),
            ('tasmoclaw.be', 'tasmoclaw.be'),
        ],
    },
}

EXTENSIONS = {
    'lite': {
        'dir': 'TasmoClaw_Lite',
        'manifest': {
            'name': 'TasmoClaw Lite',
            'version': EXT_VERSION,
            'description': 'Stock ESP32-friendly TasmoClaw AI chat with nice UI, read tools, and approval-gated safe Tasmota actions',
            'author': EXT_AUTHOR,
            'min_tasmota': EXT_MIN_TASMOTA,
            'features': 'berry,webserver',
        },
        'files': [
            ('autoexec_ext_lite.be', 'autoexec.be'),
            ('tasmoclaw_lite.be', 'tasmoclaw_lite.be'),
            ('tasmoclaw_common.be', 'tasmoclaw_common.be'),
            ('tasmoclaw_ui.be', 'tasmoclaw_ui.be'),
        ],
    },
    'full': {
        'dir': 'TasmoClaw_Full',
        'manifest': {
            'name': 'TasmoClaw Full',
            'version': EXT_VERSION,
            'description': 'Full TasmoClaw AI chat for PSRAM devices with broad Tasmota tools, Berry programming, filesystem, and advanced workflows',
            'author': EXT_AUTHOR,
            'min_tasmota': EXT_MIN_TASMOTA,
            'features': 'berry,webserver,psram',
        },
        'files': [
            ('autoexec_ext_full.be', 'autoexec.be'),
            ('tasmoclaw_util.be', 'tasmoclaw_util.be'),
            ('tasmoclaw_commands.be', 'tasmoclaw_commands.be'),
            ('tasmoclaw_store.be', 'tasmoclaw_store.be'),
            ('tasmoclaw_tools.be', 'tasmoclaw_tools.be'),
            ('tasmoclaw_llm.be', 'tasmoclaw_llm.be'),
            ('tasmoclaw_ui.be', 'tasmoclaw_ui.be'),
            ('tasmoclaw_prompt.be', 'tasmoclaw_prompt.be'),
            ('tasmoclaw.be', 'tasmoclaw.be'),
        ],
    },
}

def source_for_archive(source_name):
    text = (src / source_name).read_text()
    out = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            continue
        out.append(stripped)
    return '\n'.join(out) + '\n'


def ensure_sources(name, files):
    for source, _arcname in files:
        if not (src / source).exists():
            raise SystemExit(f'Missing required file for {name}: {source}')


def build(name):
    spec = TARGETS[name]
    out = dist / spec['out']
    ensure_sources(name, spec['files'])
    with ZipFile(out, 'w', compression=ZIP_STORED) as zf:
        for source, arcname in spec['files']:
            zf.writestr(arcname, source_for_archive(source), compress_type=ZIP_STORED)
    print(f'Created {out} ({name})')
    with ZipFile(out) as zf:
        for i in zf.infolist():
            print(f'- {i.filename}: {i.file_size} bytes, compress_type={i.compress_type}')
    print('Archive size:', out.stat().st_size, 'bytes')


def export_extension(name):
    spec = EXTENSIONS[name]
    out_dir = EXT_RAW / spec['dir']
    ensure_sources(f'{name} extension', spec['files'])

    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    (out_dir / 'manifest.json').write_text(
        json.dumps(spec['manifest'], indent=2) + '\n'
    )

    for source, arcname in spec['files']:
        (out_dir / arcname).write_text(source_for_archive(source))

    print(f'Exported extension raw folder: {out_dir}')


parser = argparse.ArgumentParser(description='Build TasmoClaw TAPP packages')
parser.add_argument('--target', choices=['all', 'full', 'lite'], default='all')
parser.add_argument(
    '--skip-extension-export',
    action='store_true',
    help='Only build standalone dist/*.tapp files; do not export Tasmota-Extensions raw folders',
)
args = parser.parse_args()

targets = ['lite', 'full'] if args.target == 'all' else [args.target]
for target in targets:
    build(target)
    if not args.skip_extension_export:
        export_extension(target)
