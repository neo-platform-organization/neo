# Executable image format and primitive subset

[Language documentation](README.md)

This documents the implemented image syntax, including inferred literals and name-selected primitives. It is a provisional engineering format, not a claim that the earlier syntax proposal or full neo language is settled. The whitepaper remains unchanged. See [the VM code tour](../../documentation/learning/code-tour.md) for implementation details.

## Graph format

The CLI accepts one rooted object graph per file:

```text
object := '(' name [ '#' label ] [ payload ] { object | edge } ')'
payload := signed_decimal | 'true' | 'false' | string | '[' { signed_decimal } ']'
         | ':primitive'
         | legacy_tagged_payload
edge := '@' name '#' label decimal_rights_mask
```

Whitespace separates tokens. `//` begins a comment outside a quoted string, ending at newline. `>` has no comment meaning. Object names, connection names, labels, and primitive bindings are bare tokens, never quoted strings. Double quotes always denote text payloads: `(word "if")` contains text, while `(if ...)` selects an operation. The current format requires named objects, so `("if")` is rejected; anonymous literal objects have no source notation yet. Supported escapes are backslash, quote, newline, carriage return, and tab. String storage preserves bytes (including UTF-8 sequences); it does not validate/normalize Unicode, support embedded NUL, or implement Unicode escape notation yet.

Payload types are dynamic: `(n 7)`, `(ready true)`, and `(name "Alice")` infer their current payload kinds from literals. A write may change the kind; operations check compatibility when executed. Quote text: `"7"` is text, while `7` is an integer. Bare unknown words and floating-point literals are rejected. Explicit scalar tags remain readable for compatibility but are not emitted by the writer.

`(if ...)` selects the `if` operation from the object's name. No `:primitive` marker is needed. The reader binds recognized operation names when no explicit payload is present. For example, `(add 7)` is integer data; `(add :object)` is an explicitly ordinary object. Unknown names remain ordinary objects. Loading never runs an operation. Explicit `(body :primitive if ...)` is readable; quoted operation bindings are rejected. An unknown binding loads inertly and fails if evaluated.

Every parenthesized expression constructs one object. An object without a payload has an ordinary empty payload. Ordinary containers require distinct child names. A container bound to primitive `do` permits repeated child names because it evaluates children in order: `(do (write (slot "n") (value 1)) (write (slot "n") (value 2)))`. Named wrappers remain valid. Named lookup of repeated children reports conflict; index-based access remains available. Rebinding such a sequence to ordinary data requires removing duplicate names before serialization. A data object merely named `do` does not receive this exception.

Labels are file-local names for identities, not runtime IDs or global lookup authority. Edges can point forward or form cycles. Containment cannot form cycles through this syntax. The loader resolves links after allocating all nodes. Nothing executes during loading.

Edges are named separately from contained fields. The final integer records these image-local permissions:

| Bit | Permission |
| --- | --- |
| 1 | READ |
| 2 | WRITE |
| 4 | INSERT |
| 8 | COPY |
| 16 | DELETE |
| 32 | DELEGATE |
| 64 | ACT (establish an acting context) |
| 128 | SEND |

Combine bits by addition when writing the file. A receiver connection for sending only therefore has mask 128. These links cannot identify an object outside the new image or grant filesystem/process authority. Loading an image is a host operation that establishes its internal grants; ordinary running code cannot forge them by manufacturing integer IDs.

The serializer writes bare names, inferred literals (including packed arrays), and generated labels. Host-created names or primitive bindings that cannot be represented as bare tokens report unsupported rather than being quoted or renamed. For known primitives whose names match their bindings it omits `:primitive`; legacy/host-created objects with different names retain an explicit binding to preserve their meaning. Ordinary empty objects with reserved names retain `:object`, so formatting cannot turn data into executable primitives. Re-reading preserves payloads, containment order, connection targets, and rights. It does not preserve numeric object IDs, source spelling, comments, or formatting. Dangling edges are rejected on serialization rather than silently dropped. Ordinary formatting remains unversioned; live saves use the versioned checkpoint envelope described below.

