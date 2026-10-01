# Executable image format and primitive subset

[Documentation index](../README.md)

This documents the actual v0 implementation. It is a provisional engineering format, not a claim that the earlier syntax proposal or full neo language is settled. The whitepaper remains unchanged. See [language-design.md](../design/language-design.md) for broader proposals and [code-tour.md](../learning/code-tour.md) for a beginner's reading path.

## Graph format

The CLI accepts one rooted object graph per file:

```text
object := '(' name [ '#' label ] [ payload ] { object | edge } ')'
payload := ':object'
         | ':integer' signed_decimal
         | ':boolean' ('true' | 'false')
         | ':text' string
         | ':primitive' string
edge := '@' name '#' label decimal_rights_mask
```

Whitespace separates tokens. `>` begins a comment outside a quoted string. Names and string payloads can be bare tokens or double-quoted strings. Supported escapes are backslash, quote, newline, carriage return, and tab. String storage preserves bytes (including UTF-8 sequences); it does not validate/normalize Unicode, support embedded NUL, or implement Unicode escape notation yet.

Every parenthesized expression constructs one object. An object without a payload has an ordinary empty payload. Contained objects must have distinct names within their parent. Their source order is preserved. The names of behavior steps can be descriptive and unique; their operation is determined by the primitive tag, not by their name.

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

The serializer writes explicit tags and generated labels. Re-reading preserves payloads, containment order, connection targets, and rights. It does not preserve numeric object IDs, source spelling, comments, or formatting. Dangling edges are rejected on serialization rather than silently dropped. Version selection is currently implicit in this prototype; a versioned envelope is needed before compatibility is promised.

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

Only an explicitly invoked body is evaluated. An object named add with an ordinary payload is data. A primitive-tagged object whose binding is add invokes the evaluator's addition rule when evaluated. An unknown primitive fails before evaluating operands.

Nonprimitive payload objects evaluate as scalar values. The ordinary empty payload acts as unit. Composite values, first-class closures, identity-observable intermediate results, and fully graph-resident activations are not implemented yet. Temporary values and activations currently use private C storage. This is an executable subset, not the full homoiconic execution model.

## Implemented primitive objects

| Binding | Named children / behavior |
| --- | --- |
| do | Evaluate contained children in order; return the last result, or unit when empty. |
| if | condition, then, else; evaluate the boolean condition and only the selected branch. |
| while | condition, body; repeat while the condition is true; normal result is unit. |
| return | value; end this invocation, even from a nested operand. |
| read | slot; evaluate a text name and read that immediate child of the receiver. |
| write | slot, value; update the existing receiver child's scalar payload; return its new value. |
| message | path; read a field of the currently processing message. Empty text selects its root payload. |
| send | target, value; use the receiver's named external connection and its `ether` connection to create and submit a message. Result is unit. |
| add / sub / mul / div / rem | left, right; checked signed 64-bit integer arithmetic. |
| lt / le / gt / ge | left, right; integer comparison returning a boolean object value. |
| eq | left, right; scalar value equality with no coercion. |
| and / or | left, right; boolean short-circuit evaluation. |
| not | value; boolean negation. |
| fail | Raise an explicit failure. |

Arguments evaluate left to right, except for the branches/short-circuit forms above. Condition values must be boolean. Integer overflow, division by zero, invalid operand kinds, missing fields, and denied writes return distinct statuses. Division truncates toward zero. The signed-minimum divided or remaindered by -1 reports overflow instead of invoking undefined C behavior.

State read/write is deliberately confined to the receiver's immediate children. Writes cannot overwrite the active behavior's containing subtree or message internals. External writes, object creation/copy/move primitives, local bindings, break/continue, pattern matching, spatial operations, and language-level failure handlers are not in this evaluator subset yet, even where a host C API exists.

## Scheduler and failures

The host registers receivers in order. Each tick first accepts pending messages up to the submission number captured at the boundary. A registered receiver then handles at most one queued message and its optional tick handler. Submissions emitted during a turn wait until a later tick. Other state writes are immediately visible. There is no artificial pacing or parallel execution.

The CLI explicitly registers direct image children containing handlers and supplies each a READ connection to the runtime-created ETHER. Other recipient connections must come from the file. This is a host bootstrap policy, not automatic authority given to arbitrary neo objects. Registration of deeper objects is available through the C API.

An execution budget bounds evaluator operations, and a separate recursion limit protects the C stack. These are host limits, not invisible resumable time slices. Exceeding them reports a failure with receiver ID, operation ID, and step count.

A failed message remains processing and blocks the receiver. Earlier effects remain. The host may inspect/repair the image and explicitly retry from the beginning or discard the failed message through neo_scheduler_recover. A failed periodic tick handler also pauses that receiver; discard disables its registration, and retry enables a later new invocation. No retry policy is chosen on the programmer's behalf. Language-level failure objects and resumable continuations remain future work.

## Persistence boundary

Formatting/duplicating ordinary quiescent images is supported. Images containing ETHER, messages, active queues, or enabled/failed registrations are explicitly rejected. Pause/unregister alone cannot make a message-bearing image serializable yet. A future codec must preserve all of that state; silently dropping it is not acceptable.

The CLI's format/clone commands write text to stdout; neither unload nor formatting deletes or overwrites an input file. External-resource persistence and migration of running native activations are not implemented.
