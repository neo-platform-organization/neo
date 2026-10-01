# Building and extending neo

[Documentation index](../README.md)

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

- Object storage and ancestry queries use linear scans. Child ordering is an ordinal, so copy/serialization can preserve execution order without a separate child-list allocation per object. Large images need measurement and indexed storage.
- Container capabilities cover the subtree. Child lookup preserves rights. Creating contained values preserves destination rights; copying returns the source/destination rights intersection. Named connections carry explicitly delegated rights. Native C callers remain trusted.
- Contexts authenticate an acting object and are cached per identity. Capability and context handles remain until VM destruction. Object identities are never reused inside a VM. Individual handle reclamation is future work.
- ETHER requires explicit host creation/access. Recipient connections need SEND permission. Message policy is fixed at submission; authorized content edits remain possible until acceptance. Accepted payloads are currently immutable to everyone.
- Messages remain contained in ETHER after acceptance and processing. Generic payload access is denied. Individual message reclamation/cancellation and post-acceptance receiver edits remain unimplemented.
- Images containing ETHER, messages, active queues, or enabled/failed registrations cannot yet be duplicated or serialized. The runtime reports unsupported rather than silently dropping state. Ordinary graphs, including behavior objects, round-trip and duplicate.
- The evaluator is a limited scalar subset. Primitive bodies are objects, but activations, temporary values, and failure reports still use private C storage. Full reflective activation objects and language-level recovery policies remain future work.
- Reads/writes target the receiver's immediate contained fields. Active behavior cannot overwrite its own containing code subtree. There is no arbitrary native-code escape or implicit I/O.
- The scheduler has one thread. Registration and primitive behavior do not mutate concurrently. Turns run to completion within a host-provided instruction budget; budget exhaustion is a reported failure, not resumable preemption.
- A failure pauses its receiver. Explicit recovery may retry from the beginning or discard. Prior effects remain, and retry can repeat them. This host mechanism is provisional pending language-level handlers.
- Named siblings must be unique. Strings preserve bytes with a small escape vocabulary; embedded NUL and Unicode escapes are unsupported. Syntax is provisional and unversioned.
- No spatial geometry, dimensional transformation, rendering, parallel VM, external-device model, or self-hosting is implemented. Those are future design work, not capabilities implied by the graph representation.

Keep these limits visible when extending the system. Do not claim complete image persistence or full homoiconicity from the current executable subset.
