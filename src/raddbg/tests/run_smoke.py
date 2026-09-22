"""Build a fixture, own an isolated cleat/RAD session, and retain its evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import struct
import subprocess
import sys
import tempfile
import time
import traceback
import uuid

from ipc_client import Client
import language_basics

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ELF_CASES = ('c-elf-exec', 'c-elf-pie', 'c-elf-exec-no-index', 'c-elf-pie-no-index')
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


def omit_elf_unwind_index(target):
    # Change only the two optional-index encodings; keep the CFI and its pointer.
    data = bytearray(target.read_bytes())
    assert data[:6] == b'\x7fELF\x02\x01', 'Fixture requires little-endian ELF64'
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', data, 54)
    assert phentsize == 56 and phoff + phentsize*phnum <= len(data)
    for index in range(phnum):
        phdr = phoff + index*phentsize
        if struct.unpack_from('<I', data, phdr)[0] == 0x6474e550:  # PT_GNU_EH_FRAME
            offset = struct.unpack_from('<Q', data, phdr+8)[0]
            size = struct.unpack_from('<Q', data, phdr+32)[0]
            assert size >= 8 and offset + size <= len(data)
            assert data[offset] == 1 and data[offset+2] != 0xff and data[offset+3] != 0xff
            data[offset+2:offset+4] = b'\xff\xff'
            target.write_bytes(data)
            return
    raise AssertionError('Fixture has no PT_GNU_EH_FRAME')


def check_loader_frame(snapshot):
    if platform.system() == 'Darwin':
        frames = [f for f in snapshot['stack']['frames'] if f['module'] == '/usr/lib/dyld']
        assert len(frames) == 1, ('startup frame must identify dyld', snapshot)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--debugger', required=True, type=Path)
    p.add_argument('--sender', type=Path, help='Default: same binary as debugger')
    p.add_argument('--compiler', default='cl' if os.name == 'nt' else 'clang')
    p.add_argument('--case', choices=['all', 'languages', *['language-'+x for x in language_basics.LANGUAGES], 'c', 'c-eval', 'c-memory', 'c-exit-37', 'c-detach', 'c-inline', 'c-inline-nested', 'c-assert', 'c-assert-optimized', *ELF_CASES, 'symbol-library', *ARM_CASES], default='c')
    p.add_argument('--examples', type=Path, help='Explicit raddebugger_examples checkout for language cases')
    p.add_argument('--output-root', type=Path, help='Existing directory for retained, unique run directories')
    a = p.parse_args()
    a.debugger = a.debugger.resolve()
    a.sender = (a.sender or a.debugger).resolve()
    if a.output_root:
        a.output_root = a.output_root.resolve()
    cases = ['c', 'c-eval', 'c-memory', 'c-exit-37', 'c-detach', 'c-inline', 'c-inline-nested', 'c-assert', 'c-assert-optimized', *ELF_CASES, 'symbol-library', *ARM_CASES] if a.case == 'all' else [a.case]
    if a.case == 'languages':
        cases = ['language-'+x for x in language_basics.LANGUAGES]
    elif a.case == 'all' and a.examples:
        cases += ['language-'+x for x in language_basics.LANGUAGES]
    statuses = [run_case(a, case)['status'] for case in cases]
    # An explicitly requested unsupported case is not a passing test.
    return 1 if 'fail' in statuses else 2 if 'uncovered' in statuses else 0


def run_case(a, case):
    output = Path(tempfile.mkdtemp(prefix='rad-smoke-'+case+'-', dir=a.output_root)).resolve()
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
        if case in ELF_CASES and platform.system() != 'Linux':
            record.update(status='uncovered', reason='Requires the Linux ELF module loader')
            return record
        if case in ARM_CASES and (platform.system() != 'Darwin' or platform.machine() != 'arm64'):
            record.update(status='uncovered', reason='Requires a native macOS ARM64 debugger and target')
            return record
        if case.startswith('c-assert') and os.name == 'nt':
            record.update(status='uncovered', reason='Fixture requires __builtin_trap; the Windows runner uses MSVC')
            return record
        if case == 'symbol-library' and platform.system() != 'Darwin':
            record.update(status='uncovered', reason='Mach-O name-only symbol provider; ELF/PE providers not implemented')
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
        language = None
        if case.startswith('language-'):
            language = language_basics.build(a.examples, case[len('language-'):], output, run, record)
        stem = 'arm64_red_zone' if case in ('tls-red-zone', 'caller-registers') else 'arm64_unwind' if case in ARM_CASES else 'session'
        if case == 'c-inline':
            stem = 'inline'
        if case == 'c-inline-nested':
            stem = 'inline_nested'
        if case.startswith('c-assert'):
            stem = 'assert_trap'
        if case == 'symbol-library':
            stem = 'symbol_library_main'
        source = HERE/(stem+'.c')
        if language:
            source = Path(language['source'])
        defines = ARM_CASES.get(case, []) + (['EXIT_CODE=37'] if case == 'c-exit-37' else [])
        sources = [source] + ([HERE/(stem+'.S')] if case in ARM_CASES else [])
        if case == 'unwind-unavailable':
            sources[1] = HERE/'arm64_unwind_error.S'
        record['fixture_hashes'] = {f.name: sha(f) for f in sources}
        if case == 'c-inline-nested':
            record['fixture_hashes']['inline_nested.h'] = sha(HERE/'inline_nested.h')
        target = output/('session.exe' if os.name == 'nt' else 'session')
        if case == 'symbol-library':
            library_source = HERE/'symbol_library.c'
            library = output/'libsymbols.dylib'
            record['fixture_hashes'][library_source.name] = sha(library_source)
            run([a.compiler, '-dynamiclib', '-g0', '-O0', '-fno-omit-frame-pointer',
                 str(library_source), '-o', str(library)], timeout=60)
            run(['xcrun', 'strip', '-S', str(library)], timeout=60)
            record['library_sha256'] = sha(library)
        if language:
            assert Path(language['target']) == target, language
        elif os.name == 'nt':
            run([a.compiler, '/Zi', '/O2' if case.startswith('c-inline') else '/Od', *['/D'+d for d in defines], str(source), '/Fe:'+str(target), '/link', '/debug', '/incremental:no'], timeout=60)
        else:
            record['compiler_version'] = run([a.compiler, '--version'], timeout=10)
            compile_flags = []
            link_flags = []
            if case in ELF_CASES:
                compile_flags = ['-fno-pie' if case.startswith('c-elf-exec') else '-fPIE']
                link_flags = ['-no-pie' if case.startswith('c-elf-exec') else '-pie']
            objects = []
            for index, src in enumerate(sources):
                obj = f'fixture-{index}.o'
                optimization = '-O2' if case == 'c-assert-optimized' else '-O1' if case.startswith('c-inline') else '-O0'
                run([a.compiler, '-g', optimization, *compile_flags, *['-D'+d for d in defines], '-c', str(src), '-o', obj], timeout=60)
                objects.append(obj)
            run([a.compiler, '-g', *link_flags, *objects, '-o', str(target)], timeout=60)
            if platform.system() == 'Darwin':
                run(['xcrun', 'dsymutil', str(target)], timeout=60)
        if case in ELF_CASES:
            with target.open('rb') as stream:
                header = stream.read(18)
            assert header[:4] == b'\x7fELF' and header[5] in (1, 2), header
            record['elf_type'] = int.from_bytes(header[16:18], 'little' if header[5] == 1 else 'big')
            assert record['elf_type'] == (2 if case.startswith('c-elf-exec') else 3), record['elf_type']
            if case.endswith('-no-index'):
                omit_elf_unwind_index(target)
                record['unwind_index_omitted'] = True
        record['target_sha256'] = sha(target)
        record['debug_info_hashes'] = {str(f.relative_to(output)): sha(f) for f in output.rglob('*')
                                       if f.is_file() and ('.dSYM/' in str(f) or f.suffix.lower() == '.pdb')}
        project = output/'test.project'
        stop_line = language_basics.line(source, 'MATRIX_CALL') if language else 6 if case in ARM_CASES else next(i for i, line in enumerate(source.read_text().splitlines(), 1) if 'volatile int seed = 20' in line)
        if case.startswith('c-inline'):
            stop_line = next(i for i, line in enumerate(source.read_text().splitlines(), 1) if 'SNAPSHOT_STOP' in line)
        breakpoint = '' if case.startswith('c-assert') or case == 'symbol-library' else f'breakpoint:\n{{\n source_location: {json.dumps(str(source)+f":{stop_line}:1")}\n enabled: 1\n}}\n'
        if language:
            breakpoint += f'breakpoint:\n{{\n address_location: {json.dumps(language["leaf"])}\n enabled: 1\n}}\n'
            return_line = language_basics.line(source, 'MATRIX_RETURN')
            breakpoint += f'breakpoint:\n{{\n source_location: {json.dumps(str(source)+f":{return_line}:1")}\n enabled: 1\n}}\n'
        project.write_text('// raddbg 0.9.29 project file\n\ntarget:\n{\n'
                           f' executable: {json.dumps(str(target))}\n working_directory: {json.dumps(str(output))}\n enabled: 1\n}}\n'
                           + breakpoint, encoding='utf-8')
        argv = [str(a.debugger), '--user:'+str(output/'test.user'), '--project:'+str(project), '--logs:'+str(output/'logs')]
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
        if language:
            language_basics.exercise(client, source, language)
            record['status'] = 'pass'
            return record
        if case == 'symbol-library':
            stopped = client.stopped('restart')
            assert stopped['stop']['cause'] in ('trap', 'exception'), stopped
            frames = stopped['stack']['frames']
            for frame, name in zip(frames[:2], ('_symbol_library_leaf', '_symbol_library_caller')):
                assert Path(frame['module']).name == 'libsymbols.dylib', frame
                assert frame['symbol']['name'] == name and frame['symbol']['source'] == 'symbol_table', frame
                assert not frame['function'] and not frame['source']['is_source'], frame
                assert int(frame['symbol']['address'], 16)+int(frame['symbol']['offset'], 16) == int(frame['pc'], 16), frame
            assert len(frames) >= 3 and frames[2]['function'] == 'main', stopped
            assert frames[2]['symbol']['source'] == 'debug_info', frames[2]
            deadline = time.monotonic()+10
            while frames[0].get('debug_info_status') == 'pending' and time.monotonic() < deadline:
                time.sleep(.03)
                stopped = client.snapshot()
                frames = stopped['stack']['frames']
            assert frames[0].get('debug_info_status') == 'unavailable', frames[0]
            assert frames[2]['debug_info_status'] == 'ready', frames[2]
            assert stopped['symbols_pending'] == any(f['debug_info_status'] == 'pending' for f in frames), stopped
            record['status'] = 'pass'
            return record
        if case.startswith('c-assert'):
            stopped = client.stopped('restart')
            frames = stopped['stack']['frames']
            assert stopped['stop']['cause'] in ('trap', 'exception'), stopped
            assert [f['function'] for f in frames[:3]] == ['check_value', 'caller', 'main'], stopped
            assert frames[0]['source']['is_source'] and Path(frames[0]['source']['path']) == source, stopped
            trap_line = next(i for i, line in enumerate(source.read_text().splitlines(), 1) if 'assert(local' in line)
            assert frames[0]['source']['line'] == trap_line, stopped
            if case == 'c-assert':
                assert client.value('input') == '20'
            if platform.machine() == 'arm64':
                # Clang emits BRK #1. Source lines alone miss a one-instruction PC error at -O0.
                assert int(client.value('*(cast(uint32*) pc)'), 0) == 0xd4200020
            assert client.value('local') == '22'
            client.command('down_one_frame')
            assert client.value('seed') == '20'
            record['status'] = 'pass'
            return record
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
            for frame in (1, 2):
                result = client.evaluated(stopped, frame, 'input')
                assert result['status'] == 'unsupported', result
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
        check_loader_frame(stopped)
        client.stopped('step_over_line')
        assert client.value('seed') == '20'
        if case == 'c-memory':
            assert client.value('seed-20') == '0', 'A known zero must remain a value'
            for expr in ('*(cast(int32*)0)', '*(cast(int32*)0)+1', '*(cast(int32**)0)'):
                assert client.value(expr) == '<unavailable>', expr
        stopped = client.stopped('step_into_line')
        if stopped['stack']['frames'][0]['source']['line'] == 8:
            client.stopped('step_over_line')
        stopped = client.stopped('step_over_line')
        frames = stopped['stack']['frames']
        assert frames[0]['function'] == 'leaf' and frames[1]['function'] == 'main', stopped
        check_loader_frame(stopped)
        assert frames[0]['pc'] != frames[1]['pc'] and frames[0]['sp'] != frames[1]['sp'], stopped
        assert client.value('input') == '20'
        assert client.value('doubled') == '40'
        if case == 'c-eval':
            for frame, expr, expected in ((1, 'seed', 20), (0, 'doubled', 40), (1, 'seed-20', 0)):
                value = client.evaluated(stopped, frame, expr)
                assert value['status'] == 'ready', value
                assert int.from_bytes(bytes.fromhex(value['value']['bytes']), 'little', signed=True) == expected, value
                assert value['context']['frame'] == frame, value
                assert client.value('input') == '20', 'Explicit evaluation changed GUI frame selection'
            for expr in ('*(cast(int32*)0)', '*(cast(int32*)0)+1'):
                value = client.evaluated(stopped, 0, expr)
                assert value['status'] == 'unavailable', value
            assert client.evaluated(stopped, 0, 'not_a_local_here')['status'] == 'error'
            assert client.evaluated(stopped, 0, '1/0')['status'] == 'error'
            # RAD's '=' defines an evaluator macro, not a target-memory write.
            definition = client.evaluated(stopped, 0, 'doubled = 99')
            assert definition['status'] == 'ready', definition
            assert int.from_bytes(bytes.fromhex(definition['value']['bytes']), 'little') == 99, definition
            again = client.evaluated(stopped, 0, 'doubled')
            assert again['status'] == 'ready' and int.from_bytes(bytes.fromhex(again['value']['bytes']), 'little') == 40, again
            assert client.value('doubled') == '40', 'Read-only evaluation changed target memory or GUI macros'
            result = client.evaluated(stopped, 0, 'leaf(99)')
            assert result['status'] in ('error', 'unsupported'), result
            for expr in ('', 'x'*4097):
                assert client.evaluate(stopped, 0, expr)['status'] == 'invalid_request'
            assert client.evaluate(stopped, 0, 'input', unexpected=1)['status'] == 'invalid_request'
            assert client.evaluate(stopped, 999999, 'input')['status'] == 'invalid_request'
            assert client.evaluate(stopped, 0, 'input', thread_id=0)['status'] == 'invalid_request'
            assert client.evaluate(stopped, 0, 'input', reg_gen=stopped['reg_gen']+1)['status'] == 'stale'
            assert client.evaluate(stopped, 0, 'input', frame_pc='0x0')['status'] == 'stale'
            assert client.evaluate(stopped, 0, 'input', inline_depth=1)['status'] == 'stale'
            current = client.snapshot()
            assert all(current[k] == stopped[k] for k in ('run_gen', 'stop_count', 'mem_gen', 'reg_gen')), current
            eval_stop = stopped
        client.command('down_one_frame')
        assert client.value('seed') == '20'
        client.command('up_one_frame')
        stopped = client.stopped('step_out')
        if case == 'c-eval':
            assert client.evaluate(eval_stop, 0, 'input')['status'] == 'stale'
        assert stopped['stack']['frames'][0]['function'] == 'main', stopped
        stopped = client.stopped('run')
        assert stopped['process_count'] == 0, stopped
        assert stopped['last_process_end'] == dict(sequence=end_sequence+1, process_id=target_pid,
                                                  exit_code=37 if case == 'c-exit-37' else 0), stopped
        if case == 'c-detach':
            stopped = client.stopped('restart')
            check_loader_frame(stopped)
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
