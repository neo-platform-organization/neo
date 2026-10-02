# neo syntax cheat sheet

[Documentation index](../README.md) · [Full runtime reference](runtime-format.md) · [Example image](../../neo/image.neo)

This covers the currently implemented image language. Examples are neo source fragments; operations execute only when evaluated in an invocation. Loading a file does not run them. Everything represented here is an object; “kind” describes its current payload, not a class or a static type declaration.

## Structure and literals

| Syntax | Meaning | Example / rule |
| --- | --- | --- |
| `(name ...)` | One named object. | `(counter (count 0))` contains a child named `count`. |
| Nested parentheses | Containment. | Sibling object names must be distinct; order is preserved. |
| Bare name | Object identifier, never a string literal. | `(counter)`; use `(caption "display name")` for text. Quoted names are rejected. |
| Whitespace | Separates tokens; indentation is for readers. | Keep short expressions together, especially `if` and its `condition`; indent branches. |
| `// comment` | Comment through the end of the line, outside strings. | `// This image loads without executing.` |
| `#label` | File-local identity label, immediately after the object's name. | `(counter #counter (count 0))` |
| `@name #label rights` | Named connection to a labeled object, with a numeric permission mask. | `(sender @counter #counter 128)`; target may occur later in the file. |
| `7`, `-7` | Signed 64-bit integer literal. | `(count 7)`; range −9223372036854775808 through 9223372036854775807. |
| `true`, `false` | Boolean literals. | `(ready true)`; numbers are not implicitly booleans. |
| `"text"` | Text literal. | `(name "Alice")`; `(n "7")` contains text, not an integer. |
| `\"`, `\\`, `\n`, `\r`, `\t` | String escapes: quote, backslash, newline, carriage return, tab. | `(line "hello\nworld")`; Unicode escapes and embedded NUL are unsupported. |

`if` is an identifier selecting a primitive in `(if ...)`; `"if"` is always a string value, as in `(word "if")`. The current format requires a name before a payload, so `("if")` is rejected rather than treated as an identifier. Anonymous literal objects do not yet have a source form.

One file has one root object. Connections may form cycles; containment cannot. Labels do not provide global lookup or authority outside the loaded image.

## Object payload kinds

| Kind | Example | Evaluation / typing |
| --- | --- | --- |
| Ordinary object | `(box (item 7))` | Empty payload evaluates to unit, except for the forwarding convention below. |
| Integer object | `(n 7)` | Evaluates to its integer payload. |
| Boolean object | `(ready true)` | Evaluates to its boolean payload. |
| Text object | `(name "Alice")` | Evaluates to its text payload. |
| Primitive object | `(add (left 2) (right 3))` | A recognized operation name without an explicit payload selects that operation. Evaluating it performs the operation. |
| Data using an operation's name | `(add 7)` or `(if :object)` | Explicit payloads override implicit primitive selection; `:object` forces an ordinary empty payload. |
| Named expression wrapper | `(value (read (slot "count")))` | An ordinary empty object with exactly one child forwards evaluation when that child is primitive. Other containers do not automatically execute their contents. |

Payloads are dynamically typed: writing text into an integer field changes its kind. Operations check operand kinds at runtime. Objects can contain children independently of their payload kind. Actors, handlers, images, and messages are roles or runtime structures, not additional payload kinds.

## Control flow primitives

| Primitive | Source example | Behavior |
| --- | --- | --- |
| `do` | `(do (first (write (slot "n") (value 1))) (second (read (slot "n"))))` | Evaluate children in order; return the last result, or unit if empty. Named wrappers allow repeated operations without duplicate sibling names. |
| `if` | `(if (condition true) (then 1) (else 0))` | Evaluate the boolean condition and only the selected branch. Use `(else)` for a unit branch. |
| `while` | `(while (condition false) (body))` | Reevaluate the boolean condition before each iteration; evaluate `body` while true. Normal result is unit. |
| `return` | `(return (value 7))` | End the current invocation, including from inside nested expressions. |
| `fail` | `(fail)` | Raise an explicit failure. Earlier effects remain; recovery is currently a host responsibility. |

## State and messaging primitives

| Primitive | Source example | Behavior |
| --- | --- | --- |
| `read` | `(read (slot "count"))` | Read the payload of an immediate child of the current receiver. |
| `write` | `(write (slot "count") (value 8))` | Replace an existing receiver child's payload and return the new value. Requires authority; does not create a field. |
| `message` | `(message (path ""))` | Read the current message's root payload. A nonempty path selects a message field; unavailable without a current message. |
| `send` | `(send (target "counter") (value "increment"))` | Create and submit a message through the receiver's named connection and its `ether` connection. Returns unit. |

## Window and buffer primitives

These require host-granted connections; see the [window interface](window-interface.md) for setup and limits. The host provides windowing and byte storage; rendering algorithms belong in neo.

| Primitive | Example | Behavior |
| --- | --- | --- |
| `buffer-write` | `(buffer-write (target "buffer") (index 0) (value 255))` | Store a byte, return that byte; WRITE. |
| `buffer-read` | `(buffer-read (target "buffer") (index 0))` | Read a byte; READ. |
| `buffer-size` | `(buffer-size (target "buffer"))` | Buffer size in bytes; READ. |
| `window-present` | `(window-present (target "window") (buffer "buffer"))` | Present pixels; WRITE on window, READ on buffer. |
| `window-poll` | `(window-poll (target "window"))` | Poll events, return close-requested boolean; WRITE. |
| `window-width` | `(window-width (target "window"))` | Cached width in pixels; READ. |
| `window-height` | `(window-height (target "window"))` | Cached height in pixels; READ. |

