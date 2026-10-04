# neo

neo is a live object-graph language and environment, implemented by a universal
C17 VM. Objects contain state and behavior and communicate through protected
connections and messages. The current implementation is an interpreter and pixel
window interface; the first OS image now provides prototype browsing, independent cloning,
dimension-independent integer dot products, and point shape validation. General
spatial protocols and a full OS environment remain in development.

## Build and run

Implemented platform: **x86_64/Linux**. Install a C17 compiler, Make, pkg-config,
and Xlib development headers, then run:

```sh
make
make test
./build/x86_64/Linux/neo help
./build/x86_64/Linux/neo platform
./build/x86_64/Linux/neo --gui neo/shaded-cube.neo cube
./build/x86_64/Linux/neo --cli --gui neo/shaded-cube.neo cube
```

The build produces only `build/ARCH/OS/neo`, selected from normalized `uname` output
(default here: `build/x86_64/Linux/neo`). `make -s path` prints it. One process owns one VM. Select `--cli`,
`--gui`, or both to grant terminal and/or display interfaces to the same image.
Without flags, CLI is selected. Launch syntax:

```text
neo [--cli] [--gui] IMAGE ACTOR [HANDLER]
```

`ACTOR` is an immediate image child or `.` for the root. Default handler: `main`
for CLI-only, `frame` for GUI. CLI-only invokes once; GUI polls and invokes until
close or failure. Each invocation has a one-million-step budget. GUI mode currently
supplies a pixel window, not a completed OS GUI. CLI mode is not an interactive shell.

An image contains its entire graph and behavior; there is no file-inclusion or
`--template` mechanism. The package hierarchy and filesystem export/import tool
are future work. Duplication stays in the kernel.

Use `make WITH_X11=0` for a CLI-only provider build. `make targets` lists the
architecture/OS selections; `make TARGET=x86_64/Linux` selects the implemented
one. The other targets are explicit unsupported stubs. `/neo` means future
bare-metal execution; `wasm32`/`wasm64` mean WebAssembly runtime execution.

## Repository

```text
neo-vm/
  objects/       object storage, capabilities, duplication
  messages/      message authority and lifecycle
  image/         inert graph parsing and serialization
  execution/     evaluator and scheduler
  display/       protected buffers and windows
  io/            protected byte streams
  platform/      abstraction, architecture/OS targets, POSIX and X11 adapters
  cli/           argument dispatch and launch
  main.c         process entry point
  internal.h     private shared VM structures
neo/             image files and isolated language documentation
documentation/   VM guides and C interface documentation
build/ARCH/OS/neo universal executable
```

Each subsystem colocates its C source and header. Preserved `.neo` sources from
removed test/build directories are under `neo/archive/`. Collaboration metadata
lives outside this repository in `brain/`; editor tooling lives outside it too.

Comparisons are prefix objects: `(== (left 1) (right 1))`,
`(!= (left 1) (right 2))`, `(< (left 1) (right 2))`, and `>`, `<=`, `>=`.
Quoted symbols are text. Comments start with `//`.

Read the [VM guides](documentation/README.md),
[language syntax](documentation/reference/syntax-cheat-sheet.md), and
[runtime contracts](neo/documentation/runtime-format.md).
[Doxygen](https://www.doxygen.nl/manual/markdown.html) can turn Markdown and C
API comments into a browsable manual using `doxygen Doxyfile`.

Licensed under [GPLv3](LICENSE).

## Try Project Substrate

```sh
./build/x86_64/Linux/neo --cli neo/neo-os/image.neo browser
./build/x86_64/Linux/neo --cli neo/neo-os/image.neo browser demo
```

The demo clones prototypes, computes a 4D dot product (70), and validates a 4D
point while leaving the 3D prototype unchanged. All behavior is in one image.
See [the OS guide](documentation/reference/project-substrate.md) and
[protected graph operations](documentation/reference/graph-operations.md).
`make test` uses the `selftest` subcommand; it creates no other executable.
