# Ordinary-object exposure services

[Packages](README.md) · [Snapshot transport](../../documentation/reference/replication-interface.md)

`exposure.neo` is package `neo.network.exposure` version 0.3.0. It exports independent
`publisher` and `subscriber` prototypes. All exposure policy is ordinary neo
behavior; no native primitive was added for this milestone. An application can
contain these services inside any object or copy/edit their behavior. Networking
participation does not require a special object class.

Install through the local manager, clear staging, then make instances. The full
OS workflow is now verified under the shared host resource profile:

```text
pkg-load neo/packages/exposure.neo
pkg install
pkg clear
set /package-manager/id "neo.network.exposure"
set /package-manager/export-name "publisher"
set /package-manager/instance-name "publication"
pkg instance
set /package-manager/export-name "subscriber"
set /package-manager/instance-name "subscription"
pkg instance
```

Clear staging to release the candidate graph before instantiating. Both networking
packages, their baselines, publisher/subscriber and two session copies now fit in
the generic OS image under the shared16384-object/4MiB profile. The integrated
base checkpoint measures13536 objects/624975 bytes in the checked build. See
[capacity and restore](../../documentation/reference/network-restore.md).
The seed itself does not preload networking samples or these services.

## Publisher

Configure `origin` and `publication` as nonempty text. Their pair is an
application-chosen publication identity, not an authenticated peer identity.
Preserve the pair and revision across restarts, or deliberately create a new
publication identity. Changing configuration in an existing mutable object is an
application decision; the VM does not certify or lock these labels.

Grant the publisher two explicit connections:

- `scope`: READ|COPY (9) to the ordinary region being exposed.
- `channel`: READ|ACT (65) to a replica channel bound to an authorized transport.

`publish` increments `revision`, constructs an envelope and freezes a snapshot
through the channel. It returns the queued revision. `send` performs one bounded
send attempt and returns true when finished; schedule it again after false.
The `pending` flag prevents preparing a second event before the first finishes.
Source changes after `publish` do not change the queued event.

`withdraw` queues a revisioned withdrawal event and disables subsequent publish
calls. Finish sending it with `send`. `resume` re-enables this publisher while
retaining its revision counter. Withdrawal announces that exposure has stopped;
it neither erases received copies nor closes the independent connection.

Each envelope contains `schema` (1), `origin`, `publication`, `revision`, `event`
(`snapshot` or `withdrawal`), and, for a snapshot, an owned `state` copy of the
exposed region. External links/resources are still rejected by the snapshot
mechanism. Copies require real source COPY permission; image policy cannot create
it. The counter may have gaps after failed preparation; gaps are permitted.

Repeated publication sends full snapshots, not mutation deltas. Scheduling,
subscriber discovery, fan-out and deciding when something changed remain explicit
application work. One publisher instance currently drives one channel.

## Subscriber

Configure the expected nonempty text `origin` and `publication`, and connect
`channel` with READ|ACT to its receiving replica channel. Matching these claimed
labels is filtering, not authentication.

`receive` polls once. It returns false while incomplete. A completed envelope is
imported inertly to `inbox/candidate`, linked through a READ|COPY `candidate`
connection, and validated. It returns true for a valid candidate. It does not
advance `revision`, invoke received behavior, overwrite application objects or
change accepted state. A candidate blocks receiving another envelope until the
application decides what to do with it.

`validate` checks the envelope version, configured identity, positive revision,
event/shape and the editable `revision-policy` handler. The default requires a
strictly newer revision than the last accepted one. Invalid candidates remain in
the inbox; the handler fails and `valid` stays false. `reject` disconnects and
deletes the candidate without advancing the accepted revision.

To accept, set `accept-name` to a free local name in `accepted`, then call `accept`.
It validates again, copies the envelope to `accepted/NAME`, records its revision,
and clears the inbox connection/candidate. Existing names cause conflict rather
than overwrite. Applications can inspect/copy the accepted `state` into their own
workspace or implement their own merge. A withdrawal is accepted as an event
without `state`; old accepted snapshots remain available. The default does not
choose a shared-writer, ownership or conflict-resolution model.

The envelope/revision checks are replaceable neo behavior. A selftest replaces
`revision-policy` and accepts an older revision, demonstrating that the VM does
not enforce the default order. Native COPY checks still apply after that edit.

## Failure and persistence boundaries

These are sequential mutable live objects. Individual native snapshot/import/copy
mechanisms retain their failure guarantees, but multi-operation service handlers
are not transactions. A budget interruption or allocation failure can leave a
draft, an already queued frame, an accepted record or bookkeeping partially
updated. Inspect and repair that state before retrying; do not assume automatic
rollback or exactly-once acceptance. In particular, an accepted-name conflict
requires choosing another name or reconciling an earlier partial acceptance.
Never blindly clear `pending` on a partly sent frame; discard the transport if the
frame is being abandoned. Keep candidate/accepted records available during repair.

The service prototypes and ordinary envelopes can be saved. Live channel/endpoint
resources must first be removed and external runtime connections disconnected,
then explicitly regranted after loading. Configure persisted publication identity
and counters before resuming a stream.

The Windows universal selftest covers successive snapshots, frozen source state,
explicit acceptance, replay rejection, retained invalid candidates, withdrawal,
resume, replaceable revision policy and native source-COPY denial. The local
package manager check covers installation, both independent exports and saving.
Transport framing and real two-process TCP snapshot transfer are covered by the
preceding milestone. Linux networking is still a stub.

