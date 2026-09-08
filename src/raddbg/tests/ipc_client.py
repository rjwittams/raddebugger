"""Serialized request/reply client for isolated RAD regression sessions."""
import json
import os
import re
import subprocess
import time


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
