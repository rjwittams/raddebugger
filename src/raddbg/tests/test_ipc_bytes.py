"""Lossless IPC byte-string decoding; no live debugger required."""
import base64
import json
from pathlib import Path
import sys
import unittest

from ipc_client import ipc_bytes


class IPCByteTests(unittest.TestCase):
    def test_text(self):
        for text in ('', 'worker', '\x00\n"\\', 'é😀'):
            self.assertEqual(ipc_bytes(text), text.encode('utf-8'))

    def test_bytes(self):
        self.assertEqual(ipc_bytes({'base64': 'd29ya2Vy/w=='}), b'worker\xff')
        for length in range(257):
            original = bytes(range(256))[:length]
            encoded = base64.b64encode(original).decode('ascii')
            self.assertEqual(ipc_bytes({'base64': encoded}), original)

    def test_reject_malformed(self):
        for value in (None, 42, {}, {'base64': 1}, {'base64': '/w==', 'extra': 1},
                      {'base64': '/w'}, {'base64': '/w==\n'}, {'base64': '_w=='},
                      {'base64': '/x=='}, {'base64': 'é'}, '\ud800'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                ipc_bytes(value)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: test_ipc_bytes.py PATH_TO_byte_strings.jsonl')
    records = Path(sys.argv[1]).read_text(encoding='utf-8').splitlines()
    assert len(records) == 257, 'Incomplete native byte-string vectors'
    for size, line in enumerate(records):
        record = json.loads(line)
        original = bytes(range(256))[:size]
        assert bytes(record['input']) == original
        assert ipc_bytes(record['encoded']) == original
        assert isinstance(record['encoded'], str) == (size <= 128)
    print('257 native JSON byte-string round trips passed')
