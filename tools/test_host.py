"""Compile and run the actual protocol/controller against the WTTC model (no board needed)."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
os.chdir(root)
cxx = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
if not cxx:
    candidate = Path.home()/'.platformio/packages/toolchain-gccmingw32/bin/g++.exe'
    if candidate.exists():
        cxx = str(candidate)
if not cxx:
    sys.exit('Install g++/clang++ or set CXX (PlatformIO: pio pkg install -g -t platformio/toolchain-gccmingw32).')
build = root/'build'
build.mkdir(exist_ok=True)
exe = build/('test_protocol.exe' if os.name == 'nt' else 'test_protocol')
flags = ['-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-g']
if '--sanitize' in sys.argv:
    flags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
env = os.environ.copy()
env['PATH'] = str(Path(cxx).parent) + os.pathsep + env.get('PATH','')
subprocess.run([cxx,*flags,'-Ilib/WBus','-Ilib/Heater','-Ithird_party/wttc',
                'tests/test_protocol.cpp','lib/WBus/WBus.cpp','lib/Heater/Heater.cpp','-o',str(exe)],check=True,env=env)
subprocess.run([str(exe)],check=True,env=env)
