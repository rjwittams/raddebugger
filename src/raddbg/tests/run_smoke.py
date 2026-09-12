"""Build a fixture, own an isolated cleat/RAD session, and retain its evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import tempfile
import time
import traceback
import uuid

from ipc_client import Client

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ARM_CASES = {
    'unwind-compact': [],
    'unwind-dwarf': ['DWARF_ROWS'],
    'unwind-frame': ['FRAME_RECORD'],
    'unwind-frame-dwarf': ['FRAME_RECORD', 'DWARF_ROWS'],
    'unwind-branch': ['FRAME_RECORD', 'BRANCH_BODY'],
    'unwind-unavailable': [],
    'tls-red-zone': [],
    'caller-registers': [],
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--debugger', required=True, type=Path)
    p.add_argument('--sender', type=Path, help='Default: same binary as debugger')
    p.add_argument('--compiler', default='cl' if os.name == 'nt' else 'clang')
    p.add_argument('--case', choices=['all', 'c', 'c-exit-37', 'c-detach', 'c-inline', 'c-inline-nested', *ARM_CASES], default='c')
    p.add_argument('--output-root', type=Path, help='Existing directory for retained, unique run directories')
    a = p.parse_args()
    a.debugger = a.debugger.resolve()
    a.sender = (a.sender or a.debugger).resolve()
    cases = ['c', 'c-exit-37', 'c-detach', 'c-inline', 'c-inline-nested', *ARM_CASES] if a.case == 'all' else [a.case]
    statuses = [run_case(a, case)['status'] for case in cases]
    # An explicitly requested unsupported case is not a passing test.
    return 1 if 'fail' in statuses else 2 if 'uncovered' in statuses else 0


def run_case(a, case):
    output = Path(tempfile.mkdtemp(prefix='rad-smoke-'+case+'-', dir=a.output_root))
    print(f'Artifacts: {output}', flush=True)
    record = dict(status='running', case=case, host=platform.node(), os=platform.platform(),
                  arch=platform.machine(), debugger=str(a.debugger), commands=[])
    session = 'rad-smoke-' + uuid.uuid4().hex[:10]
    launched = False
    client = None
    def run(command, **kwargs):
        record['commands'].append([str(x) for x in command])
        # A newly spawned cleat daemon can inherit its launcher's output handles.
        # Files let launch finish without waiting for that daemon to close a pipe.
        prefix = f'command-{len(record["commands"]):02}'
        with (output/(prefix+'.stdout')).open('w+', encoding='utf-8', errors='replace') as stdout, \
             (output/(prefix+'.stderr')).open('w+', encoding='utf-8', errors='replace') as stderr:
            result = subprocess.run(command, cwd=output, stdout=stdout, stderr=stderr, **kwargs)
            stdout.seek(0)
            stderr.seek(0)
            result.stdout, result.stderr = stdout.read(), stderr.read()
        with (output/'commands.log').open('a', encoding='utf-8') as stream:
            stream.write(repr(command)+'\n'+result.stdout+result.stderr+'\n')
        result.check_returncode()
        return result.stdout
    try:
        if case in ARM_CASES and (platform.system() != 'Darwin' or platform.machine() != 'arm64'):
            record.update(status='uncovered', reason='Requires a native macOS ARM64 debugger and target')
            return record
        record['debugger_sha256'] = sha(a.debugger)
        record['sender_sha256'] = sha(a.sender)
        record['source_revision'] = run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], timeout=10).strip()
        record['source_status'] = run(['git', '-C', str(ROOT), 'status', '--short'], timeout=10)
        patch = subprocess.check_output(['git', 'diff', '--binary', 'HEAD'], cwd=ROOT)
        (output/'source.patch').write_bytes(patch)
        record['source_patch_sha256'] = hashlib.sha256(patch).hexdigest()
        untracked = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '-z'], cwd=ROOT)
        record['untracked_sha256'] = {os.fsdecode(f): sha(ROOT/os.fsdecode(f)) for f in untracked.split(b'\0') if f}
        record['test_scripts_sha256'] = {f.name: sha(f) for f in HERE.glob('*.py')}
        help_text = run(['cleat', 'launch', '--help'], timeout=10)
        assert '--tag' in help_text, 'cleat is too old: launch must support --tag'
        stem = 'arm64_red_zone' if case in ('tls-red-zone', 'caller-registers') else 'arm64_unwind' if case in ARM_CASES else 'session'
        if case == 'c-inline':
            stem = 'inline'
        if case == 'c-inline-nested':
            stem = 'inline_nested'
        source = HERE/(stem+'.c')
        defines = ARM_CASES.get(case, []) + (['EXIT_CODE=37'] if case == 'c-exit-37' else [])
        sources = [source] + ([HERE/(stem+'.S')] if case in ARM_CASES else [])
        if case == 'unwind-unavailable':
            sources[1] = HERE/'arm64_unwind_error.S'
        record['fixture_hashes'] = {f.name: sha(f) for f in sources}
        if case == 'c-inline-nested':
            record['fixture_hashes']['inline_nested.h'] = sha(HERE/'inline_nested.h')
        target = output/('session.exe' if os.name == 'nt' else 'session')
        if os.name == 'nt':
            run([a.compiler, '/Zi', '/O2' if case.startswith('c-inline') else '/Od', *['/D'+d for d in defines], str(source), '/Fe:'+str(target), '/link', '/debug', '/incremental:no'], timeout=60)
        else:
            record['compiler_version'] = run([a.compiler, '--version'], timeout=10)
            objects = []
            for index, src in enumerate(sources):
                obj = f'fixture-{index}.o'
                run([a.compiler, '-g', '-O1' if case.startswith('c-inline') else '-O0', *['-D'+d for d in defines], '-c', str(src), '-o', obj], timeout=60)
                objects.append(obj)
            run([a.compiler, '-g', *objects, '-o', str(target)], timeout=60)
            if platform.system() == 'Darwin':
                run(['xcrun', 'dsymutil', str(target)], timeout=60)
        record['target_sha256'] = sha(target)
        record['debug_info_hashes'] = {str(f.relative_to(output)): sha(f) for f in output.rglob('*')
                                       if f.is_file() and ('.dSYM/' in str(f) or f.suffix.lower() == '.pdb')}
        project = output/'test.project'
        stop_line = 6 if case in ARM_CASES else next(i for i, line in enumerate(source.read_text().splitlines(), 1) if 'volatile int seed = 20' in line)
        if case.startswith('c-inline'):
            stop_line = next(i for i, line in enumerate(source.read_text().splitlines(), 1) if 'SNAPSHOT_STOP' in line)
        project.write_text('// raddbg 0.9.29 project file\n\ntarget:\n{\n'
                           f' executable: {json.dumps(str(target))}\n working_directory: {json.dumps(str(output))}\n enabled: 1\n}}\n'
                           f'breakpoint:\n{{\n source_location: {json.dumps(str(source)+f":{stop_line}:1")}\n enabled: 1\n}}\n', encoding='utf-8')
        argv = [str(a.debugger), '--user:'+str(output/'test.user'), '--project:'+str(project)]
        command = subprocess.list2cmdline(argv) if os.name == 'nt' else shlex.join(argv)
        run(['cleat', 'launch', session, '--tag', 'project=raddbg', '--tag', 'purpose=test',
             '--cwd', str(output), '--cmd', command], timeout=20)
        launched = True
        # Unix cleat launches RAD directly; Windows cleat may use a shell.
        info = run(['cleat', 'inspect', session, '--json'], timeout=10)
        record['cleat_inspect'] = json.loads(info)
        pid = record['cleat_inspect']['process']['leader_pid']
        if os.name == 'nt':
            deadline = time.monotonic()+5
            while time.monotonic() < deadline:
                query = (f'Get-CimInstance Win32_Process -Filter "ProcessId = {pid} OR ParentProcessId = {pid}" | '
                         'Select-Object ProcessId,ExecutablePath | ConvertTo-Json -Compress')
                rows = json.loads(run(['powershell', '-NoProfile', '-Command', query], timeout=10) or '[]')
                rows = rows if isinstance(rows, list) else [rows]
                matches = [r for r in rows if r.get('ExecutablePath') and Path(r['ExecutablePath']) == a.debugger]
                if len(matches) == 1:
                    pid = matches[0]['ProcessId']
                    break
                time.sleep(.05)
            else:
                raise AssertionError(f'Cannot identify RAD in owned cleat process tree: {rows}')
        record['debugger_pid'] = pid
        client = Client(a.sender, pid, output/'ipc.jsonl')
        deadline = time.monotonic()+15
        while time.monotonic() < deadline:
            try:
                initial = client.snapshot()
                if initial['status'] == 'ready':
                    break
            except (AssertionError, subprocess.SubprocessError):
                time.sleep(.1)
        else:
            raise AssertionError('Debugger did not become ready; see ipc.jsonl')
        record['debugger_build'] = initial['build']
        if case == 'c-inline-nested':
            source_lines = source.read_text().splitlines()
            header = HERE/'inline_nested.h'
            body_line = next(i for i, line in enumerate(header.read_text().splitlines(), 1) if 'INLINE_BODY' in line)
            outer_line = next(i for i, line in enumerate(source_lines, 1) if 'OUTER_CALL' in line)
            for visit in (1, 2):
                stopped = client.stopped('restart' if visit == 1 else 'run')
                frames = stopped['stack']['frames']
                assert [f['function'] for f in frames[:4]] == ['leaf', 'wrapper', 'outer', 'main'], stopped
                assert [f['inline_depth'] for f in frames[:4]] == [0, 2, 1, 0], stopped
                assert len({f['id'] for f in frames}) == len(frames), stopped
                for field in ('unwind_index', 'pc', 'sp', 'cfa'):
                    assert len({f[field] for f in frames[1:4]}) == 1, stopped
                caller_line = next(i for i, line in enumerate(source_lines, 1) if f'MAIN_CALL_{visit}' in line)
                for frame, path, lines in zip(frames, [source, header, source, source],
                                               [{stop_line}, {body_line, body_line+1}, {outer_line}, {caller_line}]):
                    assert frame['source']['is_source'] and Path(frame['source']['path']) == path, stopped
                    assert frame['source']['line'] in lines, stopped
            ended = client.stopped('run')
            assert ended['process_count'] == 0 and ended['last_process_end']['exit_code'] == 0, ended
            record['status'] = 'pass'
            return record
        if case == 'c-inline':
            stopped = client.stopped('restart')
            frames = stopped['stack']['frames']
            assert [f['function'] for f in frames[:3]] == ['leaf', 'wrapper', 'main'], stopped
            assert frames[1]['inline_depth'] == 1 and frames[2]['inline_depth'] == 0, stopped
            assert frames[1]['unwind_index'] == frames[2]['unwind_index'], stopped
            assert frames[1]['pc'] == frames[2]['pc'] and frames[1]['sp'] == frames[2]['sp'], stopped
            assert len({f['id'] for f in frames}) == len(frames), stopped
            for frame in frames[:3]:
                assert frame['source']['is_source'] and Path(frame['source']['path']) == source, stopped
            source_lines = source.read_text().splitlines()
            caller_line = next(i for i, line in enumerate(source_lines, 1) if 'result = wrapper(seed)' in line)
            body_line = next(i for i, line in enumerate(source_lines, 1) if 'int value = leaf(input)' in line)
            assert frames[0]['source']['line'] == stop_line, stopped
            assert frames[1]['source']['line'] in (body_line, body_line+1), stopped
            assert frames[2]['source']['line'] == caller_line, stopped
            ended = client.stopped('run')
            assert ended['process_count'] == 0 and ended['last_process_end']['exit_code'] == 0, ended
            record['status'] = 'pass'
            return record
        if case in ARM_CASES:
            script = 'arm64_red_zone_ipc.py' if case in ('tls-red-zone', 'caller-registers') else 'arm64_unwind_ipc.py'
            options = ['--frame-record'] if 'FRAME_RECORD' in defines else []
            if case == 'caller-registers':
                options.append('--caller-registers')
            if 'BRANCH_BODY' in defines:
                options.append('--branch-body')
            if case == 'unwind-unavailable':
                options.append('--broken-frame')
            run([sys.executable, str(HERE/script), str(a.sender), str(pid), '--target', str(target), *options],
                env=dict(os.environ, RAD_IPC_TRACE=str(output/'ipc.jsonl')), timeout=180)
            record['status'] = 'pass'
            return record
        stopped = client.stopped('restart')
        assert stopped['stop']['cause'] == 'breakpoint', stopped
        assert int(stopped['stop']['breakpoint_id'], 16) != 0, stopped
        target_pid = stopped['process_id']
        end_sequence = stopped['last_process_end']['sequence']
        observed_source = Path(stopped['stack']['frames'][0]['source']['path'])
        assert observed_source.is_absolute() and observed_source == source, stopped
        assert stopped['stack']['frames'][0]['source']['line'] == stop_line, stopped
        client.stopped('step_over_line')
        assert client.value('seed') == '20'
        stopped = client.stopped('step_into_line')
        if stopped['stack']['frames'][0]['source']['line'] == 8:
            client.stopped('step_over_line')
        stopped = client.stopped('step_over_line')
        frames = stopped['stack']['frames']
        assert frames[0]['function'] == 'leaf' and frames[1]['function'] == 'main', stopped
        assert frames[0]['pc'] != frames[1]['pc'] and frames[0]['sp'] != frames[1]['sp'], stopped
        assert client.value('input') == '20'
        assert client.value('doubled') == '40'
        client.command('down_one_frame')
        assert client.value('seed') == '20'
        client.command('up_one_frame')
        stopped = client.stopped('step_out')
        assert stopped['stack']['frames'][0]['function'] == 'main', stopped
        stopped = client.stopped('run')
        assert stopped['process_count'] == 0, stopped
        assert stopped['last_process_end'] == dict(sequence=end_sequence+1, process_id=target_pid,
                                                  exit_code=37 if case == 'c-exit-37' else 0), stopped
        if case == 'c-detach':
            stopped = client.stopped('restart')
            end_sequence = stopped['last_process_end']['sequence']
            client.command(['detach', 'query:control.'+stopped['process_handle']])
            deadline = time.monotonic()+10
            while time.monotonic() < deadline:
                stopped = client.snapshot()
                if stopped.get('process_count') == 0:
                    break
                time.sleep(.05)
            assert stopped['process_count'] == 0, stopped
            assert stopped['last_process_end']['sequence'] == end_sequence+1, stopped
            assert stopped['last_process_end']['exit_code'] is None, stopped
        record['status'] = 'pass'
    except FileNotFoundError as error:
        record.update(status='uncovered', reason=str(error))
    except Exception as error:
        record['status'] = 'fail'
        record['error'] = repr(error)
        record['traceback'] = traceback.format_exc()
        if client:
            try:
                record['failure_state_text'] = client.command('state')
            except Exception as observation_error:
                record['failure_state_error'] = repr(observation_error)
    finally:
        if client:
            try:
                client.command('kill_all')
                deadline = time.monotonic()+10
                while time.monotonic() < deadline:
                    if client.snapshot().get('process_count') == 0:
                        break
                    time.sleep(.05)
                else:
                    raise AssertionError('Owned debuggee did not end during cleanup')
            except Exception as error:
                record['cleanup_error'] = repr(error)
                record['status'] = 'fail'
        if launched:
            try:
                run(['cleat', 'capture', session], timeout=10)
            except Exception as error:
                record['capture_error'] = repr(error)
            try:
                run(['cleat', 'kill', session], timeout=15)
            except Exception as error:
                record['session_cleanup_error'] = repr(error)
                record['status'] = 'fail'
        record['cleat_session'] = session
        (output/'result.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
        print(record['status'].upper(), output/'result.json', flush=True)
    return record


if __name__ == '__main__':
    sys.exit(main())
