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

Every parenthesized expression constructs one object. An object without a payload has an ordinary empty payload. Contained objects must have distinct names within their parent. Their source order is preserved. A known primitive with no explicit payload uses its own name as the binding. Use distinct named wrappers for repeated operations in one container, for example `(first (write ...))` and `(second (write ...))`; ordinary sibling names remain unique.

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

The serializer writes bare names, inferred literals (including packed arrays), and generated labels. Host-created names or primitive bindings that cannot be represented as bare tokens report unsupported rather than being quoted or renamed. For known primitives whose names match their bindings it omits `:primitive`; legacy/host-created objects with different names retain an explicit binding to preserve their meaning. Ordinary empty objects with reserved names retain `:object`, so formatting cannot turn data into executable primitives. Re-reading preserves payloads, containment order, connection targets, and rights. It does not preserve numeric object IDs, source spelling, comments, or formatting. Dangling edges are rejected on serialization rather than silently dropped. Version selection is currently implicit in this prototype; a versioned envelope is needed before compatibility is promised.

Limits: 1 MiB source/output, 4096 nodes, 128 levels of containment, and 4096 decoded bytes per token. Out-of-memory or parse failure leaves no published partial image. Failed allocations may consume identity numbers; identity continuity is not a language guarantee.

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

Nonprimitive payload objects evaluate as scalar or packed-array values. The ordinary empty payload acts as unit. General composite values, first-class closures, identity-observable intermediate results, and fully graph-resident activations are not implemented yet. Temporary values and activations currently use private C storage. This is an executable subset, not the full homoiconic execution model.

## Implemented primitive objects

| Binding | Named children / behavior |
| --- | --- |
| call | selector; invoke a local handler with the same receiver, authority, current message, and shared budget. Nested return ends only that call. |
| do | Evaluate contained children in order; return the last result, or unit when empty. |
| if | condition, then, else; evaluate the boolean condition and only the selected branch. |
| while | condition, body; repeat while the condition is true; normal result is unit. |
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

Formatting/duplicating ordinary quiescent images is supported. Images containing streams, host windows/pixel buffers, ETHER, messages, active queues, or enabled/failed registrations are explicitly rejected. Pause/unregister alone cannot make a message-bearing image serializable yet. A future codec must preserve all of that state; silently dropping it is not acceptable.

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
./build/x86_64/Linux/neo --cli neo/neo-os/image.neo browser demo
./build/x86_64/Linux/neo --gui neo/cube.neo cube
./build/x86_64/Linux/neo --cli --gui neo/cube.neo cube
```

Syntax: `neo [--cli] [--gui] IMAGE ACTOR [HANDLER]`.
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
