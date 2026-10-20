import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

radbin, inputs, output = map(pathlib.Path, sys.argv[1:])

def run(arguments):
    result = subprocess.run([str(radbin), *arguments], capture_output=True,
                            text=True, timeout=30)
    if result.returncode:
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        result.check_returncode()
    return result.stdout

for machine, arch in [('x86_64', 'X64'), ('arm64', 'ARM64')]:
    for version in [4, 5]:
        name = f'{machine}-dwarf{version}'
        expected_uuid = re.search(r'UUID: ([0-9A-F-]{36})',
                                  (inputs / f'{name}.uuid.txt').read_text())[1]
        destination = output / f'mac9-macho-{name}.rdi'
        # Deterministic mode deliberately erases the build UUID; use normal
        # conversion here so this check observes the real companion identity.
        run(['--rdi', str(inputs / name), f'--out:{destination}'])
        dump = run(['--dump', str(destination)])
        assert re.search(r'^\s*arch:\s+' + arch + r'\s*$', dump, re.M), name
        assert re.search(r'^\s*guid:\s+' + expected_uuid + r'\s*$', dump, re.M), name
        for procedure in ['leaf', 'main']:
            assert re.search("'" + procedure + r"':\s*// procedures\[[1-9]\d*\]", dump), (name, procedure)
        assert 'inline.c' in dump and re.search(r'// source_file\[[1-9]\d*\]', dump), name
        assert re.search(r"name:\s*'wrapper'.*// inline_site\[[1-9]\d*\]", dump), name
        print('PASS companion dSYM CLI conversion and UUID:', name)

# Copy only fixture files to new directories; never alter the canonical inputs.
# A nonmatching companion must not contribute source/procedures to the output.
for label, wrong_name in [('uuid', 'arm64-dwarf4'), ('arch', 'x86_64-dwarf5')]:
    with tempfile.TemporaryDirectory(prefix='mac9-macho-negative-', dir=output) as tmp:
        tmp = pathlib.Path(tmp)
        executable = tmp / 'target'
        shutil.copyfile(inputs / 'arm64-dwarf5', executable)
        companion = tmp / 'target.dSYM' / 'Contents' / 'Resources' / 'DWARF'
        companion.mkdir(parents=True)
        shutil.copyfile(inputs / f'{wrong_name}.dSYM' / 'Contents' / 'Resources' / 'DWARF' / wrong_name,
                        companion / 'target')
        destination = tmp / 'negative.rdi'
        result = subprocess.run([str(radbin), '--rdi', str(executable),
                                 f'--out:{destination}'], capture_output=True,
                                text=True, timeout=30)
        if destination.exists() and destination.stat().st_size > 0:
            dump = run(['--dump', str(destination)])
            assert not re.search(r"'(leaf|main)':\s*// procedures\[[1-9]\d*\]", dump), label
            assert 'inline.c' not in dump, label
        else:
            assert result.returncode != 0 or 'Could not load debug info' in result.stderr, (label, result.stdout, result.stderr)
        print('PASS mismatched companion contributes no source:', label)
