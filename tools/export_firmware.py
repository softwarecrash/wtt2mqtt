"""Export USB .bin and app-only .bin.ota automatically after each PlatformIO build."""
Import("env")
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

def export_firmware(source, target, env):
    root = Path(env.subst("$PROJECT_DIR"))
    output = root / "firmware"
    output.mkdir(exist_ok=True)
    app = Path(env.subst("$BUILD_DIR")) / (env.subst("$PROGNAME") + ".bin")
    project = env.GetProjectOption("custom_source_name", "WTT2MQTT")
    version = env.GetProjectOption("custom_source_version", "0.1.0")
    stem = f"{project}_{env.subst('$PIOENV')}_V{version}.bin"
    ota = output / (stem + ".ota")
    merged = output / stem
    images = env.Flatten(env.get("FLASH_EXTRA_IMAGES", []))
    args = [env.subst(str(item)) for item in images]
    args += [env.subst("$ESP32_APP_OFFSET") or "0x10000", str(app)]
    subprocess.run([env.subst("$PYTHONEXE"), env.subst("$OBJCOPY"), "--chip", "esp32s3",
                    "merge_bin", "--fill-flash-size", "4MB", "--output", str(merged), *args], check=True)
    shutil.copyfile(app, ota)
    manifest = {"board":"esp32s3_supermini", "version":version, "flash_bytes":0x400000,
                "usb_image":merged.name, "usb_offset":"0x0", "ota_image":ota.name,
                "files":{p.name:{"bytes":p.stat().st_size,"sha256":hashlib.sha256(p.read_bytes()).hexdigest()}
                         for p in (merged,ota)}}
    (output / "manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(f"USB firmware: {merged}")
    print(f"OTA firmware: {ota}")

# The alias runs even when the app is up-to-date, so deleted exports reappear
# on the next ordinary 'pio run' without needing a separate packaging command.
env.AddPostAction("buildprog", export_firmware)
env.AlwaysBuild("buildprog")
