"""Check that cold TLS evaluation preserves an interrupted ARM64 red zone.

Build arm64_red_zone.c + arm64_red_zone.S with -g on macOS ARM64, run dsymutil,
and load an isolated RAD project with a source breakpoint at arm64_red_zone.c:6.
This restarts and kills that target; it must not share a user's debug session.
"""
import argparse
import re
import subprocess
import time
from ipc_client import Client

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('sender')
p.add_argument('pid', type=int)
p.add_argument('--target', required=True)
p.add_argument('--caller-registers', action='store_true')
a = p.parse_args()
client = Client(a.sender, a.pid)

def ipc(command):
    return client.command(command)

def stop(command):
    return client.stopped(command)

def value(expr):
    return int(client.value(expr), 0)

s = stop('restart')
symbols = subprocess.check_output(['xcrun', 'llvm-nm', a.target], text=True)
leaf = int(re.search(r'^([\da-fA-F]+) T _red_zone_leaf$', symbols, re.M)[1], 16)
header = int(re.search(r'^([\da-fA-F]+) [A-Za-z] __mh_execute_header$', symbols, re.M)[1], 16)
ip = int(s['stop']['pc'], 16)
voff = int(s['stack']['frames'][0]['module_offset'], 16)
address = leaf - header + ip - voff + 36
reply = ipc(f'add_address_breakpoint {address:#x}')
bp = re.search(r'\$[\da-fA-F]+', reply)
assert bp, reply
try:
    s = stop('run')
    assert int(s['stop']['pc'], 16) == address, s
    ipc('remove_breakpoint ' + bp[0])
    for _ in range(8):
        if value('pc') == address:
            break
        ipc('up_one_frame')
    assert value('pc') == address
    if a.caller_registers:
        assert value('x0') == 1234
        for expr in ('x0', 'x0+1', 'q8.u64s[1]'):
            result = client.evaluated(s, 1, expr)
            assert result['status'] == 'unavailable', result
        assert client.evaluated(s, 1, 'q8')['status'] == 'unsupported'  # A register union, not a scalar.
        for frame, expr, expected in ((0, 'x0', 1234), (1, 'sp', None), (1, 'q8.u64s[0]', None)):
            result = client.evaluated(s, frame, expr)
            assert result['status'] == 'ready', result
            if expected is not None:
                assert int.from_bytes(bytes.fromhex(result['value']['bytes']), 'little') == expected, result
        assert client.evaluated(s, 0, 'red_tls')['status'] == 'unsupported'
        assert value('x0') == 1234 and value('pc') == address
        ipc('down_one_frame')
        assert value('pc') != address
        assert value('sp') != 0
        raw = ipc('eval x0')
        assert 'unavailable' in raw.lower() and 'value: "1234"' not in raw, raw
        raw = ipc('eval x0+1')
        assert 'unavailable' in raw.lower(), raw
        assert 'unavailable' not in client.value('q8.u64s[0]').lower()
        assert 'unavailable' in ipc('eval q8.u64s[1]').lower()
        raw = ipc('eval q8')
        assert 'unavailable' in raw.lower(), raw
        ipc('up_one_frame')
        assert value('x0') == 1234
        print('PASS: caller x0 is unavailable; stopped x0 and recovered caller SP remain readable')
        raise SystemExit(0)
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
    ended = stop('run')
    assert ended['process_count'] == 0 and ended['last_process_end']['exit_code'] == 0, ended
    print('PASS: cold TLS evaluation preserves the red zone, SP, and PC')
finally:
    ipc('remove_breakpoint ' + bp[0])
    ipc('kill_all')
