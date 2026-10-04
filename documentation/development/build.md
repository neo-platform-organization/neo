# Build the universal VM

[VM documentation](../README.md)

From the repository root:

```sh
make
./build/neo help
./build/neo platform
./build/neo --gui neo/shaded-cube.neo cube
```

Requirements for `x86_64/Linux`: C17 compiler, Make, pkg-config, and Xlib development
headers/libraries. `make WITH_X11=0` omits X11 and builds the same `build/neo` path.
GUI requests then return unsupported. CLI and GUI modes share one executable.

The build compiles and links directly to `build/neo`. It leaves no object files,
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
