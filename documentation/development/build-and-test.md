# Building and extending neo

[Documentation index](../README.md)

Quotes always denote text payloads, never identifiers or primitive bindings. Use `(word "if")` for text and `(if ...)` for the primitive. Comments start with `//`; `>` is not a comment marker. The current reader requires bare object names and rejects quoted names. This supersedes earlier quoted-name and quoted-binding syntax.

## Current base

neo now has a small executable C17 interpreter. A separate VM parses a plain object graph, invokes receiver-owned behavior, and runs ETHER messages through a sequential scheduler. The CLI and static library share the same runtime.

Start with [code-tour.md](../learning/code-tour.md) to learn the implementation. [runtime-format.md](../reference/runtime-format.md) specifies the actual provisional image format and executable subset. [architecture.md](../design/architecture.md) remains the broader design, including unimplemented goals.

## Commands

From the repository root (the directory containing Makefile):

```sh
make
make test
make sanitize
./build/neo check neo/image.neo
./build/neo run neo/image.neo counter increment
./build/neo tick neo/image.neo 4
./build/neo format neo/image.neo
./build/neo clone neo/image.neo
make demo
```

Requirements: C17 compiler, Make, an archiver such as ar, and a POSIX shell for CLI tests. No external runtime libraries. The default build enables debug information and strict warnings as errors. Compiler-generated dependency files track header changes. build/libneo.a is the reusable runtime library; build/neo is the executable.

format and clone write an ordinary graph to stdout; they do not overwrite source files. Each CLI invocation starts a fresh VM. check accepts several files and loads them into independent images in the same VM without executing them.

Sanitizer targets compile separate instrumented binaries for AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer cannot run in some traced sandboxes; the verified run used authorized execution outside that environment. Do not equate a blocked sanitizer run with success.

## Dynamic payloads and syntax

The reader infers integers, booleans, quoted text, and packed integer arrays from literals. Payload kinds can change on write; operand kinds are checked during execution. Primitives can bind through their own names, as in `(if ...)`, with named containers forwarding to a sole primitive child. The writer emits inferred literals; legacy explicit tags remain accepted. Tests cover inferred literals, text-versus-number distinction, kind-changing writes, invalid literals, runtime type errors, and serialization.

## Modules

| File relative to repository root | Responsibility |
| --- | --- |
| neo-vm/include/neo.h | Object storage and host image management API |
| neo-vm/include/neo_message.h | Message policy and authenticated context API |
| neo-vm/include/neo_image.h | Inert graph parser and serializer |
| neo-vm/include/neo_execution.h | Behavior invocation, scheduler, failure recovery |
| neo-vm/source/internal.h | Private shared structures and helpers |
| neo-vm/source/object.c | Allocation, capabilities, identity, containment, copy/move/delete |
| neo-vm/source/message.c | ETHER, nested payload access, acceptance, FIFO lifecycle |
| neo-vm/source/image.c | Bounded parsing, two-pass link resolution, serialization |
| neo-vm/source/evaluator.c | Explicit primitive objects, control flow, checked arithmetic |
| neo-vm/source/scheduler.c | Registration, delivery boundaries, turns, explicit recovery |
| neo-vm/source/main.c | Filesystem I/O and host CLI policy |
| neo/image.neo | Executable counter and sender example |

## What is tested

- Independent copies and internal cyclic-edge remapping; retained external authority.
- Duplicate/delete movement, updated containment listings, and safe stale handles.
- Cross-image and cross-VM rejection, attenuation, payload ownership, denied operations.
- Maker-specific message policy, nested edits, generic API bypass rejection, acceptance finality, FIFO, identical requests, deleted recipients.
- Image parsing/serialization, forward and cyclic links, multiple images, primitive tags, malformed input, overflowed literals, source/depth limits.
- Lazy branches, short-circuit booleans, nested return, loops, receiver mutation, integer overflow/division errors, and instruction budgets.
- Ordered scheduler delivery, failure pause, repair/retry and discard, and safe unloading of registered actors.
- Real CLI loading, graph duplication/formatting, message delivery, observable state, and nonzero exits on failure.
- Allocation failure at every allocation in copy/move, image duplication, object creation, message construction/edits/grants, and parser/writer operations.

Strict-warning builds, unit suites, and CLI integration checks pass. Sanitizer suites run outside the traced sandbox. Fault-injection allocators check balanced ownership in addition to sanitizer leak detection.

## Deliberate limitations

These are implementation defaults and gaps, not additional agreed language semantics:

- Identity and parent queries use bucket indexes; ancestry follows indexed parent identities. Whole-region operations and capability validation still use scans. Child ordering remains an ordinal. Fixed bucket counts are not a claim of constant-time lookup for arbitrarily large images.
- Container capabilities cover the subtree. Child lookup preserves rights. Creating contained values preserves destination rights; copying returns the source/destination rights intersection. Named connections carry explicitly delegated rights. Native C callers remain trusted.
- Contexts authenticate an acting object and are cached per identity. Capability and context handles remain until VM destruction. Object identities are never reused inside a VM. Individual handle reclamation is future work.
- ETHER requires explicit host creation/access. Recipient connections need SEND permission. Message policy is fixed at submission; authorized content edits remain possible until acceptance. Accepted payloads are currently immutable to everyone.
- Messages remain contained in ETHER after acceptance and processing. Generic payload access is denied. Individual message reclamation/cancellation and post-acceptance receiver edits remain unimplemented.
- Images containing ETHER, messages, active queues, or enabled/failed registrations cannot yet be duplicated or serialized. The runtime reports unsupported rather than silently dropping state. Ordinary graphs, including behavior objects, round-trip and duplicate.
- The evaluator supports scalars and packed integer arrays, with a limited operation vocabulary. Primitive bodies are objects, but activations, temporary values, and failure reports still use private C storage. Full reflective activation objects and language-level recovery policies remain future work.
- Reads/writes target the receiver's immediate contained fields. Active behavior cannot overwrite its own containing code subtree. There is no arbitrary native-code escape or implicit I/O.
- The scheduler has one thread. Registration and primitive behavior do not mutate concurrently. Turns run to completion within a host-provided instruction budget; budget exhaustion is a reported failure, not resumable preemption.
- A failure pauses its receiver. Explicit recovery may retry from the beginning or discard. Prior effects remain, and retry can repeat them. This host mechanism is provisional pending language-level handlers.
- Named siblings must be unique. Strings preserve bytes with a small escape vocabulary; embedded NUL and Unicode escapes are unsupported. Syntax is provisional and unversioned.
- No spatial geometry, dimensional transformation, rendering, parallel VM, general external-device model, or self-hosting is implemented. Those are future design work, not capabilities implied by the graph representation.

