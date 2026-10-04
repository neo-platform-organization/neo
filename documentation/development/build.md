# Build the universal VM

[VM documentation](../README.md)

From the repository root:

```sh
make
make test
./build/x86_64/Linux/neo help
./build/x86_64/Linux/neo platform
./build/x86_64/Linux/neo --gui neo/shaded-cube.neo cube
```

Requirements for `x86_64/Linux`: C17 compiler, Make, pkg-config, and Xlib development
headers/libraries. `make WITH_X11=0` omits X11 and builds the same target-specific executable path.
GUI requests then return unsupported. CLI and GUI modes share one executable.

The default target is derived from `uname -m` and `uname -s`, normalized to the
architecture/OS directory names. The build compiles and links directly to
`build/$(TARGET)/neo` (`build/x86_64/Linux/neo` here). It leaves no object files,
static archives, alternate runners, or configuration stamps. `make clean` removes
build output, preserving any `.neo` files a user has placed there. The default uses
strict warnings and debug symbols. `make CFLAGS='-std=c17 -O2 -g'` builds optimized.
Compilation replaces the executable only after success.

## Target selection

```sh
make targets
make TARGET=x86_64/Linux
make x86_64/Linux
```

Target definitions live in `neo-vm/platform/ARCH/OS/platform.mk`. Only
`x86_64/Linux` is implemented. Other target definitions fail with an explicit stub
message; they do not produce dummy binaries. `/neo` denotes future bare-metal
execution. `wasm32` and `wasm64` denote execution inside a WebAssembly runtime.

The executable has one VM per invocation. Runtime behavior and its limits are
specified in [the language reference](../../neo/documentation/runtime-format.md).

## Artifacts and regression checks

`make -s path` prints the selected executable path. `TARGET=ARCH/OS` overrides the
host default for explicit cross builds; it does not implement a missing backend.
Both the temporary linker output and final binary live under the selected target.
`clean` covers the complete build tree and preserves `.neo` files. `/build/` in
`.gitignore` already covers nested targets and needed no change.

No object files, static archive, or separate test executables are produced. They
were removed in the earlier restructuring and remain absent. `make test` now runs
`neo selftest` inside the universal binary, including the actual OS seed image.
Run it from the repository root. It needs no display server; fake output is used.

Verified: default and `WITH_X11=0` builds pass the regression checks. The same binary
also passed with AddressSanitizer/UndefinedBehaviorSanitizer (sandbox leak detection
disabled; tracked allocator checks balanced ownership). Restore the ordinary build
with `make` after changing instrumentation flags.
