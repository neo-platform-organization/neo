# Launch and language milestones

This work continues in the development repository. The separate
neo-foundation-snapshot repository remains frozen.

## Agreed boundary

The C VM owns parsing, translation, native execution, graph memory, package
loading, service enforcement, execution lifecycles, the REPL, and the preOS
launcher with its portable GUI/CLI. Application behavior belongs in neo.
The current sample is deliberately a small text tool, not an editor.

Every execution has an image. A minimal deployment and a full environment can
contain the same package. A host window presents an existing application
instance in that image; it does not imply a new image or instance. Internal
presentation is the default unless the developer declares otherwise.

## Milestones

| Milestone | Deliverable and completion gate |
| --- | --- |
| M1: Package and launch contract | Inert program metadata, explicit package boundary, minimal-image export, and the same sample behavior/state in full and minimal environments. Initial self-contained package slice implemented. Dependency resolution is M4. |
| M2: Shared service access | Graph-facing I/O, arguments, directories, streams, timers, graphics, input, networking, clipboard, audio, and supported process services. Grants and delegation checked at use time. Host-window capability and ownership are explicit. |
| M3: Execution lifecycles and REPL | Fresh script execution, persistent image execution, event-loop lifetime, explicit application instantiation, and fresh/attached REPL. Add readiness acknowledgements, recoverable initialization, output/log capture, clean startup, structured arguments, and explicit launcher supervision. |
| M4: Packages and persistence | Dependency/version resolution, inert compatibility checks, initialization, safe graph publication, durable checkpoints, and authority reconstruction. Add image provenance, immutable templates, exact runtime/artifact pins, template creation, and staged recreation preserving the prior world. |
| M5: Launcher backend and CLI | Multiple roots, inert discovery, organization, and separate image/app instance tracking. Add named profiles, Open once/Register/Import choices, image details, reproducible launch plans, running registry, graceful/forced stop, and persisted settings. |
| M6: Portable launcher GUI | Images, Discover, Settings, and image-library browsing over the same CLI operations. Add profile selector, Copy command, template creation, Running view, logs, and a visible settings persistence model. |
| M7: Standalone library | Boot-bundled discovery and execution, with read-only/temporary/persistent storage capabilities. Runtime selection and running records cannot depend on a host process ID or shell. Persistence requires the missing storage driver. |
| M8: Remote catalogs | Configurable sources/providers, verified staged downloads, offline immutable template cache, safe installation, pinned builds, private-source credentials, and proxy support. Runtime acquisition is separate from image download. Standalone operation requires networking and storage support. |
| M9: Small application proof | Same text tool runs through script, REPL, minimal image, and full environment. Window placement preserves instance identity; additional instances have independent state. Export both deployments with declared assets, runtime, launch configuration, notices, and integrity manifest; test relocation on a clean machine. |
| M10: Language completion gate | Publish and pass the Warp, Weft, and Knight version-1 specifications and conformance suites described below. No language can remain a sample-only subset while this gate is marked complete. |

Language work runs alongside the launcher milestones. M10 is an explicit final
acceptance gate, not a claim that the preceding UI work completes a language.
Missing language mechanisms must be implemented when their milestone needs
them, rather than replaced by application behavior in C.

## Pharo Launcher additions

The [Pharo Launcher review](pharo-launcher-review.md) records documentation
and pinned implementation sources, feature comparisons, and acceptance tests.
These additions are planned, not features implemented by M1.

The main addition is a lifecycle distinction: catalog artifact, immutable
template, mutable installed world, and running session are separate records.
Packages, application instances, and windows remain separate inside a session.
Named profiles configure a launch; they do not create those identities.

Compatibility is always inspected without execution. Local profiles cannot
grant authority beyond their invoking user's configured rights. Recreate stages
a replacement and preserves the old world. Cached artifacts are identified by
digest rather than by a mutable URL or display name. Imported whole directories
are an explicit option, while declared bundle contents are the default.

Exact-build/commit acquisition belongs in M8. Automated regression bisection is
a useful later diagnostic, not a blocker for the initial launcher. A large
standalone runtime-management UI is also unnecessary at first: detect installed
compatibility and explain missing requirements before adding remote acquisition.

## Language version-1 completion gate