Limits from `neo-vm/resource_limits.h`: 4 MiB source/output, 131072 nodes, 128 levels of containment, and 4096 decoded bytes per token. Out-of-memory or parse failure leaves no published partial image. Failed allocations may consume identity numbers; identity continuity is not a language guarantee.

## Behavior schema

An actor's handlers are contained in its child named `handlers`. A handler is a child named for a message selector. Its child named `body` is the operation to evaluate.

```text
actor
  count: integer object
  handlers
    increment
      body: primitive object
        named argument objects
```

A named container with an ordinary empty payload and exactly one primitive child forwards evaluation to that child. This supports `(body (if ...))`, `(condition (< ...))`, and `(value (read (slot "count")))`. It applies only during evaluation; merely containing operations does not execute them. Other nonprimitive objects evaluate to copies of their payloads. Use `do` for sequences.

Only an explicitly invoked body is evaluated. An object named add with an explicit literal payload or `:object` is data; `(add ...)` with no explicit payload is a primitive. A primitive-tagged object whose binding is add invokes the evaluator's addition rule when evaluated. An unknown primitive fails before evaluating operands.

For `if` and `while`, a named `condition` takes precedence. Otherwise the first
child is the predicate, provided it is not a branch named `then`, `else` or `body`.
For example `(if (has-child (target (self)) (name "ready")) (then 1) (else 0))`
evaluates that boolean operation directly. Missing predicates and nonboolean
results fail; branches retain lazy evaluation. Existing wrapped forms still work.

Nonprimitive payload objects evaluate as scalar or packed-array values. The ordinary empty payload acts as unit. General composite values, first-class closures, identity-observable intermediate results, and fully graph-resident activations are not implemented yet. Temporary values and activations currently use private C storage. This is an executable subset, not the full homoiconic execution model.

## Implemented primitive objects

| Binding | Named children / behavior |
| --- | --- |
| call | selector; invoke a local handler with the same receiver, authority, current message, and shared budget. Nested return ends only that call. |
| do | Evaluate contained children in order; return the last result, or unit when empty. |
| if | condition, then, else; evaluate the boolean condition and only the selected branch. When condition is absent, the first child may be the predicate directly. |
| while | condition, body; repeat while the condition is true; normal result is unit. When condition is absent, the first child may be the predicate directly. |
| return | value; end this invocation, even from a nested operand. |
| read | slot; evaluate a text name and read that immediate child of the receiver. |
| write | slot, value; update the existing receiver child's scalar payload; return its new value. |
| message | path; read a field of the currently processing message. Empty text selects its root payload. |
| send | target, value; use the receiver's named external connection and its `ether` connection to create and submit a message. Result is unit. |
| add / sub / mul / div / rem | left, right; checked signed 64-bit integer arithmetic. |
| `<` / `<=` / `>` / `>=` | left, right; integer comparison returning a boolean object value. |
| `==` / `!=` | left, right; value equality/inequality with no coercion; arrays compare contents. |
| and / or | left, right; boolean short-circuit evaluation. |
| not | value; boolean negation. |
| fail | Raise an explicit failure. |

Arguments evaluate left to right, except for the branches/short-circuit forms above. Condition values must be boolean. Integer overflow, division by zero, invalid operand kinds, missing fields, and denied writes return distinct statuses. Division truncates toward zero. The signed-minimum divided or remaindered by -1 reports overflow instead of invoking undefined C behavior.

State read/write is deliberately confined to the receiver's immediate children. Writes cannot overwrite the active behavior's containing subtree or message internals. Explicit reference-based inspection and structural operations are described in [graph operations](../../documentation/reference/graph-operations.md). Local bindings, break/continue, pattern matching, general spatial protocols, and language-level failure handlers remain unimplemented.

