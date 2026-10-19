# Public file and directory tests

Run from the repository root:

```sh
bash src/base/tests/run_posix_files.sh
```

In an MSVC developer command prompt:

```bat
src\base\tests\run_windows_files.cmd
```

The six cases use public base APIs, with no native OS calls or mocked providers.
They check create/truncate, append, short reads and EOF, safe iteration of missing
and non-directory paths, and empty/populated directory iteration. Literal payloads
and entry names provide the expected results. Each case runs in its own temporary
working directory with a ten-second timeout, so an EOF loop cannot hang the suite.
The Python runner removes only those temporary directories.

Pass case names to either build script to select a subset. Set
`BASE_FILE_TEST_CFLAGS` for compiler/sanitizer flags. For a negative control,
`BASE_FILE_TEST_SOURCE_ROOT` selects another checkout's public base implementation
without changing the fixture or that checkout.

These tests do not cover all file flags, pipes, mappings, directory filters,
concurrent filesystem changes, or platform-specific root/volume enumeration.
