"""Bread-and-butter language checks through RAD's IPC interface."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys


LANGUAGES = ['c', 'cpp', 'rust', 'odin', 'zig']


class Uncovered(Exception):
    """The examples builder explicitly reported unavailable language coverage."""


def build(examples, language, output, run, record):
    if examples is None:
        raise FileNotFoundError('Language cases require --examples pointing to raddebugger_examples')
    examples = examples.resolve()
    record['examples_revision'] = run(['git', '-C', str(examples), 'rev-parse', 'HEAD'], timeout=10).strip()
    record['examples_status'] = run(['git', '-C', str(examples), 'status', '--short'], timeout=10)
    script = examples/'examples/language-basics/build.py'
    record['examples_builder_sha256'] = hashlib.sha256(script.read_bytes()).hexdigest()
    try:
        run([sys.executable, str(script), language, '--output', str(output)], timeout=180)
    except subprocess.CalledProcessError as error:
        if error.returncode == 2 and (output/'build.json').exists():
            metadata = json.loads((output/'build.json').read_text())
            if metadata.get('status') == 'uncovered':
                record['language_build'] = metadata
                raise Uncovered(metadata['reason']) from error
        raise
    metadata = json.loads((output/'build.json').read_text())
    record['language_build'] = metadata
    if metadata.get('status') == 'uncovered':
        raise Uncovered(metadata['reason'])
    return metadata


def line(source, marker):
    matches = [i for i, text in enumerate(source.read_text().splitlines(), 1) if marker in text.split()]
    assert len(matches) == 1, (marker, matches)
    return matches[0]


def exercise(client, source, metadata):
    def function_name(frame):
        name = frame['function']
        # Zig's native DWARF backend puts these declarations in module "main";
        # the LLVM-backed fixtures can emit them without that enclosing scope.
        return name.removeprefix('main::') if metadata['language'] == 'zig' else name

    def at(state, function, marker=None):
        frame = state['stack']['frames'][0]
        expected = metadata['leaf' if function == 'matrix_leaf' else 'caller']
        assert function_name(frame) == expected, state
        assert frame['source']['is_source'] and Path(frame['source']['path']) == source, frame
        if marker:
            assert frame['source']['line'] == line(source, marker), frame

    def value(state, frame, expression, expected):
        result = client.evaluated(state, frame, expression)
        assert result['status'] == 'ready', result
        assert int.from_bytes(bytes.fromhex(result['value']['bytes']), 'little', signed=True) == expected, result

    def enter_body(state):
        # Function breakpoints can stop in the prologue before parameter spills.
        for _ in range(4):
            at(state, 'matrix_leaf')
            frame = state['stack']['frames'][0]
            if frame['source']['line'] == line(source, 'MATRIX_LEAF'):
                return state
            assert frame['source']['line'] < line(source, 'MATRIX_LEAF'), frame
            current = client.stopped('step_over_line')
            assert current['stack']['frames'][0]['pc'] != frame['pc'], current
            state = current
        raise AssertionError(('Did not step past function prologue', state))

    # First prove the function breakpoint actually fires, independently of Into.
    stopped = client.stopped('restart')
    at(stopped, 'matrix_caller', 'MATRIX_CALL')
    assert stopped['stop']['cause'] == 'breakpoint', stopped
    value(stopped, 0, 'seed', 20)
    source_breakpoint = stopped['stop']['breakpoint_id']
    leaf = client.stopped('run')
    at(leaf, 'matrix_leaf')
    assert leaf['stop']['cause'] == 'breakpoint', leaf
    assert leaf['stop']['breakpoint_id'] != source_breakpoint, leaf
    function_breakpoint = leaf['stop']['breakpoint_id']
    leaf = enter_body(leaf)
    assert [function_name(f) for f in leaf['stack']['frames'][:2]] == [metadata['leaf'], metadata['caller']], leaf
    value(leaf, 1, 'seed', 20)
    for marker, expected in (('MATRIX_UPDATE', 40), ('MATRIX_RETURN', 42)):
        current = client.stopped('step_over_line')
        at(current, 'matrix_leaf', marker)
        assert current['stack']['frames'][0]['pc'] != leaf['stack']['frames'][0]['pc'], current
        # The first body line can precede the parameter's valid location range.
        value(current, 0, 'input', 20)
        value(current, 0, 'doubled', expected)
        if marker == 'MATRIX_RETURN':
            assert current['stop']['cause'] == 'breakpoint', current
            assert current['stop']['breakpoint_id'] not in (source_breakpoint, function_breakpoint, '0'), current
        leaf = current
    stopped = client.stopped('step_out')
    at(stopped, 'matrix_caller')
    value(stopped, 0, 'seed', 20)
    end_sequence = stopped['last_process_end']['sequence']
    pid = stopped['process_id']
    ended = client.stopped('run')
    assert ended['process_count'] == 0, ended
    assert ended['last_process_end'] == dict(sequence=end_sequence+1, process_id=pid, exit_code=0), ended

    # A second launch checks source Into while the same function breakpoint stays enabled.
    stopped = client.stopped('restart')
    at(stopped, 'matrix_caller', 'MATRIX_CALL')
    stopped = client.stopped('step_into_line')
    stopped = enter_body(stopped)
    stopped = client.stopped('step_over_line')
    at(stopped, 'matrix_leaf', 'MATRIX_UPDATE')
    value(stopped, 0, 'doubled', 40)
    stopped = client.stopped('run')
    at(stopped, 'matrix_leaf', 'MATRIX_RETURN')
    assert stopped['stop']['cause'] == 'breakpoint', stopped
    ended = client.stopped('run')
    assert ended['process_count'] == 0 and ended['last_process_end']['exit_code'] == 0, ended