## External window and buffer primitives

Host-granted resources support `buffer-fill`, `buffer-read`, `buffer-write`, `buffer-size`, `window-present`, `window-poll`, `window-width`, and `window-height`. See the [window interface](../../documentation/reference/window-interface.md) for operands, rights, RGBA layout, ownership, and X11 limits. These are byte access and external presentation operations; no renderer or maths library is implemented in the backend. Loading an image alone never grants display access.

## Stream and event interface

`stream-read` and `stream-write` operate only on host-granted stream connections. `buffer-width`/`buffer-height` query pixel dimensions. `window-next-event` consumes a normalized event; `window-event` reads its last snapshot. See [I/O interface](../../documentation/reference/io-interface.md) for operands, permissions, EOF/would-block results, terminal setup, and event fields. The first implementation polls synchronously; it does not add resumable I/O or move the scheduler into the graph.

## Scheduler and failures

The host registers receivers in order. Each tick first accepts pending messages up to the submission number captured at the boundary. A registered receiver then handles at most one queued message and its optional tick handler. Submissions emitted during a turn wait until a later tick. Other state writes are immediately visible. There is no artificial pacing or parallel execution.

The CLI explicitly registers direct image children containing handlers and supplies each a READ connection to the runtime-created ETHER. Other recipient connections must come from the file. This is a host bootstrap policy, not automatic authority given to arbitrary neo objects. Registration of deeper objects is available through the C API.

An execution budget bounds evaluator operations, and a separate recursion limit protects the C stack. These are host limits, not invisible resumable time slices. Exceeding them reports a failure with receiver ID, operation ID, and step count.

A failed message remains processing and blocks the receiver. Earlier effects remain. The host may inspect/repair the image and explicitly retry from the beginning or discard the failed message through neo_scheduler_recover. A failed periodic tick handler also pauses that receiver; discard disables its registration, and retry enables a later new invocation. No retry policy is chosen on the programmer's behalf. Language-level failure objects and resumable continuations remain future work.

## Persistence boundary

The local manager uses protected package primitives documented in
[local packages](../../documentation/reference/packages.md). Validation/import
remain inert. Their names are ordinary primitive bindings, not new grammar.

The OS image also provides an opt-in environment service: ordinary handlers own
workspace/edit/invocation operations and save-readiness policy. The terminal
selects it with `service PATH` and invokes it with `request ACTION`. This adds no
new language primitives or grammar. See
[environment services](../../documentation/reference/environment-services.md).

Formatting/duplicating ordinary quiescent images is supported. Ordinary formatting
and duplication reject ETHER/messages/queues, resources, and enabled/failed native
registrations. The separate `neo_image_checkpoint` encoder used by live `save`
supports ETHER, message state/policy and queue order between invocations. Its
`!ether`, `!message` and `!grant` metadata load inertly through the reader. Native
registrations, external resources and C activation stacks remain unsupported.
Reopen paused and explicitly select the image driver; active processing requests
require an explicit recovery decision. Checkpoint output starts with
`!neo-checkpoint 1`; unknown versions are rejected before graph allocation.
Legacy unversioned input remains readable. See the
[checkpoint contract](../../documentation/reference/checkpoints.md).

The CLI's format/clone commands write text to stdout; neither unload nor formatting deletes or overwrites an input file. External-resource persistence and migration of running native activations are not implemented.

## Software renderer

The [cube renderer](software-renderer.md) is contained in a single image. The VM
does not include or compose additional source files at launch.

## Packed integer arrays

`(vertices [-80 -80 -80 80 -80 -80])` creates one object with a contiguous signed
64-bit integer payload. Components have no separate identity or child objects.
`[]` is empty; whitespace and `//` comments separate entries. Strings, nested
arrays, and noninteger components are rejected. Brackets are now token delimiters
and cannot appear in bare names. The reader limits each array to 131,072 elements,
subject also to its existing source-size limit.

