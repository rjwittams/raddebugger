"""Exercise RAD's native --ipc sender against finite public-IPC echo servers."""

import pathlib
import subprocess
import sys
import tempfile
import time


def main():
    if len(sys.argv) != 4:
        raise SystemExit('usage: native_client.py RAD IPC_FIXTURE ARTIFACT_DIRECTORY')
    client, fixture = (str(pathlib.Path(arg).resolve()) for arg in sys.argv[1:3])
    artifacts = pathlib.Path(sys.argv[3]).resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ipc-client-', dir=artifacts) as temporary:
        root = pathlib.Path(temporary)
        directory = root / 'registry'
        helpers = []
        try:
            for tag, count in [('A:', 3), ('', 2)]:
                args = [fixture, f'--serve_private:{directory}', f'--serve_count:{count}']
                if tag:
                    args.append(f'--serve_tag:{tag}')
                helper = subprocess.Popen(args,
                                          stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                          stderr=subprocess.PIPE)
                helpers.append(helper)
                deadline = time.monotonic() + 10
                registration = directory / f'{helper.pid}.endpoint'
                while not registration.is_file():
                    if helper.poll() is not None:
                        raise AssertionError(f'fixture exited: {helper.stderr.read()!r}')
                    if time.monotonic() >= deadline:
                        raise AssertionError('fixture did not publish an endpoint')
                    time.sleep(.01)

            common = [client, '--ipc', f'--ipc_dir:{directory}', f'--logs:{root / "logs"}']
            for args in [[], ['--pid:0'], ['--pid:4294967296'],
                         [f'--pid:{helpers[0].pid}', '--ipc_port:7423'],
                         [f'--pid:{helpers[0].pid}', '--ipc_stdin', 'also-an-argument'],
                         [f'--pid:{helpers[0].pid + helpers[1].pid}', '--ipc_stdin']]:
                result = subprocess.run(common + args, input=b'', capture_output=True, timeout=20)
                assert result.returncode != 0 and result.stdout == b'', (args, result.returncode, result.stderr)
            print('PASS native_client_rejections', flush=True)

            def exchange(helper, payload, expected, arguments=None):
                args = common + [f'--pid:{helper.pid}']
                args += ['--ipc_stdin'] if arguments is None else ['--', *arguments]
                result = subprocess.run(args, input=payload, capture_output=True, timeout=20)
                assert result.returncode == 0, (result.returncode, result.stderr)
                assert result.stdout == expected, (len(result.stdout), len(expected))
                helper.stdin.write(b'\n')
                helper.stdin.flush()

            exchange(helpers[0], b'', b'A:echo a;b quoted data', ['echo', 'a;b', 'quoted data'])
            print('PASS native_client_parsed_arguments', flush=True)
            binary = bytes(range(256)) * 17 + b'\x00;\r\n' + 'quoted "text"; λ'.encode()
            exchange(helpers[0], binary, b'A:' + binary)
            print('PASS native_client_binary_bytes', flush=True)
            exchange(helpers[1], b'peer B', b'peer B')
            print('PASS native_client_exact_pid', flush=True)
            exchange(helpers[1], b'', b'')
            print('PASS native_client_empty_reply', flush=True)
            large = bytes(range(251)) * 24000
            exchange(helpers[0], large, b'A:' + large)
            print('PASS native_client_large_reply', flush=True)
            for helper in helpers:
                assert helper.wait(timeout=5) == 0, helper.stderr.read()
        finally:
            for helper in helpers:
                if helper.stdin and not helper.stdin.closed:
                    helper.stdin.close()
                if helper.poll() is None:
                    try:
                        helper.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        helper.kill()
                        helper.wait(timeout=5)
                if helper.stdout:
                    helper.stdout.close()
                if helper.stderr:
                    helper.stderr.close()


if __name__ == '__main__':
    main()
