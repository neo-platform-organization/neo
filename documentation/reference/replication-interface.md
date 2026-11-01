# Snapshot replication

[VM documentation](../README.md) · [Network grants](network-interface.md)

Networking belongs to ordinary neo objects: objects may expose selected scope,
receive outside state, duplicate it and authorize access/copy. No special network
object class or implicit publication is required. As with dimensionality, the
common capability does not mean every object actively participates. Applications
define authority, accepted updates, ownership arrangements and merges.

This first implementation supplies explicit snapshot transport underneath that
model. It does not yet connect remote publications to local ETHER message queues,
provide continuing updates or make remote mutations automatically authoritative.

## Image operations

All targets below are graph references. The transport stream must already have
been published through an explicit network endpoint grant (or another trusted
stream provider).

| Operation | Meaning |
| --- | --- |
| `(replica-channel (target STREAM) (destination CONTAINER) (name "channel"))` | Creates a live channel resource; destination INSERT required. Retains only supplied stream authority. |
| `(replica-prepare (target CHANNEL) (value OBJECT))` | Freezes the selected closed region. Requires source READ and COPY, channel ACT and underlying stream WRITE. |
| `(replica-send (target CHANNEL))` | One bounded transfer attempt; true after the complete queued snapshot is sent. |
| `(replica-receive (target CHANNEL))` | One bounded receive attempt; true when a complete frame is buffered. No graph parsing, import or execution occurs here. |
| `(replica-ended (target CHANNEL))` | Cached Boolean: receive observed EOF between frames. Requires channel ACT and retained stream READ; performs no I/O. Failed receive directions return an error. |
| `(replica-import (target CHANNEL) (destination CONTAINER) (name "replica"))` | Explicitly parses, validates and copies the buffered graph into the destination; requires INSERT, returns its reference. Never runs handlers or overwrites an existing child. |

Channel controls require ACT; transfer still checks the underlying stream's
READ/WRITE rights. Giving a channel ACT is an explicit delegation of its retained
transport operations. One snapshot can be queued in each direction. `prepare`
returns busy while a send is queued. A completed send consumes its queue; prepare
again before sending another snapshot. A complete receive remains buffered until
import succeeds. Import failure leaves the destination unchanged and retains the
frame for retry or channel deletion. Import success permits receiving the next
frame. Applications choose their receiving container; `quarantine` is a convention.

Publication requires a closed ordinary region: internal connections are preserved
and remapped, while external connections cause rejection rather than implicitly
exposing another scope. Native resources and runtime message state are rejected.
The snapshot freezes at prepare; later source mutations do not alter its bytes.
Authorizing a publication cannot force recipients to forget a copy already sent.

Received object IDs are new local identities. Internal serialized connections
refer only within the received graph and confer no authority over the sender or
host. Receipt/import infer no authenticated peer identity. Explicit import may
introduce behavior as data; only a later deliberate invocation runs that behavior.
The importer controls where it is put and what local authority is subsequently
granted. There is no automatic registration, merge, invocation or overwrite.

## Provisional wire format version 1

A frame has a 12-byte header and an ordinary neo graph source payload:

```text
bytes 0..3   ASCII NEOS
bytes 4..7   version 1 as unsigned 32-bit big-endian
bytes 8..11  payload byte length as unsigned 32-bit big-endian
remaining    graph source bytes, no terminating NUL
```

Payload length must be 1..1,048,576 bytes. Unknown versions, zero/oversized lengths,
truncated frames and transport failures are rejected. Framing/transport failures
make that channel direction terminal. Import rejects embedded NULs, malformed
source, unresolved links, runtime state and shared codec limits: 131072 objects,
128 nesting levels and 4096 decoded bytes per token/string. No native descriptor
or portable remote capability token is encoded. TLS and authenticated provenance
are not supplied by this framing layer.

Each send/receive does at most one stream operation, at most 4096 payload bytes
(or the remaining header bytes). Partial transfers and would-block retain offsets.
Snapshot preparation and import use the existing bounded graph codecs. An
allocation failure after receipt of a header retains that header for retry.
A channel is a live resource and cannot be copied or saved. Deleting it releases
buffers, but does not close its independent underlying stream. To abandon a
partial frame safely, discard the connection too; no resynchronization scan is
attempted. Do not interleave raw stream operations or competing channels on the
same byte direction; framing ownership is the caller's responsibility.

## Verification and use

The built-in selftest checks small partial reads/writes, would-block, immutable
snapshot contents, private-scope exclusion, internal reference remapping, authority
denials, allocation failure/retry, inert receipt/import, malformed input, invalid
versions/lengths, truncation and balanced cleanup. A separate two-process Windows
loopback check exercised the image-facing primitives over TCP.

From two terminals, using the Windows executable under `build/x86_64/Windows/`:

```text
neo --listen 127.0.0.1 9000 neo/fixtures/replica-receive.neo
neo --connect 127.0.0.1 9000 neo/fixtures/replica-send.neo
```

Start the receiver first and wait for READY. It prints RECEIVED after importing
and checking the selected snapshot. The fixtures use budget-bounded polling for a
short local test. Real image services should yield pending work between scheduled
turns. These fixtures are separate from the generic OS seed.

Next: ordinary-object exposure services, publication identity/revisions, continuing
updates and explicit application-defined acceptance. No authoritative-writer or
merge policy is selected by this transport implementation. Linux remains stubbed
in this version.

## Image exposure policy

[Exposure services](../../neo/packages/exposure.md) now supply an optional image
package with publication labels/revisions, repeated full snapshots, explicit
acceptance/rejection and withdrawal events. This does not change native framing
or install an ownership/merge policy in the VM. Scheduling, recovery and remote
ETHER queue integration remain future work.

Snapshots can now use [pinned TLS streams](authenticated-networking.md), binding
transport to a verified immediate peer. Their origin/publication fields remain
claims; forwarding a snapshot does not cryptographically attest its original
author. Import and application acceptance remain explicit.


## Frame-boundary EOF

`replica-receive` now returns OK/false after EOF between frames and remembers that
terminal observation. `replica-ended` distinguishes it from a would-block poll;
querying before receive cannot discover a close. Repeated ended receive calls do
no more I/O and never claim a complete frame. Import without a buffered frame
remains unavailable. A complete buffered frame stays complete until import and
is never bypassed to inspect EOF. Import failures retain it as before.

EOF inside any partially received header or payload is still I/O/truncation
failure. TLS stream implementations report bare TCP closure as error rather than
EOF, so it cannot become a clean frame-boundary close. Plain TCP FIN is only a
stream termination observation. No delivery, author identity or acceptance is
implied. Only the receive direction is ended; applications explicitly finish or
close their independent write direction. Wire version and frame bytes are unchanged.

[Session 0.5.0](../../neo/packages/session.md#remote-close-observation-050) handles
this observation in image code, preserving application decisions and state.


Exposure 0.2.0/session 0.6.0 optionally send reverse-direction receipt, accepted and
rejected envelopes. They correlate the exact outstanding publication revision;
local send completion is independent. This is ordinary image policy, using the
existing framing and authority. Replies assert the immediate peer's observation,
not original authorship, ownership or proof of application execution. See
[reply protocol](../../neo/packages/exposure.md#opt-in-receipt-and-decision-replies-020).


Exposure0.3/session0.7 optionally multiplex sequenced probe/alive envelopes through
the same snapshot channel. Exact echo checks, bounded reconnect staging and idle
work policy remain in the image; no new native operation or grant was added.
Heartbeats never imply publication acceptance. See [liveness](network-liveness.md).