| Operation | Example | Result |
| --- | --- | --- |
| Allocate a filled array | `(array-new (size 100) (value 0))` | Owned packed integer value; nonnegative length, checked storage size, one additional execution step per element. |
| Read an element | `(array-get (slot "vertices") (index 2))` | Integer at zero-based index 2. |
| Write an element | `(array-set (slot "vertices") (index 2) (value 80))` | Store and return 80; requires WRITE. |
| Element count | `(array-size (slot "vertices"))` | Integer count; requires READ. |

These primitives address immediate receiver fields and retain containment authority
checks. Negative/out-of-range indices fail with `NEO_INVALID`; noninteger indices,
values, or nonarray targets fail with `NEO_WRONG_KIND`. Missing fields are unavailable.
Active behavior cannot be changed through array writes. Operand evaluation and early
returns follow the other primitives. Arrays have fixed length; ordinary `write`
can replace the whole payload, including changing its kind or length.

Ordinary `read`, `write`, returned values, messages, object copying copy array contents where values are duplicated; they do not introduce
shared mutable backing storage. `==` compares array contents. Parsing/formatting
preserves entries, including empty arrays and signed integer limits. The C API's
`neo_object_read` returns borrowed storage, as it does for text; `neo_array_get`,
`neo_array_set`, and `neo_array_size` provide checked element access.

Matrix dimensions and operations belong to neo code. The cube treats its vertex
array as eight rows of three coordinates, using `row * 3 + column`; the kernel
only stores and accesses integers. There is no native matrix or rendering primitive.

## Platform metadata

`(platform-info (field "environment"))` queries the VM's host-configured platform.
Text fields: `environment`, `host-os`, `architecture`, `virtualization`, `backend`.
Boolean provider fields: `host-files`, `byte-streams`, `pixel-windows`, `input-events`, `wait`.
These are informational, never resource grants. An unconfigured VM reports
unavailable; unknown fields are invalid. See [platform contract](../../documentation/reference/platform-interface.md).

## Launch modes

One `neo` process owns one VM. The same executable handles CLI, GUI, or both:

```sh
make
./build/x86_64/Linux/neo --cli neo/neo-os/image.neo
./build/x86_64/Linux/neo --gui neo/cube.neo cube
./build/x86_64/Linux/neo --cli --gui neo/cube.neo cube
```

Syntax: `neo [--cli] [--gui] IMAGE [ACTOR [HANDLER]]`.
Normal execution is simply `neo main.neo`. When ACTOR is omitted, an optional
root field `(entry "app")` selects the immediate child `app`; otherwise the image
root is the receiver. CLI invokes `main`, GUI invokes `frame`. The entry field is
inert data: `check`, parsing and package import do not run it. Missing handlers,
invalid entry values or absent entry targets fail explicitly. Explicit ACTOR and
HANDLER override defaults for debugging. Supplied executable images declare their
entry, so `neo neo/neo-os/image.neo` and `neo --gui neo/cube.neo` work.
Mode flags precede the image. Without mode flags, CLI is selected. `ACTOR` is an
immediate child name or `.` for the image root. The default handler is `main` in
CLI-only mode, `frame` when GUI is selected. An explicit handler overrides it.

CLI grants standard streams and invokes the handler once. GUI grants a fixed
640×480 pixel buffer and a window, invoking the handler until window close or
failure, with the existing host pacing. Both flags grant both resource sets to
the same receiver and use the GUI loop; they do not create a second VM or run a
second handler. There is no interactive command shell yet. Blocking stream reads
can block the frame loop. Each invocation currently has a one-million-step budget.

One image owns its code and packages. Application output is emitted only through
its granted streams. No result/state banner is added by launch mode.

`help`, `platform`, `check`, `format`, `clone`, `run`, and `tick` remain utility
arguments of the same executable. `run` retains its diagnostic result/state output
and grants no streams. `platform` reports compiled providers, not object grants or
a guarantee that a display server is currently reachable.

