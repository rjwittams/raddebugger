"""Check automatic GUI source selection through the saved user layout.

Requires the graphical debugger, clang/dsymutil on macOS, and cleat.
Uses isolated configuration and retains source, binaries and IPC evidence.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import time
import uuid

from ipc_client import Client


def process_is_running(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--debugger', type=Path, required=True)
    parser.add_argument('--cycles', type=int, default=5)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('This fixture builder currently requires macOS clang/dsymutil')
    if args.cycles < 1:
        parser.error('--cycles must be positive')
    debugger = args.debugger.resolve()
    output = Path(tempfile.mkdtemp(prefix='rad-stop-navigation-')).resolve()
    print(f'Artifacts: {output}', flush=True)
    sources = {
        'main.c': 'extern int caller(int);\nint main(void)\n{\n  volatile int seed = 20;\n  return caller(seed);\n}\n',
        'caller.c': 'extern int check_value(int);\n__attribute__((noinline)) int caller(int seed)\n{\n  volatile int saved = seed;\n  return check_value(saved);\n}\n',
        'check.c': '#define assert(expr) !(expr) ? __builtin_trap() : (void)0\n__attribute__((noinline)) int check_value(int input)\n{\n  volatile int local = input + 2;\n  assert(local == 23);\n  return local;\n}\n',
    }
    for name, source in sources.items():
        (output/name).write_text(source)
    def run(argv):
        return subprocess.run(argv, cwd=output, capture_output=True, text=True,
                              check=True, timeout=60).stdout
    run(['clang', '-g', '-O0', '-c', *sources])
    run(['clang', '-g', 'main.o', 'caller.o', 'check.o', '-o', 'target'])
    run(['xcrun', 'dsymutil', 'target'])
    project = output/'test.project'
    user = output/'test.user'
    project.write_text('// raddbg 0.9.29 project file\n\ntarget:\n{\n executable: '+json.dumps(str(output/'target'))+
                       '\n enabled: 1\n}\n'+''.join(
                           'breakpoint:\n{\n source_location: '+json.dumps(str(output/name)+':5:1')+
                           '\n enabled: 1\n}\n' for name in ('main.c', 'caller.c')))
    session = 'rad-navigation-'+uuid.uuid4().hex[:10]
    client = None
    pid = None
    try:
        run(['cleat', 'launch', session, '--tag', 'project=raddbg', '--tag', 'purpose=test',
             '--cwd', str(output), '--cmd', shlex.join([str(debugger), '--user:'+str(user),
                                                       '--project:'+str(project)])])
        pid = json.loads(run(['cleat', 'inspect', session, '--json']))['process']['leader_pid']
        client = Client(debugger, pid, output/'ipc.jsonl')
        deadline = time.monotonic()+15
        while True:
            try:
                if client.snapshot()['status'] == 'ready':
                    break
            except (AssertionError, subprocess.SubprocessError):
                if time.monotonic() >= deadline:
                    raise
            if time.monotonic() >= deadline:
                raise AssertionError('Debugger did not become ready')
            time.sleep(.05)
        for cycle in range(args.cycles):
            for command, name, function, causes in (
                    ('restart', 'main.c', 'main', ('breakpoint',)),
                    ('run', 'caller.c', 'caller', ('breakpoint',)),
                    ('run', 'check.c', 'check_value', ('trap', 'exception'))):
                state = client.stopped(command)
                frame = state['stack']['frames'][0]
                assert frame['function'] == function and frame['source']['line'] == 5, state
                assert Path(frame['source']['path']) == output/name, state
                cause = state['stop']['cause']
                assert cause in causes, state
                deadline = time.monotonic()+3
                selected = []
                while time.monotonic() < deadline:
                    client.command('write_user_data')
                    layout = user.read_text() if user.exists() else ''
                    # This fixture has no nested text-view settings. Match the
                    # exported selected text tab, not a matching unselected tab.
                    selected = [block for block in re.findall(r'\btext:\s*\{([^{}]*)\}', layout)
                                if re.search(r'\bselected\b', block)]
                    if any(str(output/name) in block for block in selected):
                        break
                    time.sleep(.05)
                else:
                    raise AssertionError(f'cycle {cycle}: stopped at {name}:5 but selected source tabs are {selected}')
                print(f'{cycle}: {cause} {name}:5 selected', flush=True)
            client.command('down_one_frame')
            client.command('find_selected_thread')
            client.command('write_user_data')
            layout = user.read_text()
            selected = [block for block in re.findall(r'\btext:\s*\{([^{}]*)\}', layout)
                        if re.search(r'\bselected\b', block)]
            assert any(str(output/'caller.c') in block for block in selected), selected
            assert client.value('saved') == '20'
            print(f'{cycle}: explicit caller selection preserved', flush=True)
    finally:
        try:
            if client:
                client.command('kill_all')
                deadline = time.monotonic()+10
                while client.snapshot()['process_count'] != 0:
                    assert time.monotonic() < deadline, 'Owned target did not exit'
                    time.sleep(.03)
                client.command('exit')
                deadline = time.monotonic()+10
                while process_is_running(pid):
                    assert time.monotonic() < deadline, f'Owned RAD {pid} did not exit'
                    time.sleep(.03)
        finally:
            if pid is None or process_is_running(pid):
                subprocess.run(['cleat', 'kill', session], capture_output=True, timeout=10)
    print('PASS', flush=True)


if __name__ == '__main__':
    main()
