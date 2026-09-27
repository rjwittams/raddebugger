"""Check caller SP/locals at every instruction of the isolated arm64_unwind target.

Build arm64_unwind.c + arm64_unwind.S with -g on macOS ARM64, run dsymutil,
and configure an isolated RAD with a source breakpoint at arm64_unwind.c:6.
Pass the executable as --target; llvm-nm supplies only the address of the
assembly leaf, not the expected unwind results. This restarts the target.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from ipc_client import Client

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('sender')
p.add_argument('pid', type=int)
p.add_argument('--target', required=True)
p.add_argument('--frame-record', action='store_true')
p.add_argument('--branch-body', action='store_true')
p.add_argument('--shrink-wrap', action='store_true')
p.add_argument('--body-only', action='store_true')
p.add_argument('--early-return', action='store_true')
p.add_argument('--broken-frame', action='store_true')
p.add_argument('--result', type=Path, help='Write the broken-frame observation as JSON')
a = p.parse_args()
client = Client(a.sender, a.pid)

def ipc(command):
    return client.command(command)

def state(snapshot=None):
    s = snapshot or client.snapshot()
    frames = s['stack']['frames']
    return dict(raw=s, stop=s['stop_count'], running=s['running'],
                ip=int(s['stop']['pc'], 16),
                voff=int(frames[0]['module_offset'], 16) if frames else 0)

def stop(command):
    return state(client.stopped(command))

def value(expr):
    return int(client.value(expr), 0)

symbols = subprocess.check_output(['xcrun', 'llvm-nm', a.target], text=True)
leaf = int(re.search(r'^([\da-fA-F]+) T _unwind_leaf$', symbols, re.M)[1], 16)
header = int(re.search(r'^([\da-fA-F]+) [A-Za-z] __mh_execute_header$', symbols, re.M)[1], 16)
s = stop('restart')
assert value('seed') == 20
caller_sp = value('sp')
caller_fp = value('x29')
caller_x19 = value('x19')
leaf += s['ip'] - s['voff'] - header
entry = leaf + (24 if a.body_only else 0)
assert not a.body_only or a.shrink_wrap
reply = ipc(f'add_address_breakpoint {entry:#x}')
bp = re.search(r'\$[\da-fA-F]+', reply)
assert bp, reply
try:
    assert stop('run')['ip'] == entry
    ipc(f'remove_breakpoint {bp[0]}')
    if a.broken_frame:
        stop('step_into_inst')
        before = client.snapshot()
        ipc('step_into_inst')
        deadline = time.monotonic()+5
        while time.monotonic() < deadline:
            broken = client.snapshot()
            if not broken['running'] and broken['stop_count'] > before['stop_count']:
                break
            time.sleep(.03)
        assert not broken['running'] and broken['stop_count'] > before['stop_count'], broken
        assert int(broken['stop']['pc'], 16) == leaf+8, broken
        # An unreadable stack currently may never settle in the memory cache.
        # A bounded pending result is honest; an apparently valid stack is not.
        for _ in range(10):
            broken = client.snapshot()
            assert broken['stack']['status'] in ('pending', 'error'), broken
            assert broken['stack']['frames'] == [], broken
            time.sleep(.05)
        observation = dict(stack_status=broken['stack']['status'], frame_count=len(broken['stack']['frames']))
        if a.result:
            a.result.write_text(json.dumps(observation)+'\n', encoding='utf-8')
        pending = observation['stack_status'] == 'pending'
        print(('UNCOVERED' if pending else 'PASS')+': unreadable stack is explicitly '+observation['stack_status'])
        sys.exit(2 if pending else 0)
    # Expected from the explicit assembly: allocation, body, then restoration.
    sizes = (0, 16, 16, 48, 48, 48, 48, 48, 16, 0) if a.frame_record else (0, 16, 16, 16, 16, 16, 0)
    boundaries = list(enumerate(sizes))
    boundaries = [(index*4, size) for index, size in boundaries]
    if a.shrink_wrap:
        assert a.frame_record
        boundaries = ([(0, 0)] + [(offset+4, size) for offset, size in boundaries if offset < 20] +
                      [(24, 48)] + [(offset+8, size) for offset, size in boundaries if offset >= 20])
        if a.body_only:
            boundaries = [(offset, size) for offset, size in boundaries if offset >= 24]
        if a.early_return:
            boundaries = [(0, 0), (44, 0)]
    if a.branch_body:
        assert a.frame_record
        boundaries = [(0,0), (4,16), (8,16), (12,48), (16,48), (20,48),
                      (28,48), (32,48), (36,48), (40,48), (44,16), (48,0)]
    for index, (offset, stack_size) in enumerate(boundaries):
        if index:
            stop('step_into_inst')
        assert state()['ip'] == leaf + offset, state()['raw']
        frames = client.snapshot()['stack']['frames']
        assert frames[0]['lookup_pc'] == frames[0]['pc'], frames[0]
        assert int(frames[1]['pc'], 16) == int(frames[1]['lookup_pc'], 16)+4, frames[1]
        symbol = frames[0].get('symbol', {})
        assert symbol.get('name') == '_unwind_leaf', frames[0]
        assert symbol['source'] == 'symbol_table' and int(symbol['offset'], 16) == offset, symbol
        assert not frames[0]['function'], 'Assembly label must not invent a DWARF procedure'
        assert frames[1]['function'] == 'main' and frames[1]['symbol']['source'] == 'debug_info', frames[1]
        # Source navigation may initially select main when the assembly has no
        # source DIE. Explicitly select the actual stopped frame before reading.
        for _ in range(8):
            if value('pc') == state()['ip']:
                break
            ipc('up_one_frame')
        assert value('pc') == state()['ip']
        top_sp = value('sp')
        assert top_sp + stack_size == caller_sp
        ipc('down_one_frame')
        assert value('pc') == int(frames[1]['pc'], 16), frames[1]
        recovered_sp = value('sp')
        seed = value('seed')
        print(f'+{offset}: caller SP delta={recovered_sp-caller_sp}, seed={seed}', flush=True)
        assert recovered_sp == caller_sp, f'caller SP at +{offset}'
        assert seed == 20, f'caller local at +{offset}'
        assert value('x29') == caller_fp, f'caller FP at +{offset}'
        if a.frame_record:
            assert value('x19') == caller_x19, f'caller x19 at +{offset}'
        ipc('up_one_frame')
    ended = stop('run')['raw']
    assert ended['process_count'] == 0 and ended['last_process_end']['exit_code'] == 0, ended
    print('PASS: all leaf instruction boundaries')
finally:
    ipc(f'remove_breakpoint {bp[0]}')
