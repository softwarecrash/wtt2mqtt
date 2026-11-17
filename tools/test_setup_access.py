"""Compile the firmware's actual open-AP access policy; no hardware emulation."""
from pathlib import Path
import os
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
cxx=os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
if not cxx:
    candidate=Path.home()/'.platformio/packages/toolchain-gccmingw32/bin/g++.exe'
    if candidate.exists(): cxx=str(candidate)
if not cxx: raise SystemExit('Install g++/clang++ or set CXX')
out=ROOT/'build'
out.mkdir(exist_ok=True)
# Avoid Windows' legacy installer detection for executables named '*setup*'.
exe=out/('ap_access_checks.exe' if os.name=='nt' else 'ap_access_checks')
env=os.environ.copy()
env['PATH']=str(Path(cxx).parent)+os.pathsep+env.get('PATH','')
subprocess.run([cxx,'-std=c++17','-Wall','-Wextra','-Werror','-Iinclude','tests/test_setup_access.cpp','-o',str(exe)],check=True,env=env)
subprocess.run([str(exe)],check=True,env=env)
maintenance=out/('maintenance_checks.exe' if os.name=='nt' else 'maintenance_checks')
subprocess.run([cxx,'-std=c++17','-Wall','-Wextra','-Werror','-Ilib/WBus','-Ilib/Heater',
                'tests/test_maintenance.cpp','lib/WBus/WBus.cpp','lib/Heater/Heater.cpp','-o',str(maintenance)],check=True,env=env)
subprocess.run([str(maintenance)],check=True,env=env)
transport=out/('uart_receive_checks.exe' if os.name=='nt' else 'uart_receive_checks')
subprocess.run([cxx,'-std=c++17','-Wall','-Wextra','-Werror','-Ilib/WBus',
                'tests/test_bus_receive.cpp','lib/WBus/WBus.cpp','-o',str(transport)],check=True,env=env)
subprocess.run([str(transport)],check=True,env=env)
timing=out/('rx_timing_checks.exe' if os.name=='nt' else 'rx_timing_checks')
subprocess.run([cxx,'-std=c++17','-Wall','-Wextra','-Werror','-Iinclude',
                'tests/test_rx_timing.cpp','-o',str(timing)],check=True,env=env)
subprocess.run([str(timing)],check=True,env=env)

probe=out/('protocol_scan_checks.exe' if os.name=='nt' else 'protocol_scan_checks')
subprocess.run([cxx,'-std=c++17','-Wall','-Wextra','-Werror','-Ilib/WBus',
                'tests/test_protocol_scan.cpp','lib/WBus/WBus.cpp','lib/WBus/ProtocolScan.cpp','-o',str(probe)],check=True,env=env)
subprocess.run([str(probe)],check=True,env=env)