`index` is a zero-based byte offset. `buffer` names a granted buffer connection. Windows and buffers are host-backed object roles, not extra scalar payload kinds.

## Arithmetic, comparison, and logic primitives

| Primitive | Source example | Result |
| --- | --- | --- |
| `add` | `(add (left 7) (right 2))` | Integer `9`. |
| `sub` | `(sub (left 7) (right 2))` | Integer `5`. |
| `mul` | `(mul (left 7) (right 2))` | Integer `14`. |
| `div` | `(div (left 7) (right 2))` | Integer `3`; truncates toward zero. |
| `rem` | `(rem (left 7) (right 2))` | Integer `1`; remainder of integer division. |
| `lt` | `(lt (left 2) (right 3))` | Boolean: less than. |
| `le` | `(le (left 2) (right 3))` | Boolean: less than or equal. |
| `gt` | `(gt (left 2) (right 3))` | Boolean: greater than. |
| `ge` | `(ge (left 2) (right 3))` | Boolean: greater than or equal. |
| `eq` | `(eq (left 7) (right "7"))` | Scalar equality without coercion; this example is `false`. Not graph identity or deep equality. |
| `and` | `(and (left false) (right true))` | Boolean AND; skips the right operand if the left is false. |
| `or` | `(or (left true) (right false))` | Boolean OR; skips the right operand if the left is true. |
| `not` | `(not (value false))` | Boolean negation; this example is `true`. |

Arithmetic and ordered comparisons require integers; logical operations require booleans. Overflow and division by zero fail. Operands evaluate left to right except for conditional branches and short-circuiting.

## Special names and execution conventions

These names have meaning in specific positions. They are not all primitive keywords.

| Name / role | Meaning |
| --- | --- |
| `handlers` | Receiver child containing message handlers. |
| Handler name, such as `increment` | Selector used to choose a handler. User-defined names are allowed. |
| `body` | Handler's expression wrapper; also the loop body argument of `while`. |
| `tick` | Optional periodic handler recognized by the scheduler. Not a primitive. |
| `condition`, `then`, `else` | Named operands of control flow objects. `condition` is not itself an operation. |
| `left`, `right` | Named operands of binary operations. |
| `value` | Operand used by `not`, `return`, `write`, and `send`. |
| `slot` | Text operand naming an immediate receiver field for `read` or `write`. |
| `path` | Text operand selecting data within the current message. |
| `target` | Text operand naming the receiver's connection for `send`. |
| `ether` | Connection used for message submission; CLI registration supplies access to runtime-created ETHER. |
| Image root / container | One ordinary root object; `image` is a conventional name, not a primitive. |
| Actor / receiver | Object invoked directly or registered with the scheduler; no separate class declaration. |
| Message | Runtime-created object carrying data and lifecycle/authority state; `message` as a primitive reads the current one. |

The CLI registers direct image children containing `handlers`. A scheduler turn processes at most one queued message per registered receiver and its optional tick handler. This is the current host policy, not syntax for parallel execution.

## Connection rights

| Mask | Right | Purpose |
| --- | --- | --- |
| `1` | READ | Read. |
| `2` | WRITE | Modify payloads. |
| `4` | INSERT | Insert contained objects. |
| `8` | COPY | Copy objects. |
| `16` | DELETE | Delete objects. |
| `32` | DELEGATE | Delegate authority. |
| `64` | ACT | Establish an acting context. |
| `128` | SEND | Send messages. |
| `255` | All rights | Sum of all eight bits. |

Combine rights by addition, for example `3` for READ + WRITE. These are capability permissions, not a list of available language primitives: some operations currently exist only in the host C API.

## Compatibility syntax

Prefer inferred literals and implicit primitive names in new source.

| Accepted older / explicit notation | Preferred equivalent or purpose |
| --- | --- |
| `(n :integer 7)` | `(n 7)` |
| `(ready :boolean true)` | `(ready true)` |
| `(name :text "Alice")` | `(name "Alice")` |
| `(box :object)` | `(box)`; retain `:object` when suppressing a recognized primitive name. |
| `(if :primitive (condition true) (then 1) (else 0))` | `(if (condition true) (then 1) (else 0))` |
| `(body :primitive add (left 1) (right 2))` | `(body (add (left 1) (right 2)))`; explicit binding permits a different object name. |

An explicit unknown primitive binding can load but fails when evaluated. Unknown ordinary names without payloads remain ordinary objects.

## Current boundaries

| Area | Current status |
| --- | --- |
| Numbers | Signed 64-bit integers only; no floating-point literals. |
| Expressions | Named operands; no infix operators, `self.field`, brace blocks, or assignment syntax. |
| Variables and control flow | No local bindings, `break`, `continue`, or pattern matching. |
| Object operations | No language-level creation, copy, move, or delete primitives yet. |
| Rich values | No first-class closures or composite evaluation results; temporary values use private C storage. |
| Spatial objects | No built-in spatial operations or dimensional types yet. |
| Recovery | No language-level failure handlers yet; the host can retry or discard failed activity. |
| Persistence | Ordinary quiescent graphs only; active messages, ETHER, scheduler state, windows, and pixel buffers cannot yet be serialized. |
| Format limits | 1 MiB source/output, 4096 objects, 128 containment levels, 4096 decoded bytes per token. |

See the [runtime reference](runtime-format.md) for authority restrictions, failure behavior, scheduler details, and persistence guarantees.