Keep these limits visible when extending the system. Do not claim complete image persistence or full homoiconicity from the current executable subset.

## Optional X11 window interface

`make window` builds `build/neo-window` with Xlib; run `./build/neo-window neo/window.neo display` for the blank presentation image. No renderer is provided. Xlib development files and pkg-config are required for this optional target only. The default runtime and tests remain headless.

`make test` includes fake-backend window/buffer tests, authority and bounds checks, neo primitive invocation, and allocation-failure rollback. `make test-window` explicitly opens a short-lived window and checks pixel readback, resize, and close events. `build/sanitize_display` covers the headless resource boundary; its verified sandbox run disabled leak detection, with allocation-fault tests separately checking balanced VM ownership.

New modules: `neo_display.h`/`display.c` define resource ownership and buffer operations; `neo_window_x11.h`/`window_x11.c` implement the optional backend. Live resources block copy/move/serialization. Xlib connection-loss recovery is not implemented; the basic normalized event subset is described below. See [window interface](../reference/window-interface.md).

The neo-only triangle example runs with `./build/neo-window neo/triangle.neo triangle`. `make test` executes its actual image through a headless backend, checks every RGBA pixel against the triangle boundaries, checks per-frame execution budgets and completed-frame behavior, and writes `build/triangle.ppm` for inspection.

## Streams and normalized events

`make io` builds `build/neo-io`; run `./build/neo-io neo/terminal.neo terminal greet` to write through a granted stdout stream. The optional POSIX adapter lives in `io_posix.c`, while `io.c`/`neo_io.h` define the portable stream boundary. `make test` now builds this runner for output-redirection checks and tests EOF, partial writes, would-block, broken pipes, rights, lifetime, and allocation rollback.

The window callback interface now optionally exposes normalized input events. X11 translates native input into a bounded queue with explicit overflow reporting. Fake-backend tests cover event snapshots and buffer dimensions; `make test-window` covers real translation and queue overflow. ASan/UBSan runs of `sanitize_io` and `sanitize_display` passed with leak detection disabled in the sandbox. See [I/O interface](../reference/io-interface.md) for remaining blocking, text-input, and graph-state limitations.

## Software renderer and cube

`./build/neo-window neo/cube.neo cube neo/software-renderer.neo` runs the neo software line renderer. `make test` includes actual-image renderer/projection checks and local-call execution limits. Sanitizer targets `sanitize_renderer`, `sanitize_runtime`, and `sanitize_display` cover the new paths; verified sandbox runs use `ASAN_OPTIONS=detect_leaks=0`.

`bootstrap.c` implements explicit host-only copying of inert template fields and handlers before startup; it is not a runtime module loader. On bootstrap failure, discard the partially composed image. `buffer-fill` is a byte-storage operation; the rendering algorithms remain in neo. Exact immutable capability grants are reused to avoid per-pixel handle growth. See [software renderer](../reference/software-renderer.md).

## Lookup performance and packed arrays

`make release-window` builds an optimized window runner separately in
`build/release/neo-window`. Run:

```sh
make release-window
./build/release/neo-window neo/cube.neo cube neo/software-renderer.neo
```

The default remains a debug build. Release uses C17, `-O2`, and debug symbols.
Use `make BUILD=build/release CFLAGS='-std=c17 -O2 -g' test` to validate it.

The identity and parent indexes have 1024 collision buckets each. Publication and
freeing maintain them for ordinary creation, parsing, duplication, messages, and
unload. They add bounded bucket storage per VM and two links per object; dense
buckets still take linear search. They neither skip capability checks nor change
creation/serialization order. No new per-lookup allocation or cached value is used.

`test_objects` exercises more live identities/parents than buckets, ordered child
lookup, and deletion with stale handles. `test_arrays` covers integer limits,
empty literals, bounds, permissions, independent copies, dynamic replacement,
serialization, invalid syntax, and allocation-failure cleanup. The existing
message and runtime tests also exercise indexed publication and rollback.

A local `-O0 -pg` profile of the old cube spent about 79% in `neo_child_after`,
10% in `neo_lookup`, and 9% in `neo_child_named`. The original uninstrumented
renderer suite took 10.4 seconds; with indexes it took about 0.21 seconds, before
changing geometry storage. These are whole headless test-suite times, including
setup, seven frames, auxiliary checks, and file output—not window FPS guarantees.
The array-based cube reproduced all four saved reference poses byte-for-byte.

The updated renderer test also reports average frame CPU time separately from setup. A local optimized run averaged about 20 ms over seven frames; this excludes X11 presentation and the host pause. Debug/release suites and ASan/UBSan suites pass (sandbox leak detection disabled).
