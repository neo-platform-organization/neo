# Initial implementation plan

[Documentation index](../README.md)

## Reading order and scope

Read the applicable AGENTS.md, [architecture.md](../design/architecture.md), [c-conventions.md](../development/c-conventions.md), and [language-design.md](../design/language-design.md). [whitepaper.md](../design/whitepaper.md) is the unchanged vision reference. Architecture records current semantics; the language design contains provisional syntax and primitive choices. Do not interpret acceptance of C naming conventions as acceptance of all language-design proposals.

The object store, initial ETHER messaging, scalar evaluator, sequential scheduler, graph codec, CLI, and executable image example are implemented. neo-vm/ holds C source, headers, examples, and tests; neo/ holds image.neo. See [build-and-test.md](../development/build-and-test.md) for build commands and current limitations. Project guides live in documentation/.

## First milestone: an object store that can be verified

1. Establish a small C17 build, public opaque VM interface, explicit statuses, and a test executable.
2. Implement image instances with independent roots and identity namespaces. Separate internal empty-image construction from the later file loader.
3. Implement primitive payload objects, containment, authorized connections, and safe identity lookup. Primitive payloads are not a non-object language tier.
4. Implement deep duplication with two-pass internal edge remapping. Reject unsupported active state explicitly. Avoid exposing incomplete duplicates on allocation failure.
5. Implement move as successful duplication followed by source listing update and deletion. Invalidate old identities safely; do not implement reparenting under the name move.
6. Test independent copies, nested remapping, preserved permission limits, failed copy cleanup, cycle rejection, stale handles, and isolation between images.

This stage is a substrate, not yet a runnable language. Identify provisional containment and connection policies in the code's API documentation.

## Following milestones

The stages below now have tested, deliberately limited implementations. See [build-and-test.md](../development/build-and-test.md) and [runtime-format.md](../reference/runtime-format.md) for exact coverage. Fully reflective activations, language-level failure policies, runtime-state persistence, and spatial semantics remain open.

1. ETHER messages, authenticated access rules, editable pending content, atomic acceptance, and per-receiver ordering. Queue connections do not transfer message containment.
2. Operation objects, activations, sequential scheduling, and configurable failure handling. Expose unhandled failures without inventing automatic recovery.
3. Reader/writer, validation, image.neo, and host load/start/pause/duplicate/unload/save operations. Loading is inert; starting execution is explicit. Unload does not delete the source file.
4. An executable image demonstration: create independent objects, exchange messages, update environment listings, and consume or move a resource.

Test each milestone before extending it. Document commands and actual limitations when code exists. Do not claim arbitrary running-image persistence, parallel execution, or self-hosting from an initial sequential interpreter.

## Reserved semantic decisions

Keep ETHER reachability, message-policy representation, full acceptance permissions, and the executable primitive vocabulary visibly provisional until resolved. Use the architecture's narrow proposed defaults for a prototype and report them. The unhandled-failure fallback and duplication of active images must never be silent behavior choices.

Odin/Zig migration and a compiled self-hosted neo subset are future options. Preserve that option through semantic image formats and tests, not by adding a compiler before the interpreter works.

## Next design work after the executable base

Read [code-tour.md](../learning/code-tour.md) before extending the runtime. Prioritize a concrete spatial operation and its meaning before assuming the generic interpreter already delivers a spatial environment. Separately, resolve full activation/value representation, accepted-message lifecycle persistence, and image-format versioning. Improve indexed lookup only after profiling; preserve the semantic tests.

## External presentation interface

The first window/buffer boundary is implemented with a generic RGBA buffer and an optional X11 backend. No renderer or maths library was added. neo code can write bytes through granted connections and request presentation. See [window interface](../reference/window-interface.md). Higher-level rendering belongs in neo; general input, bulk buffer operations, resource persistence, and resizing buffer storage remain future work.

## Next priority: portable I/O and display contracts

The user prioritizes abstracting terminal/general I/O and display access before GUI or higher-dimensional presentation. The [I/O and display design](../design/io-and-display.md) records the proposed interfaces, current gaps, and unresolved choice between portable pixel presentation and portable GPU/software rendering commands. Design these contracts before adding more backend-specific primitives. The initial polling byte streams, terminal runner, buffer-dimension queries, and normalized window events are implemented. Resumable I/O, graph-resident completion queues, full text input, a production headless surface, and GPU/software rendering fallback remain pending. See the [I/O reference](../reference/io-interface.md).

## First software rendering option

A sequential neo pixel/line renderer and rotating wireframe cube now exercise the platform-independent buffer interface. Local handler calls, byte-buffer clearing, and explicit host template composition support reuse. This is the initial software path, not a completed GUI renderer or automatic fallback selector. Filled triangles, depth, text, and GPU work remain deferred. See [software renderer](../reference/software-renderer.md).

Profiling-driven identity/parent indexes and packed integer arrays are now implemented. The cube uses array loops; general matrix libraries remain neo-level work. See the build guide for measurements and validation.

## Current priority: OS foundation

The next work is organized into [bounded OS sessions](os-sessions.md). Start with the platform contract for hosted Linux x86_64, before structural primitives. Duplication stays in the kernel under the revised user decision; move retains duplicate-then-delete semantics. Universal dimensionality and maths/spatial protocols will be the first OS packages, with non-graphical validation before GUI implementation. See the [spatial decision](../design/decisions/0002-universal-dimensionality.md). Other platform implementations and self-hosting remain deferred.

OS Session 1 is implemented: portable platform description, hosted Linux x86_64 bootstrap, and metadata queries. Next is service extraction; see [session checkpoint](os-sessions.md#current-checkpoint).

OS Session 2 is implemented: runner loading, standard stream creation, waiting, and optional window creation use platform callbacks. Next is consolidation into one CLI; see [session checkpoint](os-sessions.md#current-checkpoint).

Session 3 launch model was corrected by the user: one `neo` executable, one VM per
process, optional CLI/GUI modes including both together. The updated binary
compiles; further tests were explicitly deferred. Next is reference/structural
protocol design serving the first universal-dimensionality packages.
