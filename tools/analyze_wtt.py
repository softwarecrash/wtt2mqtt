"""Read-only static evidence extraction from the user's WTT 2.08 executable.

Dependencies: pefile, capstone. Never loads/executes the target as a program.
Output (including disassembly) belongs under ignored build/, not in releases.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('exe', type=Path)
parser.add_argument('--deps', type=Path, help='Optional local Python dependency directory')
parser.add_argument('--out', type=Path, default=Path('build/wtt-analysis/evidence'))
args = parser.parse_args()
if args.deps:
    sys.path.insert(0, str(args.deps.resolve()))
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

blob = args.exe.read_bytes()
digest = hashlib.sha256(blob).hexdigest()
if digest != 'd05d8de6cc7835be856bff1db6742f05ca0799201e11d29b89f363b7eb833b56':
    raise SystemExit('Unsupported binary hash: addresses in this extractor apply only to the supplied 2.08 MSI payload')
pe = pefile.PE(data=blob)
base = pe.OPTIONAL_HEADER.ImageBase
assert base == 0x400000
md = Cs(CS_ARCH_X86, CS_MODE_32)

def data(va, n):
    return pe.get_data(va-base, n)

def disassemble(va, n):
    return [{'va': f'0x{i.address:08X}', 'instruction': f'{i.mnemonic} {i.op_str}'}
            for i in md.disasm(data(va, n), va)]

strings = {}
for kind in pe.DIRECTORY_ENTRY_RESOURCE.entries:
    if kind.id != 6:
        continue
    for block in kind.directory.entries:
        for language in block.directory.entries:
            b = pe.get_data(language.data.struct.OffsetToData, language.data.struct.Size)
            pos = 0
            for index in range(16):
                n = struct.unpack_from('<H', b, pos)[0]
                pos += 2
                value = b[pos:pos+2*n].decode('utf-16le')
                pos += 2*n
                if value:
                    strings[(block.id-1)*16+index] = value

# These addresses are tied to this exact MSI payload, not arbitrary WTT builds.
expected = {
    9801: (0x497ECC, 0x444F50, 9600, 2),
    9803: (0x4979EC, 0x4560F5, 10400, 0),
    9804: (0x498004, 0x448A6D, 10400, 0),
    9805: (0x4980A0, 0x44D6D1, 10400, 0),
    9806: (0x497B24, 0x43E9E3, 10400, 0),
    9818: (0x49813C, 0x451F3C, 10400, 0),
}
profiles = []
for resource_id, (table, init, baud, parity) in expected.items():
    row = struct.unpack('<6I', data(table, 24))
    assert row[0] == resource_id and row[4] == init
    instructions = disassemble(init, 0x90)
    baud_line = next(i for i in instructions
                     if i['instruction'].startswith('mov dword ptr [')
                     and i['instruction'].endswith(f', 0x{baud:x}'))
    # Adjacent DCB bytes: ByteSize=8, Parity=0/2 (NOPARITY/EVENPARITY), StopBits=0.
    text = [i['instruction'] for i in instructions]
    assert any(s.startswith('mov byte ptr [') and s.endswith(', 8') for s in text)
    assert sum(s.startswith('mov byte ptr [') and s.endswith(', 0') for s in text) >= (3 if parity == 0 else 2)
    if parity:
        assert any(s.startswith('mov byte ptr [') and s.endswith(', 2') for s in text)
    profiles.append(dict(resource_id=resource_id, name=strings[resource_id],
                         table_va=f'0x{table:08X}', init_va=f'0x{init:08X}',
                         baud=baud, format='8E1' if parity == 2 else '8N1',
                         evidence=instructions, baud_instruction=baud_line))

def command_table(va, count):
    result=[]
    for index in range(count):
        row=data(va+index*56,56)
        n=struct.unpack_from('<I',row,32)[0]
        assert 0<n<=32
        result.append(dict(index=index, va=f'0x{va+index*56:08X}',
                           payload=row[:n].hex(' ').upper(),
                           response_callback=f'0x{struct.unpack_from("<I",row,48)[0]:08X}'))
    return result

def kwp(target, payload):
    b=bytes([0x80+len(payload),target,0xF1])+bytes(payload)
    return (b+bytes([sum(b)&255])).hex(' ').upper()

fiat=command_table(0x4E06E8,22)
psa=command_table(0x4E0BF8,24)
bmw=command_table(0x4E03D8,12)
assert data(0x4E0678,2)==bytes([0x51,0xF1])
assert data(0x4E0BB8,2)==bytes([0x01,0xF1])
assert data(0x4E0370,2)==bytes([0x6B,0x3F])
assert [r['payload'] for r in fiat[:2]]==['81','1A 80']
assert [r['payload'] for r in psa[:2]]==['81','21 80']
assert bmw[0]['payload']=='00'
output=dict(source=str(args.exe), sha256=digest, image_base=f'0x{base:X}',
            method='Static PE resource/data extraction and disassembly only; no target execution',
            profiles=profiles, fiat_commands=fiat, psa_commands=psa, bmw_commands=bmw,
            derived_read_only_frames=dict(fiat_session=kwp(0x51,[0x81]),
                fiat_ident=kwp(0x51,[0x1A,0x80]), psa_session=kwp(0x01,[0x81]),
                psa_ident=kwp(0x01,[0x21,0x80]), bmw_ident='3F 03 6B 00 57'),
            wakeup=dict(va='0x0041DA97',low_ms=300,high_ms=50,low2_ms=25,high2_ms=25,
                        evidence=disassemble(0x41DA97,0x9C)),
            frame_builders=dict(fiat=disassemble(0x44AF63,0xD0),psa=disassemble(0x44F896,0xD0),
                                bmw=disassemble(0x446443,0xD0)),
            session_key_checks=dict(fiat=disassemble(0x447475,0x2D),psa=disassemble(0x44BB85,0x2D)),
            byte_pacing=dict(fiat=disassemble(0x44B309,0x50),psa=disassemble(0x44FC3C,0x50)),
            limitations=['Static code evidence is not a successful hardware transaction.',
                         'Derived frames require correct wakeup, addressing and timing.',
                         'No proprietary executable code is copied into firmware.'])
args.out.mkdir(parents=True,exist_ok=True)
(args.out/'protocol-evidence.json').write_text(json.dumps(output,ensure_ascii=False,indent=2),encoding='utf-8')
for p in profiles:
    print(f"{p['resource_id']} {p['name']}: {p['baud']} {p['format']} init={p['init_va']}")
for key,value in output['derived_read_only_frames'].items():
    print(key,value)
print('Evidence:',args.out/'protocol-evidence.json')
