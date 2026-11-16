"""Embed deterministic gzip assets in program flash, without a filesystem."""
import gzip
from pathlib import Path

def generate():
    lines = ['// Generated; do not edit.\n#pragma once\n#include <Arduino.h>\n',
             'struct WebAsset { const char* path; const char* mime; const uint8_t* data; size_t size; };\n']
    entries = []
    for i, path in enumerate(sorted(Path('web').glob('*'))):
        if not path.is_file():
            continue
        mime = {'.html':'text/html; charset=utf-8','.css':'text/css','.js':'application/javascript'}.get(path.suffix)
        if not mime:
            continue
        data = gzip.compress(path.read_bytes(), compresslevel=9, mtime=0)
        lines.append(f'const uint8_t asset{i}[] PROGMEM = {{{",".join(str(x) for x in data)}}};\n')
        entries.append(f'{{"/{path.name}", "{mime}", asset{i}, sizeof(asset{i})}}')
    lines.append('const WebAsset webAssets[] = {' + ','.join(entries) + '};\n')
    target = Path('include/WebAssets.h')
    content = ''.join(lines)
    if not target.exists() or target.read_text() != content:
        target.write_text(content)

generate()
