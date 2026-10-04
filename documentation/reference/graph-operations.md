# Protected graph operations

[VM documentation](../README.md) · [Syntax cheat sheet](syntax-cheat-sheet.md)

These primitives let neo behavior inspect and change objects using capabilities.
Every operation remains a prefix object with named operand children. References
are temporary evaluation results, not integer IDs, names, native addresses, or
contained copies. Duplication is performed explicitly by the kernel.

| Primitive | Operands, in evaluation order | Result / required authority |
| --- | --- | --- |
| `self` | None | Reference with the current receiver's authority. |
| `child` | `target`, `name` | Reference to a named child; READ on parent. |
| `child-at` | `target`, `index` | Ordered zero-based child reference; READ. |
| `child-count` | `target` | Immediate child count; READ. |
| `has-child` | `target`, `name` | Boolean; READ. Only a missing child returns false. |
| `connection` | `target`, `name` | Explicit connection's reference; READ on owner. |
| `restrict` | `target`, `rights` | Attenuated reference; rights cannot increase. |
| `same` | `left`, `right` | Live target identity equality; READ on both. |
| `object-name` | `target` | Copied name text; READ. |
| `object-kind` | `target` | `object`, `integer`, `boolean`, `text`, `primitive`, or `integers`; READ. |
| `inspect` | `target` | Independent payload value; READ, no execution. |
| `object-create` | `target`, `name`, `value` | New contained object reference; INSERT on parent. |
| `object-write` | `target`, `value` | Written payload; WRITE. |
| `object-copy` | `target`, `destination`, `name` | Independent copy reference; READ/COPY on source, INSERT on destination. |
| `object-move` | `target`, `destination`, `name` | New copy reference, then source deletion; also DELETE on source. |
| `object-delete` | `target` | Unit after authorized deletion; DELETE. |
| `connect` | `target`, `name`, `value`, `rights` | Unit; WRITE on connection owner, DELEGATE on referenced value, no rights increase. |
| `invoke` | `target`, `selector` | Receiver-owned handler result; READ/ACT on target. |
| `to-text` | `value` | Convert integer, boolean, or text to text. |

`target`, `destination`, and both `same` operands require references. `connect`'s
`value` is a reference. Names/selectors are text; index/rights are integers. Rights
are the existing bit mask (0–255). Invalid kinds fail, never implicitly coerce.

```neo
// Inspect a contained value without evaluating it.
(inspect (target (child (target (self)) (name "count"))))

// Inspect an explicitly granted environment.
(child-count (target (connection (target (self)) (name "environment"))))

// Copy a prototype into an authorized workspace.
(object-copy
  (target (connection (target (self)) (name "prototype")))
  (destination (connection (target (self)) (name "workspace")))
  (name "instance"))
```

## Lifetime and persistence

Temporary references can flow through operands, `do`, `return`, and local/cross-object
calls. They cannot be stored with `write` or `object-write`, supplied as creation or
message payloads, or parsed from literals. `==` and `!=` reject references; use
`same` for identity. The C result handle is borrowed until VM destruction.

Persistent relationships remain named protected graph connections. Copy/serialization
remaps internal connections; native pointers never enter the image format. Ordinary
quiescent graphs still round-trip. Running activations, messages, queues, and live
resources still prevent full-image persistence.

Deleted targets become unavailable through all old references. Moving creates new
identities and invalidates old ones. A stale parent is an error for `has-child`,
not false. Reference lookup cannot search globally or recover authority from an ID.
Containment capabilities currently cover their subtrees; following a connection
uses its explicit grant. Copy's result has intersected source/destination rights.

## Mutation during execution

All operands are evaluated before targets are resolved for a structural operation.
Nested operands can delete targets; the final operation then fails safely. Earlier
effects remain when a later operand or operation fails. Nothing retries implicitly.

Mutations of every active receiver's handlers subtree are denied, including
suspended callers. Deleting or moving an active receiver or its ancestors is denied;
payload writes to ancestors of active code are denied too. Creating data children
or modifying an inactive object's behavior is possible with the required authority.
These are current activation-safety restrictions, not a final live-editing protocol.
Kernel restrictions for ETHER, messages, registered receivers, cycles, and external
resources still apply to structural operations.

`invoke` installs the target capability as execution authority. It cannot substitute
another identity from a string. It has a separate return scope and no inherited
message context, shares the caller's step budget/depth bound, and propagates errors.
`call` remains same-receiver invocation with inherited message context. Neither
implements resumable exceptions, asynchronous invocation, or OS scheduling policy.
