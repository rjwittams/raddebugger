# Companion dSYM CLI checks

Build the inputs once on a Mac, using the existing inline C fixture:

```sh
bash src/radbin/tests/build_macho_inputs.sh "$PWD" "$PWD/build/macho-inputs"
python3 src/radbin/tests/run_macho_dsym.py "$PWD/build/radbin" \
  "$PWD/build/macho-inputs" "$PWD/build"
```

The input builder needs Apple's command-line tools. The Python check also runs
on Linux and Windows with those same input files and their UUID receipts.
Pass absolute paths; the Windows executable is `build/radbin.exe`.

The check converts x64/ARM64 executables with companion DWARF 4/5 dSYMs,
then checks the baked RDI's UUID, architecture, procedures, source and inline
site through `radbin --dump`. Two negative companions have a different UUID
or architecture and must contribute no source. It never executes a target.

Run against an ASan build as well: a scratch-backed queued companion path can
pass normal conversion but fail after discovery releases its scratch arena.
Do not pass `--deterministic` to conversion; that mode clears the build UUID.
An empty output with the CLI's explicit no-debug-info diagnostic counts as
rejection, not a successful RDI conversion or a verified error exit status.
