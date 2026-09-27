"""Runner failure-path tests; no debugger or cleat server required."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import run_smoke


class SmokeRunnerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.debugger = self.root/'debugger'

    def invoke(self, case='c', *extra):
        argv = ['run_smoke.py', '--debugger', str(self.debugger),
                '--output-root', str(self.root), '--case', case, *extra]
        with patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()):
            status = run_smoke.main()
        records = [json.loads(p.read_text()) for p in self.root.glob('rad-smoke-*/result.json')]
        return status, records

    def test_missing_debugger_is_failure_not_uncovered(self):
        status, records = self.invoke()
        self.assertEqual(status, 1)
        self.assertEqual(records[0]['status'], 'fail')
        self.assertIn('FileNotFoundError', records[0]['error'])

    def test_unsupported_explicit_case_is_skipped_not_passed(self):
        with patch('platform.system', return_value='Linux'):
            status, records = self.invoke('symbol-library')
        self.assertEqual(status, 2)
        self.assertEqual(records[0]['status'], 'skipped')

    def external_command(self, command, **kwargs):
        if command[0] == 'git':
            if kwargs.get('stdout') == subprocess.PIPE:
                return subprocess.CompletedProcess(command, 0, stdout=b'')
        elif command[:3] == ['cleat', 'launch', '--help']:
            kwargs['stdout'].write('--tag\n')
        elif command[0] == 'fixture-compiler':
            if '-o' in command:
                (Path(kwargs['cwd'])/command[command.index('-o')+1]).write_bytes(b'fixture')
            for arg in command:
                if arg.startswith('/Fe:'):
                    Path(arg[4:]).write_bytes(b'fixture')
        elif command[:2] == ['xcrun', 'dsymutil']:
            pass
        else:
            raise FileNotFoundError(command[0])
        return subprocess.CompletedProcess(command, 0)

    def test_missing_cleat_is_failure(self):
        self.debugger.write_bytes(b'debugger')
        def missing_cleat(command, **kwargs):
            if command[0] == 'cleat':
                raise FileNotFoundError('cleat')
            return self.external_command(command, **kwargs)
        with patch('subprocess.run', side_effect=missing_cleat):
            status, records = self.invoke()
        self.assertEqual(status, 1)
        self.assertIn('cleat', records[0]['error'])

    def test_missing_optional_language_compiler_remains_uncovered(self):
        self.debugger.write_bytes(b'debugger')
        script = self.root/'examples/language-basics/build.py'
        script.parent.mkdir(parents=True)
        script.write_text('# stand-in examples builder\n')
        def unavailable_compiler(command, **kwargs):
            if command[0] == sys.executable:
                (Path(kwargs['cwd'])/'build.json').write_text(json.dumps(
                    {'status': 'uncovered', 'reason': 'odin is not installed'}))
                return subprocess.CompletedProcess(command, 2)
            return self.external_command(command, **kwargs)
        with patch('subprocess.run', side_effect=unavailable_compiler):
            status, records = self.invoke('language-odin', '--examples', str(self.root))
        self.assertEqual(status, 2)
        self.assertEqual(records[0]['status'], 'uncovered')
        self.assertEqual(records[0]['reason'], 'odin is not installed')

    def test_missing_required_compiler_is_failure(self):
        self.debugger.write_bytes(b'debugger')
        with patch('subprocess.run', side_effect=self.external_command):
            status, records = self.invoke('c', '--compiler', 'missing-compiler')
        self.assertEqual(status, 1)
        self.assertIn('missing-compiler', records[0]['error'])

    def test_language_without_examples_is_setup_failure(self):
        self.debugger.write_bytes(b'debugger')
        with patch('subprocess.run', side_effect=self.external_command):
            status, records = self.invoke('language-odin')
        self.assertEqual(status, 1)
        self.assertIn('--examples', records[0]['error'])

    def test_batch_platform_skips_do_not_hide_setup_failures(self):
        with patch('platform.system', return_value='Linux'):
            status, records = self.invoke('all')
        self.assertEqual(status, 1)
        by_case = {r['case']: r for r in records}
        self.assertEqual(by_case['c']['status'], 'fail')
        self.assertEqual(by_case['symbol-library']['status'], 'skipped')

    def test_timed_out_launch_cleans_up_owned_session(self):
        self.debugger.write_bytes(b'debugger')
        sessions = set()
        def launch_timeout(command, **kwargs):
            if command[:2] == ['cleat', 'launch'] and '--help' not in command:
                sessions.add(command[2])
                raise subprocess.TimeoutExpired(command, kwargs['timeout'])
            if command[:2] == ['cleat', 'capture']:
                return subprocess.CompletedProcess(command, 0)
            if command[:2] == ['cleat', 'kill']:
                sessions.remove(command[2])
                return subprocess.CompletedProcess(command, 0)
            return self.external_command(command, **kwargs)
        with patch('subprocess.run', side_effect=launch_timeout):
            status, records = self.invoke('c', '--compiler', 'fixture-compiler')
        self.assertEqual(status, 1)
        self.assertIn('TimeoutExpired', records[0]['error'])
        self.assertEqual(sessions, set(), 'launch left its owned session running')

    def test_cleanup_failure_keeps_original_launch_error(self):
        self.debugger.write_bytes(b'debugger')
        def launch_and_cleanup_fail(command, **kwargs):
            if command[:2] == ['cleat', 'launch'] and '--help' not in command:
                raise subprocess.TimeoutExpired(command, kwargs['timeout'])
            if command[:2] in (['cleat', 'capture'], ['cleat', 'kill']):
                return subprocess.CompletedProcess(command, 1)
            return self.external_command(command, **kwargs)
        with patch('subprocess.run', side_effect=launch_and_cleanup_fail):
            status, records = self.invoke('c', '--compiler', 'fixture-compiler')
        self.assertEqual(status, 1)
        self.assertIn('TimeoutExpired', records[0]['error'])
        self.assertIn('CalledProcessError', records[0]['session_cleanup_error'])

    def unreadable_stack(self, stack_status):
        self.debugger.write_bytes(b'debugger')
        def pending_stack(command, **kwargs):
            if command[0] == 'powershell':
                kwargs['stdout'].write(json.dumps({'ProcessId': 123, 'ExecutablePath': str(self.debugger.resolve())}))
                return subprocess.CompletedProcess(command, 0)
            if command[0] == str(self.debugger.resolve()):
                snapshot = dict(version=1, status='ready', process_count=0, build={})
                return subprocess.CompletedProcess(command, 0, stdout=json.dumps(snapshot), stderr='')
            if command[:2] == ['cleat', 'inspect']:
                kwargs['stdout'].write(json.dumps({'process': {'leader_pid': 123}}))
                return subprocess.CompletedProcess(command, 0)
            if command[0] == 'cleat' and '--help' not in command:
                return subprocess.CompletedProcess(command, 0)
            if command[0] == sys.executable:
                if '--result' in command:
                    Path(command[command.index('--result')+1]).write_text(json.dumps(
                        {'stack_status': stack_status, 'frame_count': 0}))
                return subprocess.CompletedProcess(command, 2 if stack_status == 'pending' else 0)
            return self.external_command(command, **kwargs)
        with patch('platform.system', return_value='Darwin'), \
             patch('platform.machine', return_value='arm64'), \
             patch('subprocess.run', side_effect=pending_stack):
            return self.invoke('unwind-unavailable', '--compiler', 'fixture-compiler')

    def test_unreadable_stack_pending_is_uncovered_not_passed(self):
        status, records = self.unreadable_stack('pending')
        self.assertEqual(status, 2, records)
        self.assertEqual(records[0]['status'], 'uncovered')
        self.assertEqual(records[0]['unreadable_stack']['stack_status'], 'pending')

    def test_unreadable_stack_error_is_passed_and_recorded(self):
        status, records = self.unreadable_stack('error')
        self.assertEqual(status, 0, records)
        self.assertEqual(records[0]['unreadable_stack']['stack_status'], 'error')

    def test_unreadable_stack_ready_is_failure(self):
        status, records = self.unreadable_stack('ready')
        self.assertEqual(status, 1, records)


if __name__ == '__main__':
    unittest.main()
