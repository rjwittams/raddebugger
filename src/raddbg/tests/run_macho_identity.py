"""Live macOS checks for debug-info build identity, conversion and cache reuse."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('root', nargs='?', type=Path, default=Path(__file__).resolve().parents[3])
parser.add_argument('--debugger', type=Path)
parser.add_argument('--sender', type=Path)
args = parser.parse_args()
root = args.root.resolve()
debugger = (args.debugger or root/'build/raddbg').resolve()
sender = (args.sender or root/'build/raddbg_non_graphical').resolve()
assert sys.platform == 'darwin', 'This regression requires macOS and a GUI-started cleat daemon'
sys.path.insert(0, str(root/'src/raddbg/tests'))
from ipc_client import Client

out = Path(tempfile.mkdtemp(prefix='identity-live-', dir=root/'build'))
print(out, flush=True)
results = []

def run(args):
    with tempfile.TemporaryFile(mode='w+') as stream:
        result = subprocess.run([str(a) for a in args], stdout=stream, stderr=stream, timeout=30)
        stream.seek(0)
        output = stream.read()
    with (out/'commands.jsonl').open('a') as log:
        log.write(json.dumps({'command': [str(a) for a in args], 'returncode': result.returncode, 'output': output})+'\n')
    result.check_returncode()
    return output

metadata = {'source_revision': run(['git', '-C', root, 'rev-parse', 'HEAD']).strip(),
            'source_status': run(['git', '-C', root, 'status', '--short']),
            'debugger_sha256': hashlib.sha256(debugger.read_bytes()).hexdigest(),
            'sender_sha256': hashlib.sha256(sender.read_bytes()).hexdigest(),
            'radbin_sha256': hashlib.sha256((root/'build/radbin').read_bytes()).hexdigest()}
(out/'metadata.json').write_text(json.dumps(metadata, indent=2))
(out/'source.patch').write_text(run(['git', '-C', root, 'diff', '--binary', 'HEAD']))

def stop_target(client):
    client.command('kill_all')
    deadline = time.monotonic()+10
    while client.snapshot()['process_count'] != 0:
        assert time.monotonic() < deadline, 'Owned target did not exit'
        time.sleep(.05)

@contextmanager
def debugger_client(name, target=None):
    folder = out/name
    folder.mkdir()
    project = folder/'test.project'
    text = '// raddbg 0.9.29 project file\n'
    if target:
        text += '\ntarget:\n{\n executable: '+json.dumps(str(target))+'\n working_directory: '+json.dumps(str(target.parent))+'\n enabled: 1\n}\n'
    project.write_text(text)
    session = 'rad-identity-'+name+'-'+out.name[-8:]
    client = None
    try:
        command = shlex.join([str(debugger), '--user:'+str(folder/'test.user'),
                              '--project:'+str(project), '--logs:'+str(folder/'logs')])
        run(['cleat', 'launch', session, '--tag', 'project=raddebugger', '--tag', 'purpose=test', '--cmd', command])
        pid = json.loads(run(['cleat', 'inspect', session, '--json']))['process']['leader_pid']
        client = Client(sender, pid, folder/'ipc.jsonl')
        for _ in range(100):
            try:
                if client.snapshot()['status'] == 'ready':
                    break
            except (AssertionError, subprocess.SubprocessError):
                pass
            time.sleep(.1)
        else:
            raise AssertionError('Debugger did not become ready')
        yield client, folder
    finally:
        try:
            if client:
                try:
                    stop_target(client)
                finally:
                    client.command('exit')
        finally:
            (out/'results.json').write_text(json.dumps(results, indent=2))

def probe(name, target, want_source, after_stop=None, expected_file='fresh.c', expected_line=4):
    with debugger_client(name, target) as (client, folder):
        state = client.stopped('restart')
        deadline = time.monotonic()+20
        while state['symbols_pending'] and time.monotonic() < deadline:
            time.sleep(.1)
            state = client.snapshot()
        (folder/'snapshot.json').write_text(json.dumps(state, indent=2))
        assert state['stop']['cause'] in ('trap', 'exception'), state
        frame = state['stack']['frames'][0]
        assert frame['source']['is_source'] == want_source, frame
        if want_source:
            assert frame['function'] == 'main' and frame['source']['line'] == expected_line, frame
            assert Path(frame['source']['path']) == out/expected_file, frame
        results.append({'case': name, 'status': 'pass', 'frame': frame, 'build': state['build']})
        print('PASS', name, flush=True)
        if after_stop:
            after_stop(client, folder)

fresh = out/'fresh.c'
fresh.write_text('int main(void)\n{\n volatile int first_value = 42;\n __builtin_trap();\n return first_value;\n}\n'
                 'typedef struct { char bytes[37]; } IdentityProbe;\nIdentityProbe probe_global;\n')
old = out/'old.c'
old.write_text('\n\n\n\nint main(void)\n{\n volatile int second_value = 84;\n __builtin_trap();\n return second_value;\n}\n')
images = out/'images'
images.mkdir()
target = images/'prog'
old_target = out/'old-prog'
for source, binary in ((fresh, target), (old, old_target)):
    obj = source.with_suffix('.o')
    run(['clang', '-g', '-O0', '-c', source, '-o', obj])
    run(['clang', obj, '-o', binary])
    run(['xcrun', 'dsymutil', binary])
    assert 'DW_TAG_compile_unit' in run(['xcrun', 'dwarfdump', '--debug-info', str(binary)+'.dSYM'])
    run(['strip', '-S', binary])
dsym = Path(str(target)+'.dSYM')
saved = out/'matching.dSYM'
shutil.copytree(dsym, saved)
saved_target = out/'matching-prog'
shutil.copy2(target, saved_target)
probe('matching', target, True)
caches = list(images.glob('*.rdi'))
assert len(caches) == 1, caches
cache = caches[0]
original = cache.read_bytes()
cache.write_bytes(b'not a valid RDI')
os.utime(cache, (time.time()+60, time.time()+60))
probe('corrupt-cache', target, True)
assert cache.read_bytes() == original, 'Cache did not regenerate identically'
sections = struct.unpack_from('<I', original, 12)[0]
top = struct.unpack_from('<Q', original, sections+32+8)[0]
wrong_uuid = bytearray(original)
wrong_uuid[top+24] ^= 1
cache.write_bytes(wrong_uuid)
os.utime(cache, (time.time()+60, time.time()+60))
probe('wrong-uuid-cache', target, True)
assert cache.read_bytes() == original, 'Wrong-UUID cache was not regenerated'
cache.write_bytes(wrong_uuid)
os.utime(cache, (time.time()+60, time.time()+60))
cache.chmod(0o444)
try:
    probe('failed-conversion', target, False)
    assert cache.read_bytes() == wrong_uuid, 'Fixture failed to prevent conversion output'
finally:
    cache.chmod(0o644)
cache.write_bytes(original)
shutil.rmtree(dsym)
shutil.copytree(Path(str(old_target)+'.dSYM'), dsym)
os.rename(dsym/'Contents/Resources/DWARF/old-prog', dsym/'Contents/Resources/DWARF/prog')
probe('stale-dsym-good-cache', target, True)
cache.unlink()
probe('stale-dsym-no-cache', target, False)
shutil.rmtree(dsym)
shutil.copytree(saved, dsym)
if cache.exists():
    cache.unlink()
images.chmod(0o555)
try:
    probe('readonly-fallback', target, True)
    assert not list(images.glob('*.rdi')), 'Wrote cache in readonly directory'
finally:
    images.chmod(0o755)

def replace_and_restart(client, folder, concurrent=False):
    before = client.snapshot()
    if concurrent:
        client.command('freeze_local_machine')
    else:
        stop_target(client)
    dwarf = dsym/'Contents/Resources/DWARF/prog'
    timestamps = [(p, p.stat().st_atime_ns, p.stat().st_mtime_ns) for p in (target, dwarf)]
    replacement = images/'replacement'
    shutil.copy2(old_target, replacement)
    os.replace(replacement, target)
    shutil.copyfile(Path(str(old_target)+'.dSYM')/'Contents/Resources/DWARF/old-prog', dwarf)
    for p, access, modified in timestamps:
        os.utime(p, ns=(access, modified))
        assert p.stat().st_mtime_ns == modified
    command = ['launch_and_run', 'query:targets[0]'] if concurrent else 'restart'
    state = client.stopped(command)
    deadline = time.monotonic()+20
    while state['symbols_pending'] and time.monotonic() < deadline:
        time.sleep(.1)
        state = client.snapshot()
    (folder/'replacement-snapshot.json').write_text(json.dumps(state, indent=2))
    frame = state['stack']['frames'][0]
    correct = frame['source']['path'] == str(old) and frame['source']['line'] == 8
    case = 'concurrent-build-replacement' if concurrent else 'preserved-timestamp-replacement'
    if concurrent:
        assert state['process_count'] == 2, state
        assert state['process_id'] != before['process_id'], state
        for observed, expression, expected in ((before, 'first_value', '42'), (state, 'second_value', '84')):
            observed_frame = observed['stack']['frames'][0]
            request = dict(state, process_handle=observed['process_handle'], thread_id=observed['thread_id'])
            deadline = time.monotonic()+10
            while True:
                value = client.evaluate(request, 0, expression, frame_pc=observed_frame['pc'],
                                        unwind_index=observed_frame['unwind_index'], inline_depth=observed_frame['inline_depth'])
                if value['status'] != 'pending' or time.monotonic() >= deadline:
                    break
                time.sleep(.05)
            assert value['status'] == 'ready', value
            decoded = int.from_bytes(bytes.fromhex(value['value']['bytes']), value['value']['byte_order'])
            assert decoded == int(expected), value
    results.append({'case': case, 'status': 'pass' if correct else 'fail', 'frame': frame})
    print('PASS' if correct else 'FAIL', case, frame['source'], flush=True)

probe('in-memory-before-replacement', target, True, replace_and_restart)
probe('replacement-fresh-debugger', target, True, expected_file='old.c', expected_line=8)
shutil.copy2(saved_target, target)
shutil.copyfile(saved/'Contents/Resources/DWARF/prog', dsym/'Contents/Resources/DWARF/prog')
for other_cache in images.glob('*.rdi'):
    if other_cache != cache:
        other_cache.unlink()
probe('concurrent-builds', target, True, lambda c, f: replace_and_restart(c, f, True))

explicit = out/'explicit'
explicit.mkdir()
plain = explicit/'plain.rdi'
plain.write_bytes(original)
compressed = explicit/'compressed.rdi'
run([root/'build/radbin', '--rdi', '--compress', plain, '--out:'+str(compressed)])
assert compressed.stat().st_size < plain.stat().st_size, 'Fixture did not compress'
zero_uuid = bytearray(original)
zero_uuid[top+24:top+40] = bytes(16)
(explicit/'zero-uuid.rdi').write_bytes(zero_uuid)
(explicit/'wrong-uuid.rdi').write_bytes(wrong_uuid)

for name, rdi in (('explicit-plain', plain), ('explicit-compressed', compressed),
                  ('explicit-zero-uuid', explicit/'zero-uuid.rdi'),
                  ('explicit-wrong-uuid', explicit/'wrong-uuid.rdi'),
                  ('explicit-readonly-plain', plain), ('explicit-readonly-compressed', compressed)):
    readonly = 'readonly' in name
    if readonly:
        explicit.chmod(0o555)
        rdi.chmod(0o444)
    try:
        with debugger_client(name) as (client, folder):
            before_hash = hashlib.sha256(rdi.read_bytes()).hexdigest()
            unloaded = client.command(['eval', 'sizeof(IdentityProbe)'])
            assert 'value: "37"' not in unloaded, unloaded
            client.command(['load_debug_info', str(rdi)])
            deadline = time.monotonic()+10
            while True:
                value = client.command(['eval', 'sizeof(IdentityProbe)'])
                if 'value: "37"' in value or time.monotonic() >= deadline:
                    break
                time.sleep(.05)
            (folder/'evaluation.txt').write_text(value)
            assert 'value: "37"' in value, value
            assert 'msgs:  ""' in value, value
            assert hashlib.sha256(rdi.read_bytes()).hexdigest() == before_hash, 'Explicit input was rewritten'
            assert set(explicit.iterdir()) == {plain, compressed, explicit/'zero-uuid.rdi', explicit/'wrong-uuid.rdi'}, 'Unexpected adjacent cache'
            state = client.snapshot()
            assert state['process_count'] == 0, state
            results.append({'case': name, 'status': 'pass', 'input_sha256': before_hash, 'build': state['build']})
            print('PASS', name, flush=True)
    finally:
        if readonly:
            explicit.chmod(0o755)
            rdi.chmod(0o644)

print('DONE', out, flush=True)
assert all(r['status'] == 'pass' for r in results), results
