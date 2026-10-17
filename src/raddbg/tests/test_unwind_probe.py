"""Exercise the broken-frame probe with simulated IPC transport replies."""
import contextlib
import io
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


class UnwindProbeTests(unittest.TestCase):
    def probe(self, stack_status):
        stop_count, pc = 0, 0x100
        def command(argv, **kwargs):
            nonlocal stop_count, pc
            if argv[0] == 'xcrun':
                return subprocess.CompletedProcess(argv, 0, stdout='100 T _unwind_leaf\n0 T __mh_execute_header\n')
            operation = argv[3]
            reply = ''
            if operation in ('restart', 'run', 'step_into_inst'):
                stop_count += 1
                if operation == 'step_into_inst':
                    pc += 4
            elif operation == 'eval':
                reply = 'value: "20"'
            elif operation == 'add_address_breakpoint':
                reply = '$1'
            elif operation == 'debug_snapshot':
                broken = pc == 0x108
                reply = json.dumps(dict(version=1, status='ready', running=False,
                    stop_count=stop_count, process_count=1, stop={'pc': hex(pc)},
                    stack=dict(status=stack_status if broken else 'ready',
                               frames=[] if broken else [{'module_offset': '0x100'}])))
            elif operation != 'remove_breakpoint':
                raise AssertionError(argv)
            return subprocess.CompletedProcess(argv, 0, stdout=reply, stderr='')

        with tempfile.TemporaryDirectory() as temporary:
            result = Path(temporary)/'result.json'
            script = Path(__file__).with_name('arm64_unwind_ipc.py')
            argv = [str(script), 'sender', '123', '--target', 'fixture', '--broken-frame', '--result', str(result)]
            with patch.object(sys, 'argv', argv), patch('subprocess.run', side_effect=command), \
                 contextlib.redirect_stdout(io.StringIO()), self.assertRaises(SystemExit) as exited:
                runpy.run_path(str(script), run_name='__main__')
            return exited.exception.code, json.loads(result.read_text())

    def test_pending_is_reported_with_nonzero_exit(self):
        status, result = self.probe('pending')
        self.assertEqual(status, 2)
        self.assertEqual(result, {'stack_status': 'pending', 'frame_count': 0})

    def test_error_is_reported_with_success_exit(self):
        status, result = self.probe('error')
        self.assertEqual(status, 0)
        self.assertEqual(result, {'stack_status': 'error', 'frame_count': 0})


if __name__ == '__main__':
    unittest.main()
