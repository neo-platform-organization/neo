# Version 1 checkpoints

[VM documentation](../README.md) · [Live terminal](live-image.md)

Live `save FILE` writes `!neo-checkpoint 1` followed by one literal graph.
The reader rejects unsupported versions before allocating graph nodes. Unversioned
source and older unversioned checkpoints remain readable as legacy input; ordinary
`format` output remains unversioned. Loading never executes handlers.

## Preserved state

Containment order, scalar/array/behavior payloads, identity relationships,
connection targets and rights survive. File labels reconstruct identities;
numeric runtime IDs are newly allocated. Numeric failure diagnostic fields remain
historical observations, not references to newly allocated objects or capabilities.
Use ordinary authorized graph connections for relationships that must survive.

ETHER roles, all five message states, payloads, grants, submission sequence,
accepted inbox order and one active request per receiver survive. Submission
numbers preserve delivery order; the VM advances its next submission counter past
restored values. Sender/receiver/grant actors must exist in the saved graph.
Frozen submitted payloads remain frozen. Dangling links and unsupported runtime
state cause save failure rather than silent omission.

Image scheduler membership, flags, cursor and error fields are graph data and
survive. Save between invocations, preferably after a completed turn or explicit
reset. The encoder refuses an active C invocation. An interrupted image turn's
`running` marker/scratch also survive; repair the cause and explicitly reset it.
The operator's driver selection and resume flag do not survive: reopen paused.

## Processing means an explicit recovery decision

A saved processing request stays active and blocks further dispatch. Earlier
effects remain in the graph. No stack, instruction pointer, rollback, automatic
retry or acknowledgement is reconstructed. After inspecting and repairing the
worker, select its scheduler entry and call `retry` to restart the handler, or
`discard` to finish the lifecycle without asserting success. Retry may repeat
earlier effects. Merely enabling an entry does not recover an active message.

```text
drive /workspace/scheduler
set /workspace/scheduler/selected 0
call /workspace/scheduler retry
tick
```

## Resources and publication

Native registrations, streams, windows and host handles are rejected by saving.
Keep logical resource descriptions as ordinary data, close/remove live resources
before saving, and request fresh host grants explicitly after reopen. An inert
load never opens a resource or restores old authority. Automatic reconstruction
of GUI resources belongs to the later startup/restore milestone.

The decoder validates labels, rights, message actors, unique submissions, queue
positions/order and active-request uniqueness before publishing any image.
Malformed input and allocation failure return no root and leave existing images
usable. The host can choose to replace a world only after successful decoding;
the live terminal has no implicit load-over-current-world command.

Save encodes before calling the file backend. Native backends write a sibling
temporary file then replace the destination; Windows flushes before replacement.
Encoding failure leaves the destination unchanged. This does not promise recovery
from every filesystem/device failure or an application-level graph transaction.
Shared reader/writer profile: 4 MiB, 131072 nodes, 128 containment levels and
4096 decoded bytes per token/string. See [integrated networking restore](network-restore.md).

## Validation

Windows strict build and all 78 expression cases plus checkpoint groups pass.
Checks cover all message states, order, grants and payload freezing, unsupported
versions, malformed labels/queues/submissions, duplicate active requests, and a
failure injected at every decoder allocation point. Tracked ownership balances.
A fresh-process workflow saves a worker after an observable effect and failure,
reopens it disabled with accepted work retained, repairs and explicitly retries,
then delivers queued work. A subsequent invalid save preserves the previous file.
Linux and sanitizer runs were not repeated for this milestone.
