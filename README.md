# neo

neo is an experimental language and runtime built around a live world of objects. Objects own their internal behavior; prototypes are examples to duplicate, rather than sources of shared behavior through delegation. The long-term goal is a consistent spatial computing environment.

neo is **dynamically typed**: `(count 0)` infers an integer payload, and later writes may change its kind. Primitive objects use forms such as `(if ...)`.

The implementation is **C17**. A separate VM loads `neo/image.neo` as an object graph. Loading is inert; behavior runs only when invoked or scheduled.

## Try it

Requirements: a C17 compiler, Make, `ar`, and a POSIX shell for the integration tests. No external runtime libraries.

From this directory:

```sh
make
make test
./build/neo check neo/image.neo
./build/neo run neo/image.neo counter increment
./build/neo tick neo/image.neo 4
```

The direct call returns `1`. The tick example ends with `counter.count = 3` and `sender.sent = true`: a sender creates a message, the counter accepts it on the next tick, and its own behavior handles the request.

Additional commands:

```sh
./build/neo format neo/image.neo
./build/neo clone neo/image.neo
make demo
make sanitize
```

`format` and `clone` print a graph to stdout; neither overwrites the input. Each invocation loads a fresh image. `check` accepts multiple files. Use `make clean` to remove `build/`.

## Current scope

- Independent images, checked object identities, and limited-authority connections.
- Containment, deep copying, and duplicate-then-delete movement.
- ETHER message objects with maker-defined policy and atomic acceptance.
- Ordered per-receiver queues and a sequential scheduler.
- Graph parsing/serialization, explicit primitive objects, conditions, loops, arithmetic, state access, and message submission.
- Failure reports and explicit host-controlled repair/retry or discard.
- Unit, CLI, allocation-failure, and sanitizer tests.

This is a small runnable interpreter, not yet the complete language or a spatial OS. Geometry, dimensional transformations, rendering, parallel execution, fully reflective activations, and persistence of running/message-bearing images remain unimplemented. Unsupported runtime-state duplication is rejected explicitly.

## Code map

```text
build/               Generated binaries, library, and object files
documentation/       Learning, reference, development, design, and plans
neo-vm/
  include/           Public C embedding APIs
  source/            VM, object store, messaging, codec, and execution
  examples/          C embedding example
  tests/             Semantic, failure-path, and CLI tests
neo/
  image.neo          Executable object graph
.gitignore
LICENSE
Makefile
README.md
```

The build also produces `build/libneo.a` for C embedding.

## Reading and design guides

Start at the [documentation index](documentation/README.md). The guides are grouped by purpose:

- [Code tour](documentation/learning/code-tour.md): start here to learn how the system works.
- [Implemented format](documentation/reference/runtime-format.md): exact syntax and primitive subset.
- [Development](documentation/development/build-and-test.md): build commands, policies, tests, and limitations.
- [Architecture](documentation/design/architecture.md): requirements, rationale, and open decisions.
- [Language proposals](documentation/design/language-design.md), [implementation plan](documentation/planning/implementation-plan.md), and [C conventions](documentation/development/c-conventions.md).
- [Whitepaper](documentation/design/whitepaper.md): broader vision; some details have since been refined.

C is the initial host language. A future Odin/Zig port or compiled self-hosted subset remains possible; image semantics are kept separate from native memory layouts.

## License

GNU General Public License, version 3.0 only (`GPL-3.0-only`). See [LICENSE](LICENSE).

The optional [X11 window interface](documentation/reference/window-interface.md) presents a pixel buffer filled by neo. Build it with `make`, then run `./build/neo --gui neo/window.neo display`. The supplied image is a blank presentation surface, not a renderer.

The [stream and input interface](documentation/reference/io-interface.md) keeps native platform details behind granted objects. Run `make` and `./build/neo --cli neo/terminal.neo terminal greet` for the terminal example.

For the [neo software renderer and rotating wireframe cube](documentation/reference/software-renderer.md), run `./build/neo --gui neo/cube.neo cube --template neo/software-renderer.neo` after `make`. All geometry, projection, and line rasterization are written in neo.
