"""Check that cold TLS evaluation preserves an interrupted ARM64 red zone.

Build arm64_red_zone.c + arm64_red_zone.S with -g on macOS ARM64, run dsymutil,
and load an isolated RAD project with a source breakpoint at arm64_red_zone.c:6.
This restarts and kills that target; it must not share a user's debug session.
"""
import argparse
import re
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('sender')
p.add_argument('pid', type=int)
p.add_argument('--target', required=True)
a = p.parse_args()

def ipc(command):
    return subprocess.check_output([a.sender, '--ipc', f'--pid:{a.pid}',
                                    *command.split()], text=True, timeout=15)

def stop(command):
    before = int(re.search(r'stop_count: (\d+)', ipc('state'))[1])
    ipc(command)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        raw = ipc('state')
        if 'running: 0' in raw and int(re.search(r'stop_count: (\d+)', raw)[1]) > before:
            return raw
        time.sleep(.03)
    raise AssertionError(raw)

def value(expr):
    raw = ipc('eval ' + expr)
    m = re.search(r'value:\s*"(0x[\da-f]+|\d+)"', raw)
    assert m, raw
    return int(m[1], 0)

s = stop('restart')
symbols = subprocess.check_output(['xcrun', 'llvm-nm', a.target], text=True)
leaf = int(re.search(r'^([\da-fA-F]+) T _red_zone_leaf$', symbols, re.M)[1], 16)
header = int(re.search(r'^([\da-fA-F]+) [A-Za-z] __mh_execute_header$', symbols, re.M)[1], 16)
ip = int(re.search(r'ip: (0x[\da-f]+)', s)[1], 16)
voff = int(re.search(r'ip_voff: (0x[\da-f]+)', s)[1], 16)
address = leaf - header + ip - voff + 36
reply = ipc(f'add_address_breakpoint {address:#x}')
bp = re.search(r'\$[\da-fA-F]+', reply)
assert bp, reply
try:
    s = stop('run')
    assert int(re.search(r'ip: (0x[\da-f]+)', s)[1], 16) == address, s
    ipc('remove_breakpoint ' + bp[0])
    for _ in range(8):
        if value('pc') == address:
            break
        ipc('up_one_frame')
    assert value('pc') == address
    original_sp = value('sp')
    registers = {f'x{i}': value(f'x{i}') for i in range(31)}
    before = [value(f'*(cast(uint64*) ({original_sp}-{i}))') for i in range(8, 129, 8)]
    assert before == [1234]*16, before
    print('before TLS:', before, flush=True)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        raw = ipc('eval red_tls')
        if 'value: "123"' in raw:
            break
        time.sleep(.05)
    assert 'value: "123"' in raw, raw
    after = [value(f'*(cast(uint64*) ({original_sp}-{i}))') for i in range(8, 129, 8)]
    print('after TLS:', after, flush=True)
    assert value('sp') == original_sp and value('pc') == address
    assert {name: value(name) for name in registers} == registers
    assert after == before, 'TLS evaluation clobbered the interrupted red zone'
    assert 'ip: 0x0\n' in stop('run')
    print('PASS: cold TLS evaluation preserves the red zone, SP, and PC')
finally:
    ipc('remove_breakpoint ' + bp[0])
    ipc('kill_all')