A language is complete relative to an explicit versioned specification, not
relative to how many demonstrations work. The gate requires:

- Warp: documented graph identity, references, containment/delegation rules,
  values, mutation, messages, object lifecycle, validation, serialization,
  persistence, and controlled live replacement, with failure semantics.
- Weft: stable grammar and diagnostics, with faithful authoring/projection of
  every public executable construct in the version-1 language. Any graph-only
  data must have a documented lossless representation. No silent information
  loss during edit, lower, project, or reopen.
- Knight: specified pure function and value semantics, composition, recursion,
  collections, numeric behavior, and type/contract checks. Effects cannot leak
  through calls or reflective views. It is not a promise of Haskell compatibility.
- Interpretation and reflection: default metadata and intentional alternate
  views have precise supported meanings, validation, and authority rules.
  Inspection is inert. An unsupported interpretation fails explicitly.
- Shared foundation: usable text and collection operations, memory management,
  errors and recovery, modules, host service calls, source locations, and live
  updates work beyond the tiny sample. Resource limits are documented and
  tested; existing small token limits cannot be concealed as general text support.
- Independent conformance suites exercise each construct, boundaries, invalid
  input, and equivalent Warp/Weft/Knight representations where applicable.
  Supported target profiles are explicit, including standalone numeric gaps.
- The text application is written solely against the published package/service
  contract. Production VM code contains no text-tool behavior or expected result.

Exact version-1 feature scope still needs a written language specification.
The earlier philosophical proposals are not automatically implemented semantics.
Higher-order and type-contract details must be settled there and tested, not
implicitly invented by launcher code. M10 stays open until that scope is resolved
and implemented. Current Warp, Weft, and Knight are working subsets.

## M1 implementation

Launch intent uses ordinary graph slots on an explicitly packaged object:

```lisp
(slot entry-selector "start")
(slot presentation "internal")
(slot instances "optional")
```

`entry-selector` marks the program and names a parameterless handler.
`presentation` defaults to internal and accepts internal, host, or headless.
`instances` defaults to single; optional declares that additional instances
are intended. Metadata declares intent, not resource access or implementation
of window management. It is preserved by existing Warp/Weft source views.

```text
neo launch-info ../neo-image/neo-os/image.neo text-tool
neo package ../neo-image/neo-os/image.neo text-tool
neo program-check ../neo-image/neo-os/image.neo text-tool
```

The first command is inert inspection. The second prints a minimal Warp image
containing exactly the declared package and current values. It validates the
result without invoking the program. Packages with unresolved external
references/imports are rejected; automatic dependency resolution is not added
silently. Export carries no host grants or native resources.

The diagnostic explicitly opens full and minimal test worlds, invokes the same
entry twice in each, and compares results plus serialized package state. This
is a deployment test, not application instantiation and not the finished script
runner. Program metadata does not create a host window yet.

The text-tool sample counts UTF-8 bytes and records how often it was used.
It has no file format, editor, widgets, or OS dependencies. An empty text input
and a populated input are tested through ordinary session sends. Pure counting
and arithmetic are Knight; application state and message handling are graph
handlers. The existing exercise objects remain acceptance fixtures and are
not the future OS desktop.

## Current development checkpoint

The implementation is now split across independent sibling surface, depth,
and image repositories. See [the boundary](three-repositories.md).
M2/M3 have a tested vertical slice: scoped launch arguments, asynchronous
byte-stream requests, script execution, full/minimal launch, and REPL interaction
with that same running image. The sample remains a small byte-counting tool.

This does not complete M2 or M3. General host filesystem/directory access,
resource constructors and delegation at the image layer, remote REPL attachment,
application-instance ownership, complete close/exit protocols, and window
routing remain open. Other existing platform resources can be explicitly bound
by trusted C code to byte channels, but this is not a finished graph service API.

Depth code accepts graph requests atomically into a queue; external I/O happens
later. Completion handlers receive payload bytes, status, transferred length,
and a request ID. Partial stream writes are continued in order. Revocation,
stale graph revisions, failed sends, and shutdown have acceptance tests.
Current reads are at most 31 bytes, text arguments remain bounded tokens, and
request deadlines are currently five seconds once submitted. These limits
remain language-completion work, not a finished general-purpose I/O system.
