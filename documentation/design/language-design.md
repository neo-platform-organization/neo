# neo syntax, primitives, and host-language proposal

[Documentation index](../README.md)

The executable prototype now has a deliberately smaller, explicitly tagged syntax. See [runtime-format.md](../reference/runtime-format.md) for what actually runs. The examples and feature tables below remain design proposals, not a compatibility promise.

Status: proposal for review, not implemented or agreed syntax. Read [architecture.md](architecture.md) for settled semantics. The user has established image.neo as the image graph and a separate VM capable of loading, duplicating, and deleting multiple image instances. The initial VM will be written in C.

## Image and execution

The file describes objects and edges. Loading constructs a graph without executing it. Starting an image explicitly enables its scheduled behavior and message processing. Behavior bodies are objects in the same graph, interpreted by the VM when activated.

Do not make a file of imperative construction commands the sole image format. Graph serialization needs to preserve identity relationships and cycles, not just regenerate similar-looking objects by executing source.

## Proposed reader

Retain o-expression containment, extending its lexical vocabulary:

```ebnf
document = object ;
object   = "(", name, [label], {object | link | literal}, ")" ;
label    = "#", identifier ;
link     = "@", identifier ;
literal  = integer | string ;
```

Whitespace separates tokens. `>` starts a comment outside strings, ending at newline. Identifiers use ASCII letters/underscore initially, followed by letters, digits, underscores, or hyphens. Integers are signed decimal. Strings are UTF-8 in double quotes, with `\"`, `\\`, `\n`, `\r`, `\t`, and `\uXXXX` escapes; reject invalid Unicode scalar sequences. Reserved literal object names are `true`, `false`, and `unit`, each used as a zero-child object. No implicit truthiness.

Every parenthesized expression defines one object. A label identifies that node within the file. A link adds an edge to an existing node, not another contained copy. Labels are not runtime addresses or a globally searchable namespace. Forward links are allowed. Reject duplicate labels and unresolved links. Resolve links after allocating nodes, permitting reference cycles without containment cycles.

Literal payload tokens are reader notation for primitive payload objects; the runtime does not expose non-object values merely because source uses a shorthand. Ordered entries are preserved. A named-slot API rejects ambiguous repeated names; sequence operations may contain repeated operation names without ambiguity because they use entry order.

```neo
> A containment tree with a connection back to the root.
(image #world
  (cell #cell-a
    (environment @world)
    (count 0)
    (caption "cell A")))
```

An `@` edge reconstructs connectivity. It does not grant inspection, mutation, or host authority by itself. Image-local policies require validation; external grants come from the VM's loading context. Do not let author-supplied IDs impersonate objects from another loaded image.

## Proposed behavior notation

```neo
(image #world
  (counter #counter
    (count 0)
    (handlers
      (increment
        (body
          (do
            (if
              (lt (read (self) "count") 10)
              (write (self) "count"
                (add (read (self) "count") 1))
              (unit))
            (return (read (self) "count"))))))))
```

This is a graph declaration, not an immediate call to increment. Handler names select receiver-owned bodies. Runtime role validation, rather than the spelling of an arbitrary data object's name, determines what is executable. The bootstrap schema establishes handler/body roles. A message naming increment requests that handler; it does not inject executable code.

`read` obtains a named slot's contained value subject to authority. `write` replaces that value with an independent copy of its evaluated input, rather than sharing containment. Low-level connection construction is separate and permission-checked. `self` denotes the current receiver without widening its authority. This is provisional slot semantics.

## Control flow

| Form | Proposed evaluation rule | Reason |
| --- | --- | --- |
| do | Evaluate children left to right; return the last result, or unit if empty. | Explicit ordering of effects. |
| if | Evaluate a boolean condition, then only the selected branch. | Unselected behavior must not execute. |
| while | Reevaluate the condition before each iteration; return unit on normal exit. | Minimal general repetition without exposing ticks. |
| break / continue | Exit or continue the nearest loop in the current activation. | Structured loop control. |
| return | Evaluate its operand and finish the current activation. | Explicit invocation boundary. |
| let / local / assign | Bind, read, and update activation-local values; block scopes are lexical. | Temporary computation should not require persistent receiver mutation. |
| and / or / not | Boolean operations; and/or short-circuit left to right. | Avoid unnecessary or unsafe effects. |
| handle / raise | Install an explicit failure handler / expose failure to it. | Programmer chooses recovery policy. Exact resumption contract remains open. |

Operands evaluate left to right except the explicitly lazy forms above. Initially do not add implicit parallel blocks, implicit retries, or scheduling yields. Iteration over collections and pattern matching can be library behavior or later syntax. Nonlocal return and resumable continuations are deferred.

## Primitive families

**Agreed:** every primitive is an object. This includes control flow, arithmetic, messaging, authority operations, and lifecycle operations. The names below identify primitive objects or their reader notation, not non-object functions outside the language. The VM may execute their behavior through native code, but that does not change their language-level status. Inspection must not execute them, and copying must not increase their authority.

| Family | Candidate operations | Implementation boundary |
| --- | --- | --- |
| Values | integer, boolean, text, bytes, unit | VM representations; define overflow and encoding explicitly. |
| Arithmetic | add, sub, mul, div, rem | Start with checked signed 64-bit integers; division by zero and overflow produce failures. Division truncates toward zero. Arbitrary precision is a later option. |
| Comparison | eq, same, lt, le, gt, ge | eq compares primitive values; same compares identity. Compound structural equality is explicit library behavior. |
| Object state | self, read, write, children | Reads/inspection and writes require separate authority. |
| Lifecycle | new, copy, move, delete | Containment, listings, and safe identity invalidation belong in the VM. |
| Messaging | message, edit, submit, accept, inspect | ETHER policy and atomic acceptance belong in the VM; replies can be ordinary messages. |
| Authority | restrict, delegate, permits | Cannot manufacture stronger rights. permits is not a substitute for checking at use time. |
| Execution | register, unregister, explicit local behavior invocation | Minimal scheduling boundary; cross-object requests remain messages. |
| Failure | raise, handle, inspect-failure | Mechanism only; no built-in recovery policy. |
| External I/O | operations on explicitly granted device/service objects | No ambient filesystem, network, clock, or process authority. |

Not every public operation needs a VM opcode. Discovery, replying, filtering, repeated scheduling patterns, and most collection algorithms should be implemented within the image using the core. Start with a small trusted kernel and grow image-level behavior.

Whole-image load, duplicate, unload, and save are host VM operations. An image gets access to them only through an explicit management capability. Unloading an image is not deleting its source file.

## Host-language decision

**Agreed: C for the initial implementation.** The user's priority is debugging neo rather than the host programming language or toolchain. Odin and Zig remain potential migration targets. A specialized compiled subset of neo could eventually implement neo itself.

The host language does not dictate neo's object model. C structs, procedures, and allocation strategies implement objects; they do not become language-level classes or non-object primitives.

Proposed baseline: portable C17, explicit ownership, checked opaque handles, strict warnings, and sanitizer-assisted tests where supported. Keep platform code isolated and serialize graph semantics rather than C memory layouts. Primitive objects use validated semantic bindings to native implementations, never serialized function addresses.

The migration contract is the documented image format, primitive behavior, authority checks, message lifecycle, and executable semantic tests. Odin/Zig or a self-hosted runtime should satisfy that contract. Self-hosting and a native compiler are future work, not prerequisites for the C interpreter.

The reader and primitive subset above remain proposals. The sample is illustrative, not executable until a parser and evaluator implement these rules.
