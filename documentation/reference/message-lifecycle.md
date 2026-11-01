# Message lifecycle and failure inspection

[VM documentation](../README.md) · [Verified foundation](runtime-foundation.md)

This describes the implemented provisional milestone-2 contract. Explicit release
and capability-scoped failure references are engineering choices for the current
runtime, not a claim that all future language semantics have been settled.

## States and explicit decisions

| Current state | Allowed lifecycle decision | Result |
| --- | --- | --- |
| `created` | Maker submits. | `pending`; recipient and grants become fixed. |
| `pending` | Designated receiver accepts in sender/receiver submission order. | `accepted`; payload and nested fields freeze. |
| `accepted` | Receiver begins the inbox head while no request is active. | `processing`. |
| `processing` | Handler returns successfully through dispatch. | Dispatcher finishes it as `processed`. |
| `processing` | Handler fails, has no handler, has an invalid selector, or exceeds host limits. | Remains `processing`; previous effects remain. |
| `processing` | Explicit retry between invocations. | Returns to the front of the accepted inbox; the next attempt starts at the beginning. |
| `processing` | Explicit finish/discard between invocations. | `processed`; an explicit lifecycle decision, not proof the requested action succeeded. |
| `processed` | Explicit authorized release. | Payload disappears; old references report unavailable. |

No automatic retry, rollback, cancellation, rejection, request collection, or
success reply is introduced. A successfully recovered handler counts as success
for its own dispatcher; an outer driver's recovery does not implicitly finish
a different receiver's failed request. Finishing/retrying/releasing a message
owned by an active evaluator/scheduler frame is blocked.

Different senders may be accepted in different orders. An earlier pending message
from the same sender to the same recipient prevents accepting a later one first.
Begin order follows actual acceptance order. Double submit/accept/finish and begin
while already processing return lifecycle/busy errors; they do not duplicate work.

## Participant disappearance

Deleting an inactive sender or receiver does not silently change message state.
`message-info` with `sender-available` or `receiver-available` reports availability
after the usual INSPECT policy check. Numeric sender/receiver IDs remain diagnostic
identities and cannot redirect to replacement objects.

| Disappearance | Outcome |
| --- | --- |
| Sender disappears after submission. | A surviving recipient can still accept, process, and explicitly finish the request. Grants remain in force. |
| Sender disappears before submission. | No other actor gains the maker's submit authority. An explicit cleanup grantee can release the unsubmitted payload. |
| Receiver disappears in any state. | Delivery cannot continue. A surviving maker or cleanup grantee can explicitly release it; no successful processing is implied. |
| Both disappear. | A surviving actor granted RELEASE can reclaim the payload. Without such a grantee it remains retained until image/VM teardown. |

An image that needs orphan cleanup should grant a designated cleanup actor the
required rights before submission. Mere access to ETHER or a message link is not
cleanup authority. Checkpoint encoding still rejects retained messages with missing
participants; reclaim them and remove stale named links before saving.

## Explicit payload release

```neo
(message-release (target MESSAGE_REFERENCE))
```

Returns unit on success. It uses authenticated message policy, not ordinary object
DELETE authority. Message policy bits are READ=1, EDIT=2, INSPECT=4, RELEASE=8,
ALL=15. The maker has ALL; the receiver has READ|INSPECT|RELEASE by default, with
release restricted by state. Additional actors can be granted explicit rights
only while the message is `created`. Existing masks 1–7 keep their earlier meaning.

Release is allowed for:

- A `processed` message, by the maker, receiver, or RELEASE grantee.
- A `created` message, by the maker or explicit RELEASE grantee. A future receiver
  needs that explicit grant to release it before submission.
- A message whose receiver is unavailable, by a surviving maker or RELEASE grantee.

A live pending/accepted/processing request returns busy even to cleanup grantees.
A grant of RELEASE alone grants no payload reading or inspection. Discovering a
target still needs a reachable reference, for example through granted ETHER access.

Release frees the message's entire payload subtree and grant entries. Its opaque
metadata handle remains a tombstone until VM teardown to prevent stale C handles
from aliasing a new allocation. Old named links remain stale, never redirect, and
can be removed with `disconnect` or the native operator's `unlink`. Repeated
release or policy operations on the old handle return unavailable. This is
explicit payload reclamation, not a general-purpose VM garbage collector.

## Capability-scoped failure references

Within `attempt` recovery, status and diagnostic number fields remain available:

```neo
(failure (field "status"))
(failure (field "receiver"))
(failure (field "operation"))
```

To inspect an actual object, supply an existing capability to it or its container:

```neo
(failure (field "receiver-reference") (target EXISTING_CAPABILITY))
(failure (field "operation-reference") (target EXISTING_CAPABILITY))
```

The scope must have READ authority and contain the recorded location. A number
cannot be used as the scope. An unrelated scope is denied; a deleted location
reports unavailable. Returned rights are restricted to READ and any DELEGATE
already present in the scope; WRITE, ACT, DELETE and other rights are excluded.
Persistent diagnostic connections are possible only when the supplied scope
already allowed delegation. Message payload access still requires message policy.

These references identify current live objects, not frozen snapshots or resumable
activations. Image tools can explicitly capture diagnostic values and authorized
connections into their own ordinary records. Outside recovery, all failure fields
return bad state. Nested recovery restores the outer diagnostic scope. Host
execution/allocation limits bypass image recovery and remain visible to the host.

For missing nested handlers or malformed dispatch selectors, no failing handler
instruction exists: the receiver object is recorded as the diagnostic location.
Ordinary handler failures record the actual failing operation. This prevents
pre-handler failures from being incorrectly attributed to the driver.

## Verified scope

`neo selftest` covers 71 expressions plus lifecycle groups: participant loss in all
five states (15 combinations), cleanup permission/retention, stale links, nested
payload freezing, multi-sender acceptance/inbox order, missing/malformed handlers,
managed-message finish/retry/release guards, explicit retry/discard, retained earlier
effects, and uncatchable host limits. The tracked allocator checks balanced cleanup.
Windows x86_64 was rebuilt and tested. No new Linux or sanitizer result is claimed.
