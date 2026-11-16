"""Check actual binary flash layout and byte-identical vendored simulator sources."""
from pathlib import Path
import gzip
import hashlib
import json
import struct

ROOT=Path(__file__).resolve().parents[1]
build=ROOT/'.pio/build/esp32s3_supermini'
data=(build/'partitions.bin').read_bytes()
parts=[]
for pos in range(0,len(data),32):
    magic,kind,sub,offset,size,label,flags=struct.unpack('<HBBII16sI',data[pos:pos+32])
    if magic!=0x50aa:
        break
    parts.append((label.rstrip(b'\0').decode(),kind,sub,offset,size))
assert parts==[('nvs',1,2,0x9000,0x5000),('otadata',1,0,0xe000,0x2000),
               ('app0',0,0x10,0x10000,0x1f0000),('app1',0,0x11,0x200000,0x1f0000)],parts
for name in ['bootloader.bin','firmware.bin']:
    b=(build/name).read_bytes()
    assert b[0]==0xe9
    assert b[3]>>4==2, f'{name}: flash header must specify 4 MB'
    assert struct.unpack_from('<H',b,12)[0]==9, f'{name}: chip ID must be ESP32-S3'
assert (build/'firmware.bin').stat().st_size<=0x1f0000
lock=json.loads((ROOT/'third_party/wttc/SOURCES.json').read_text())
for name,expected in lock['sha256'].items():
    assert hashlib.sha256((ROOT/'third_party/wttc'/name).read_bytes()).hexdigest()==expected,name
print('PASS binary partitions: NVS, OTA metadata, two 1984-KiB apps; no filesystem; 4-MB ESP32-S3 headers')
print('PASS vendored WTTC files match recorded upstream SHA-256 hashes')
