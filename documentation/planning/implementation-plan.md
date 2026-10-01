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
