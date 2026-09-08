"""Check caller SP/locals at every instruction of the isolated arm64_unwind target.

Build arm64_unwind.c + arm64_unwind.S with -g on macOS ARM64, run dsymutil,
and configure an isolated RAD with a source breakpoint at arm64_unwind.c:6.
Pass the executable as --target; llvm-nm supplies only the address of the
assembly leaf, not the expected unwind results. This restarts the target.
"""
import argparse
import re
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('sender')
p.add_argument('pid', type=int)
p.add_argument('--target', required=True)
p.add_argument('--frame-record', action='store_true')
p.add_argument('--branch-body', action='store_true')
a = p.parse_args()

def ipc(command):
    return subprocess.check_output([a.sender, '--ipc', f'--pid:{a.pid}',
                                    *command.split()], text=True, timeout=15)

def state():
    raw = ipc('state')
    def number(key):
        m = re.search(rf'^\s*{key}:\s*(0x[\da-f]+|\d+)', raw, re.M)
        return int(m[1], 0) if m else None
    return dict(raw=raw, stop=number('stop_count'), running=number('running'),
                ip=number('ip'), voff=number('ip_voff'))

def stop(command):
    before = state()['stop']
    ipc(command)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        s = state()
        if not s['running'] and s['stop'] > before and (command != 'restart' or s['ip']):
            return s
        time.sleep(.03)
    raise AssertionError(state()['raw'])

def value(expr):
    raw = ipc('eval ' + expr)
    m = re.search(r'value:\s*"(0x[\da-f]+|-?\d+)"', raw)
    assert m, raw
    return int(m[1], 0)

symbols = subprocess.check_output(['xcrun', 'llvm-nm', a.target], text=True)
leaf = int(re.search(r'^([\da-fA-F]+) T _unwind_leaf$', symbols, re.M)[1], 16)
header = int(re.search(r'^([\da-fA-F]+) [A-Za-z] __mh_execute_header$', symbols, re.M)[1], 16)
s = stop('restart')
assert value('seed') == 20
caller_sp = value('sp')
caller_fp = value('x29')
caller_x19 = value('x19')
leaf += s['ip'] - s['voff'] - header
reply = ipc(f'add_address_breakpoint {leaf:#x}')
bp = re.search(r'\$[\da-fA-F]+', reply)
assert bp, reply
try:
    assert stop('run')['ip'] == leaf
    ipc(f'remove_breakpoint {bp[0]}')
    # Expected from the explicit assembly: allocation, body, then restoration.
    sizes = (0, 16, 16, 48, 48, 48, 48, 48, 16, 0) if a.frame_record else (0, 16, 16, 16, 16, 16, 0)
    boundaries = list(enumerate(sizes))
    boundaries = [(index*4, size) for index, size in boundaries]
    if a.branch_body:
        assert a.frame_record
        boundaries = [(0,0), (4,16), (8,16), (12,48), (16,48), (20,48),
                      (28,48), (32,48), (36,48), (40,48), (44,16), (48,0)]
    for index, (offset, stack_size) in enumerate(boundaries):
        if index:
            stop('step_into_inst')
        assert state()['ip'] == leaf + offset, state()['raw']
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
        recovered_sp = value('sp')
        seed = value('seed')
        print(f'+{offset}: caller SP delta={recovered_sp-caller_sp}, seed={seed}', flush=True)
        assert recovered_sp == caller_sp, f'caller SP at +{offset}'
        assert seed == 20, f'caller local at +{offset}'
        assert value('x29') == caller_fp, f'caller FP at +{offset}'
        if a.frame_record:
            assert value('x19') == caller_x19, f'caller x19 at +{offset}'
        ipc('up_one_frame')
    assert stop('run')['ip'] == 0
    print('PASS: all leaf instruction boundaries')
finally:
    ipc(f'remove_breakpoint {bp[0]}')