Linux builds include X11 by default; `make WITH_X11=0` builds the same binary path
without it, making GUI requests fail as unsupported. GUI mode currently exposes
the existing display interface, not the future dimensional OS GUI. Universal
spatial packages must precede that GUI's implementation.

## Temporary reference results

Graph primitives can return a temporary capability reference (`NEO_REFERENCE` in C).
It has no literal syntax and cannot be persisted as an ordinary payload or message.
`same` compares live identities; `==`/`!=` reject reference operands. Protected graph
connections remain the persisted relationship representation. See the
[graph contract](../../documentation/reference/graph-operations.md) and
[OS image guide](../../documentation/reference/project-substrate.md).

## Live terminal session

`neo live IMAGE` keeps one image loaded for inspection, editing, invocation, and
explicit scheduling. See the [live image guide](../../documentation/reference/live-image.md) for commands,
a working counter experiment, authority boundaries, and save limitations. The
operator interface remains native C; partial message checkpoints are supported
between invocations, with the restrictions above.

## Image-controlled recovery

`attempt` has `body` and `recover` operands. It evaluates `recover` only after an
ordinary body failure. `failure` with text field `status`, `operation`, or `receiver`
reads the dynamically scoped diagnostic (text status or integer identity). These
identities cannot be used as capabilities. `receiver-reference` and
`operation-reference` additionally require a `target` operand containing an existing
READ capability to the location or its container; the resulting reference grants
only READ plus already-held DELEGATE. Unrelated scopes and manufactured IDs fail.
For missing nested handlers/malformed dispatch selectors, the receiver is the
diagnostic location because no failing handler instruction exists.
`rethrow` preserves the caught status
and location. Recovery may return a replacement value, mutate with its existing
authority, fail differently, or explicitly propagate. Previous mutations remain.

Nested recovery restores the outer failure scope. Calls from recovery share that
scope. The host step/depth budget and allocation exhaustion bypass recovery;
recovery never receives a fresh budget. Success after recovery is handler success,
so the existing scheduler can complete its message. This is non-resumable recovery,
not full inspectable activation/failure objects or runtime-state persistence.

## Message lifecycle and explicit release

The [message/failure contract](../../documentation/reference/message-lifecycle.md)
defines the implemented states, participant-loss behavior and recovery matrix.
`(message-release (target REFERENCE))` reclaims processed or unsubmitted payloads,
or payloads whose receiver is gone, under authenticated cleanup authority. Message
rights are READ=1, EDIT=2, INSPECT=4, RELEASE=8, ALL=15; old masks retain their meaning.
Live pending/accepted/processing work cannot be released. The future recipient
needs an explicit RELEASE grant to abandon a created request. There is no automatic
cancellation, retry, acknowledgement or cleanup. Old links become unavailable and
need explicit disconnection before checkpointing. `message-info` exposes both
`sender-available` and `receiver-available` under INSPECT authority.

Networking currently exists only as an optional trusted platform provider.
Attached connections use the existing stream operations and explicit grants;
explicit host endpoint grants enable `network-open`, `network-ready`,
`network-port`, `network-stream` and `network-close`. There is no remote ETHER
replication yet.
See [transport scope](../../documentation/reference/network-interface.md).

Explicit snapshot operations now include `replica-channel`, `replica-prepare`,
`replica-send`, `replica-receive` and `replica-import`. They operate on ordinary
selected graph regions; receipt and import do not invoke behavior. See the
[replication contract](../../documentation/reference/replication-interface.md).

The optional [exposure package](../packages/exposure.md) builds publication and
update policy from ordinary objects/handlers above these primitives. Its default
revision order is editable image behavior; receive does not apply updates.

