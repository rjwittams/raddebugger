"""Serialized request/reply client for isolated RAD regression sessions."""
import base64
import binascii
import json
import os
import re
import subprocess
import time


def ipc_bytes(value):
    """Decode an IPC byte-string explicitly; never replace invalid bytes."""
    if isinstance(value, str):
        return value.encode('utf-8')
    if not isinstance(value, dict) or set(value) != {'base64'} or not isinstance(value['base64'], str):
        raise ValueError('Expected an IPC string or {base64: string}')
    try:
        result = base64.b64decode(value['base64'], validate=True)
    except (ValueError, binascii.Error) as error:
        raise ValueError('Invalid IPC base64') from error
    if base64.b64encode(result).decode('ascii') != value['base64']:
        raise ValueError('Expected canonical padded IPC base64')
    return result


class Client:
    def __init__(self, sender, pid, trace=None):
        self.sender, self.pid = str(sender), int(pid)
        self.trace = trace or os.environ.get('RAD_IPC_TRACE')

    def command(self, command):
        args = command.split() if isinstance(command, str) else command
        result = subprocess.run([self.sender, '--ipc', f'--pid:{self.pid}', *args],
                                capture_output=True, text=True, timeout=20)
        if self.trace:
            with open(self.trace, 'a', encoding='utf-8') as stream:
                stream.write(json.dumps(dict(time=time.time(), command=args,
                                            stdout=result.stdout, stderr=result.stderr,
                                            returncode=result.returncode)) + '\n')
        result.check_returncode()
        return result.stdout

    def snapshot(self):
        raw = self.command('debug_snapshot')
        try:
            result = json.loads(raw)
        except ValueError as error:
            raise AssertionError(f'debug_snapshot did not return JSON: {raw!r}') from error
        assert result['version'] == 1, result
        assert result['status'] != 'too_large', 'debug_snapshot exceeds the v1 transport limit'
        return result

    def stopped(self, command, timeout=20):
        before = self.snapshot()
        self.command(command)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = self.snapshot()
            if (state['status'] == 'ready' and not state['running']
                    and state['stop_count'] > before['stop_count']
                    and (command != 'restart' or state['process_count'] > 0)):
                return state
            time.sleep(.03)
        raise AssertionError(f'{command} did not produce a fresh stop: {state}')

    def value(self, expression):
        # Legacy eval remains GUI-frame based until frame-explicit evaluation.
        raw = self.command(['eval', expression])
        match = re.search(r'value:\s*"([^\"]*)"', raw)
        assert match and match[1], raw
        return match[1]

    def evaluate(self, stop, frame, expression, **overrides):
        request = {key: stop[key] for key in ('run_gen', 'stop_count', 'mem_gen', 'reg_gen',
                                              'process_handle', 'thread_id')}
        request.update(frame=frame, expr=expression)
        frame_info = next((f for f in stop['stack']['frames'] if f['id'] == frame), {})
        request.update(frame_pc=frame_info.get('pc', '0x0'),
                       unwind_index=frame_info.get('unwind_index', 0), inline_depth=frame_info.get('inline_depth', 0))
        request.update(overrides)
        payload = ', '.join(f'{key}: {json.dumps(value, ensure_ascii=False)}'
                           for key, value in request.items())
        raw = self.command(['debug_eval', payload])
        try:
            result = json.loads(raw)
        except ValueError as error:
            raise AssertionError(f'debug_eval did not return JSON: {raw!r}') from error
        assert result['version'] == 1, result
        assert result['complete'] == (result['status'] != 'pending'), result
        if result['status'] != 'ready':
            assert result['value'] is None, result
        return result

    def evaluated(self, stop, frame, expression):
        deadline = time.monotonic()+10
        while True:
            result = self.evaluate(stop, frame, expression)
            if result['status'] != 'pending' or time.monotonic() >= deadline:
                return result
            time.sleep(.03)
