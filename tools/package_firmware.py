"""Package built firmware, a USB-only merged image and the matching project source."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import struct
import zipfile

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/release'
OUT.mkdir(parents=True,exist_ok=True)
ENV=ROOT/'.pio/build/esp32s3_supermini'
APP=ENV/'firmware.bin'
if not APP.exists():
    raise SystemExit('First run: pio run -e esp32s3_supermini')
if APP.stat().st_size>0x1f0000:
    raise SystemExit('Firmware exceeds OTA slot')
core=Path(os.environ.get('PLATFORMIO_CORE_DIR',Path.home()/'.platformio'))
framework=core/'packages/framework-arduinoespressif32'
if not (framework/'package.json').exists() or not json.loads((framework/'package.json').read_text())['version'].startswith('3.20017.'):
    raise SystemExit('Matching Arduino 2.0.17 framework not found; set PLATFORMIO_CORE_DIR')
parts=[(0,ENV/'bootloader.bin'),(0x8000,ENV/'partitions.bin'),
       (0xe000,framework/'tools/partitions/boot_app0.bin'),(0x10000,APP)]
image=bytearray(b'\xff'*(0x10000+APP.stat().st_size))
for index,(offset,path) in enumerate(parts):
    blob=path.read_bytes()
    if index+1<len(parts) and offset+len(blob)>parts[index+1][0]:
        raise SystemExit(f'Overlapping image: {path}')
    image[offset:offset+len(blob)]=blob
(OUT/'WTT2MQTT-0.1.0-esp32s3-4mb-usb-merged.bin').write_bytes(image)
shutil.copyfile(APP,OUT/'WTT2MQTT-0.1.0-esp32s3-4mb-ota.bin')
# Keep symbols and parts for reproducible flashing/debugging; no NVS credentials included.
for name in ['firmware.elf','bootloader.bin','partitions.bin']:
    shutil.copyfile(ENV/name,OUT/name)
source=[]
for folder in ['src','include','lib','web','tools','tests','docs','third_party','partitions','.github']:
    source.extend(p for p in (ROOT/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.name!='WebAssets.h')
source.extend(ROOT/name for name in ['README.md','LICENSE','.gitignore','platformio.ini'])
with zipfile.ZipFile(OUT/'WTT2MQTT-0.1.0-source.zip','w',zipfile.ZIP_DEFLATED) as z:
    for path in sorted(source):
        z.write(path,path.relative_to(ROOT))
manifest=dict(version='0.1.0',platform='espressif32@6.10.0',arduino='2.0.17',arduinojson='7.4.2',
              flash_bytes=0x400000,ota_slot_bytes=0x1f0000,psram=False,filesystem=False,
              usb_flash_offset='0x0',hardware_validated=False,files={})
for path in sorted(OUT.iterdir()):
    if path.suffix in ['.bin','.elf','.zip']:
        manifest['files'][path.name]=dict(bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(OUT/'SHA256SUMS.txt').write_text(''.join(f'{v["sha256"]}  {k}\n' for k,v in manifest['files'].items()))
(OUT/'FLASH.txt').write_text('USB only: esptool.py --chip esp32s3 --port COM7 write_flash 0x0 WTT2MQTT-0.1.0-esp32s3-4mb-usb-merged.bin\n'
                          'OTA web upload: WTT2MQTT-0.1.0-esp32s3-4mb-ota.bin only.\n'
                          'The merged image includes default OTA metadata and overwrites the NVS range with 0xff; use for initial setup/recovery, not OTA.\n'
                          'Keep the source archive and upstream framework/toolchain licenses with redistributed firmware.\n')
print(f'Packaged {APP.stat().st_size} byte OTA image, merged USB image, source and hashes in {OUT}')