`(authenticated-peer (target STREAM_REFERENCE))` queries the verified immediate
peer certificate fingerprint of a TLS stream, requiring READ. It fails for
plaintext/unavailable streams. Only trusted host `net-grant-tls` provisions the
local identity/expected pin; image data cannot downgrade it. See
[authenticated networking](../../documentation/reference/authenticated-networking.md).


`(network-finish (target STREAM_REFERENCE))` requires WRITE on a native network
stream. False requests another nonblocking poll; true reports completed local
write shutdown. TLS sends close-notify first. New nonempty writes are then denied;
reads remain available until deletion/remote termination. Complete any pending
TLS application write before calling finish (otherwise work pending/out of order).
This is not remote receipt or application acceptance. See
[network shutdown](../../documentation/reference/network-interface.md#explicit-local-write-shutdown).


`(replica-ended (target CHANNEL_REFERENCE))` returns whether replica receive has
observed EOF between complete frames. It performs no I/O; requires channel ACT
and its retained stream READ. `replica-receive` returns false both while pending
and after boundary EOF; use this query to distinguish them. Partial-frame EOF and
transport failures still raise errors. No snapshot import or acceptance follows
from an ended observation. See [boundary EOF](../../documentation/reference/replication-interface.md#frame-boundary-eof).

Image-library note: [affine transforms](../packages/affine.md) are ordinary
package behaviors, not new evaluator syntax or primitives. They use dimension-
driven arrays and existing checked integer operations; frame conversion remains
future work.

The [space/frame package](../packages/frames.md) implements parent traversal,
identity checks and coordinate conversion using existing graph and affine
operations. These are library contracts; no frame syntax or automatic authority
is added to the evaluator.

The runtime loads a single image source as the object graph. Local package
artifacts can be explicitly imported into that graph and retained in its saved
checkpoint; package operations are graph import/installation, not runtime source
module evaluation. [Geometry](../packages/geometry.md) executes literal behaviors
already present in that image, using existing array/integer primitives.

[Axis projections](../packages/projection.md) are image-owned derived views linked
to source geometry. Editing uses existing protected array/payload operations and
preserves hidden coordinates; no projection primitive or runtime source loading
is added. Saved images retain the source and view identities/connections.

[Coordinate-band selection](../packages/slices.md) runs as ordinary image behavior,
using checked arrays/integers and explicit source authority. It publishes derived
visibility/source-index maps in the same loaded graph; no slice primitive or
external source-module evaluation is added.

[Segment sections](../packages/section.md) generate derived geometry through literal
image behaviors and existing checked arithmetic/array primitives. The source
relationship and provenance remain in the same graph/checkpoint; there is no
section primitive or external source-module execution.

[Triangle sections](../packages/triangle-sections.md) use existing literal image
behavior, checked integer arrays and explicit source authority. Source and derived
geometry remain in the same graph; no triangle primitive or source-module loader
is added. Per-invocation work bounds still apply to dimension-driven algorithms.

[General affine views](../packages/linearview.md) are literal image behaviors over
existing integer/array/authority primitives. Lower-dimensional edits require an
explicit caller-selected lift and validation; no implicit inverse primitive or
source-module runtime is introduced. Source and derived views share one graph.

[Perspective views](../packages/perspective.md) use existing image integer/array
operations, explicit READ source authority and guarded division. Vertex clipping
codes are derived graph state, not a new native primitive or complete topology
clipping mechanism. All artifacts and instances persist in one image source.

The [headless host provider](../../documentation/reference/headless-display.md)
uses existing buffer/window/event primitives. Its injection/controller handles are
trusted host APIs, never image values. It adds no headless syntax, text/composition
primitive, source loader or second live graph.

`window-capture` takes target connection text and boolean value, requiring WRITE.
`window-state` takes target and field (`focused`, `captured`, `input-epoch`), requiring
READ on the cached state. `window-event` now also exposes epoch; event kinds add
input-reset and capture. Poll before consuming. See [reset contract](../../documentation/reference/input-reset.md)
for unqueued reset priority, stale filtering, image-owned cancellation and
unsupported native-provider behavior.

Committed input adds the `text` event kind and `window-event` field `text`.
The payload is validated UTF-8, 1–63 bytes; other event kinds expose empty text.
Symbolic key commands remain separate. See [text input](../../documentation/reference/text-input.md)
for Windows surrogate decoding, byte/control limits and pending IME/X11 support.

`window-state` also exposes `scale-numerator`, `scale-denominator`,
`presentation-lost` and `surface-epoch`. A coalesced `surface` event follows
input-reset and precedes provider input when poll observes a new surface
generation. Coordinates and buffer sizes remain drawable pixels; image behavior
owns layout, retry and redraw policy. See [surface state](../../documentation/reference/surface-state.md).

Windows selftest additionally validates these existing interfaces against hidden
native windows and GDI bitmap output. No new primitive or image syntax is added.
See [native validation scope](../../documentation/reference/native-display-validation.md).

The core rejects provider input epochs above INT64_MAX before publishing state,
matching the image integer/reset contract. Malformed provider responses preserve
the last successful snapshot. See [presentation audit](../../documentation/reference/presentation-audit.md)
for the current mechanism coverage and incomplete cross-platform exit gate.

The [projected point renderer](../packages/point-renderer.md) uses existing graph
inspection, integer/array and buffer/window primitives. Its view and source remain
in the same saved graph; rendering neither loads modules nor adds renderer syntax.

The [topology-aware line renderer](../packages/line-renderer.md) likewise uses
ordinary graph inspection, checked arithmetic and buffer primitives. Full-row
view mappings preserve source topology identity; viewport raster clipping adds no
native operation or graph-source loader. Homogeneous clipping is still pending.

The separate [segment clipper](../packages/segment-clipper.md) now supplies
homogeneous edge clipping through existing checked arithmetic/array primitives.
Generated topology and exact original-edge parameters persist as ordinary graph
fields. Triangle clipping is still pending; no new primitive or source syntax.

[Filled triangle rendering](../packages/triangle-renderer.md) likewise uses ordinary
checked integer, graph, array and buffer primitives. Coverage, source-indexed
screen-depth interpolation and owner/depth arrays belong to the image; they add no
native drawing syntax. Full-row input is required until triangle clipping exists.
# GUI graph behavior

`text-bytes` exposes UTF-8 bytes as an integer array; `text-concat` joins text.
`to-text` can inspect a primitive's inert binding identifier without executing it.
The GUI packages use these operations for bitmap labels and graph inspection.
See [GUI environment](../../documentation/reference/gui-environment.md) and
[desktop model](../packages/desktop-model.md) for the current application protocol
and the separate desktop policy model, hosted by the image desktop session.
# Image-owned desktop session

The canonical seed now has `gui-entry "desktop"` alongside terminal `entry
"browser"`. Its desktop behavior allocates private app surfaces, manages window
records, composes buffers and routes local input. See
[desktop](../packages/desktop.md) and
[primitive mechanism contracts](../../documentation/reference/private-surfaces.md).
# Current desktop workflow

The desktop actor owns a single top bar and app launcher; selecting an app visits
its canvas window through the viewport. Task Manager is an ordinary windowed app
with read-only model access and explicit focus/close requests. Windows cannot be
minimized. Navigation remains pan plus wheel/pinch zoom. See
[desktop](../packages/desktop.md) and [Task Manager](../packages/task-manager.md).

## Editable source and text values

The runtime now exposes generic UTF-8 text slicing/splicing and cursor/line helpers,
source-text, source-check and protected source-apply. Syntax remains the literal
neo object graph. Replacement preserves the selected root identity and rejects
active code, stale baselines, malformed source and live resources. attempt may
reserve a child budget; execution-budget reports remaining work. See the
[complete mechanism contracts](../../documentation/reference/source-editing.md)
and [image text editor](../packages/text-editor.md). GUI editing is image behavior.