Next: image scheduling/integration of exposure, connection recovery and
resynchronization, plus authenticated provenance. Automatic deltas, discovery and
remote ETHER message-queue integration are not implemented by this package.

The optional [session driver](session.md) now supplies scheduled transport and
full-snapshot reconnect policy. It preserves undecided candidates and never calls
accept/reject. Pinned TLS authenticates the immediate transport peer; discovery, original-author
provenance and mutation deltas remain open.


## Opt-in receipt and decision replies (0.2.0)

Before starting either session, set `acknowledgements` true on both services:

```text
set /service/acknowledgements true
call /session start
```

Use session 0.6.0 and exposure 0.2.0 on both ends. Default false preserves the
one-way protocol. Configure the flag, origin and publication before start; do not
change them while connected. There is no mode negotiation or automatic downgrade.
Reply-enabled operation needs both READ and WRITE on the underlying stream. The
channel ACT grant does not manufacture missing transport authority.

This first protocol permits one publication awaiting a decision. Successful
prepare records `awaiting-revision`; a second publish/withdraw fails while it is
nonzero. Local send completion clears only `pending`. The publisher subsequently
polls `receive-reply`. It keeps these independent last-observed revisions:

| Field | Observation |
| --- | --- |
| `received-revision` | Receiver imported and validated the candidate in quarantine. It has not necessarily accepted it. |
| `accepted-revision` | Receiver explicitly completed its accept handler and reported that decision. |
| `rejected-revision` | Receiver explicitly rejected a previously validated candidate and reported that decision. |
| `awaiting-revision` | Current revision lacking a terminal reply; zero can also mean reset/abandonment, not successful acceptance. |

These are editable image observations, not native authority, cryptographic
receipts, cumulative watermarks or proof that application behavior ran. A reply
from the verified TLS peer is that peer's assertion. Plain TCP has no peer
identity proof. Publishing does not impose ownership or merge policy.

A reply is an ordinary five-child envelope: schema1, origin, publication,
revision, event (`received`, `accepted`, `rejected`). It travels in the reverse
replica-channel direction using the same bounded snapshot framing. The publisher
validates shape, labels and exact outstanding revision. A terminal reply requires
a prior matching receipt. Unexpected/future/old revisions and unknown events fail
without overwriting the last valid observations. The inert `reply` child stays
available after validation failure; no imported behavior runs. Receipt duplicates
for the outstanding revision are harmless; duplicate terminal replies after that
revision is released are rejected.

After valid receipt, a subscriber sets receipt-pending and candidate-reply-active.
It sends receipt before any accepted/rejected decision, even when the application
decides immediately. Reply generation and partial writes take scheduled steps.
It receives no further publication while a reply is pending or a candidate awaits
a decision. `accept` and `reject` retain their explicit application meaning; the
session never calls them. Invalid candidates do not receive positive receipts;
rejecting one does not fabricate a prior valid receipt.

Stop/retry invokes reset-replies and discards wire-pending flags/frames. Publisher
revision counters and last observations survive, but awaiting-revision is cleared
so fresh full-state resync can proceed. A retained subscriber candidate belongs
to its old exchange: accepting/rejecting it after reconnect emits no reply on the
new connection. Only a newly received candidate can acknowledge the new revision.
Do not reuse publication revisions across exchanges. Dropped replies leave the
remote outcome unknown; there is no exactly-once replay or durable receipt log.

Reply handlers, acceptance, and their bookkeeping are sequential operations, not
transactions. Interruption can leave committed accepted state without a decision
reply or a queued reply with incomplete flags. Inspect/repair or discard the
connection before retry; never infer rollback from a failure. Direct users must
coordinate reset-replies with destruction of the old channel/transport. Do not
reset bookkeeping while continuing a partly written byte stream.

Universal selftest covers receipt versus acceptance, stop-and-wait, explicit
rejection and partial reads/writes/would-block. Minimal catalogue installation,
independent exports and serialization pass. Live pinned-TLS tests additionally
cover retained old candidates after reconnect and mismatched reply revisions
without changing previously valid observations. Optional correlated heartbeats and idle work budgets are now available as
described below. Elapsed-time/decision deadlines, replay logs, fan-out and
automatic ETHER queue integration remain open.


## Candidate-safe heartbeats (0.3.0)

Set heartbeats true on both services and acknowledgements true before start, using
session0.7. Probe/alive control frames carry their own sequence and never count as
a publication receipt or acceptance. receive-live stages complete input before
dispatching it to a control handler or ordinary candidate validation; it continues
while a candidate awaits decision. A bounded deferred snapshot handles reconnect
resync without replacing the old candidate. Only a later explicit decision allows
promotion; no receipt is generated for that deferred input before validation.
Reset drops wire staging but preserves the actual candidate/accepted records.

Publisher probe-sequence persists across reconnect; awaiting-probe/probe-pending
are wire state reset with the connection. Subscriber last-probe resets on a new
connection. Activity is an editable counter of validated incoming frames. Control
input never runs imported behavior. See the complete
[liveness contract](../../documentation/reference/network-liveness.md) for queue
ownership, exact echo checks, idle policy, repair and capacity boundaries.
