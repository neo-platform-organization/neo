# Source editing mechanisms

The editor and application lifecycle are image policy. These generic C mechanisms
provide bounded text access, inert parsing and protected replacement; there are no
native text widgets, package-specific edits or second source language.

## Text values

| Primitive | Operands / result |
| --- | --- |
| text-size | value -> UTF-8 byte length |
| text-part | value, start, count -> byte slice |
| text-edit | value, start, count, insert -> replacement text |
| text-offset | value, start, direction -> previous/next scalar boundary; direction0 clamps to a boundary |
| text-line / text-lines | value, index -> line text / value -> line count |
| text-position | value, index, field="line" or "column" -> position |
| text-index | value, line, column -> clamped byte index |
| text-find | value, needle, start -> byte index or -1 |
| text-from-bytes | value integer array -> validated UTF-8 text |

Indices and columns are zero-based UTF-8 byte offsets. Splices/slices reject an
interior scalar boundary; text-index clamps to a boundary. Insert/from-bytes reject
invalid UTF-8, NUL and control bytes other than tab, LF and CR. Empty text has one
line. A zero text-offset direction clamps an arbitrary index down to a boundary.
General result bounds use the host image source-byte profile. Memory work is
charged in 64-byte blocks; allocations are owned by the VM's normal value lifetime.

stream-read-text takes a connection-name target and positive limit. It makes one
bounded stream read into a text value; invalid UTF-8/NUL fails, WOULD_BLOCK is BUSY,
and EOF yields empty text. It does not loop to wait for input. The optional Windows
clipboard is a stream capability: reads snapshot CF_UNICODETEXT, writes replace
clipboard text with UTF-8/UTF-16 conversion. It is not serialized into an image.
The host creates a message-only clipboard owner; no visible editor UI is native.

window-event also exposes boolean control and shift fields alongside symbolic key
and committed-text events. Native fonts/shaping are unchanged.

## Inert source and replacement

source-text(target) returns canonical neo for a closed ordinary subgraph. Labels
are included only where internal connections need them. External connections,
native resources and live runtime roles are rejected. It can format a handler
without formatting the app's state/resources.

source-check(value) stages an inert image parse, then releases it. It returns empty
text on success or a line/column diagnostic on failure; it never invokes behavior.

source-apply(target,value,expected) requires READ|WRITE|INSERT|DELETE. It checks
canonical current source against expected, stages/parses the replacement, requires
the same root name, and validates runtime/resource restrictions before mutation.
After preflight succeeds, publication performs no allocation or behavior calls:
it replaces children/payload/internal edges while preserving the target root's
identity, containment name and order. New internal root references are remapped.
Old descendant identities become unavailable. State outside the target stays intact.
Syntax/allocation/preflight failure leaves the old graph unchanged. A changed
baseline returns CONFLICT; there is no implicit retry or merge.

The evaluator's active-code guards also apply. An editor submits a request; the
desktop services it after the edited app's input/frame invocation has returned.
A live invocation is never rewritten underneath native activation pointers.

## Callback work isolation

attempt optionally accepts a positive budget operand. If it fits below the
caller's remaining budget, it reserves a child bound and permits recovery from
that child's LIMIT. The caller's original bound is restored before recovery.
An unreserved outer LIMIT and OUT_OF_MEMORY still propagate. Effects before an
error remain; attempt is not transactional. execution-budget reports remaining
steps so an image dispatcher can leave room for composition/error handling.

Desktop uses these mechanisms to isolate input/frame callbacks and document work.
A failed frame is marked in its view record; successful live application can clear
that failure for the matching receiver. Native process crashes are not caught by
this mechanism. The user's earlier Instantiate exit was not reproduced as a native
crash; an uncontained callback LIMIT was identified and is now bounded.

## Verification

selftest gui checks UTF-8 boundaries, malformed input, child-budget recovery,
allocation-failure atomicity, internal-edge remapping, identity/state preservation, invalid/stale/denied edits, fresh app instances and
termination, shared documents/last-owner cleanup, keyboard mini-editor Apply,
package publication affecting future copies, stale package rejection, clipboard
fallback, retained data, scroll/zoom and runaway repair. Final full Windows
selftest passes with balanced ownership.
It uses headless surfaces and does not overwrite the Windows clipboard. Native
clipboard interop and physical editing interactions still need an interactive
acceptance pass. Font shaping and durable filesystem-backed documents are deferred.
