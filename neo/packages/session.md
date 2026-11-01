# Scheduled exposure sessions

[Image packages](README.md) · [Exposure services](exposure.md)

`session.neo` is package `neo.network.session` 0.7.0, exporting an independent
`session` prototype. It drives one publisher or subscriber through the existing
image scheduler. Connection, retry and full-snapshot recovery policy are neo
handlers. No evaluator primitive or scheduler grant was added or broadened.
Linux networking is still a stub.

## Grants and configuration

Configure `role` as `publisher` or `subscriber`. Set up and configure the matching
[exposure service](exposure.md), then give the session these explicit connections:

| Connection | Rights | Scope |
| --- | --- | --- |
| `network` | 65, READ|ACT | A host-created immutable listen/connect endpoint. |
| `service` | 127, READ|WRITE|INSERT|COPY|DELETE|DELEGATE|ACT | Its dedicated publisher/subscriber instance. |
| `runtime` | 119, READ|WRITE|INSERT|DELETE|DELEGATE|ACT | The session itself, solely for managing its own peer/channel. |

The runtime self-grant allows `tick` to operate through the scheduler's existing
67 (READ|WRITE|ACT) worker dispatch connection. It grants no root or host-network
authority. The separate endpoint grant is still required. The operator can set
up `runtime` with `link /session runtime /session 119` when it is absent.

The session exclusively manages the service's `channel` connection and its own
`peer` and `channel` children. Do not share that connection between drivers or
use those names for unrelated children. Starting/stopping/retrying disconnects
the service channel, discards the owned channel and peer, closes the endpoint,
and resets the publisher's pending-send bookkeeping. It preserves service
identity, revision counters, accepted records, and any subscriber candidate.

The host operator can now provision a fixed endpoint in a live image:

```text
net-grant /session listen 127.0.0.1 9000
net-grant /session connect 127.0.0.1 9000
```

These are alternatives on opposite peers. The command creates `host-network`
under the selected object and grants its `network` connection; it does not open
a socket. An existing grant/name causes conflict rather than replacement. The
address, port and direction cannot be broadened by editing image payloads.
The Windows live host installs the provider, but source code cannot call this
trusted operator command. Linux reports unsupported.

## Tick lifecycle

`start` validates the role/retry delay, cleans up any previous transport and sets
phase `closed`. It does not publish or accept state. Register the worker with
`candidate-periodic = true` through the existing scheduler registration protocol.
Each scheduler invocation calls `tick` once:

- `stopped`: no activity.
- `closed`: one nonblocking endpoint-open attempt, then `connecting`.
- `connecting`: one connect/accept poll. On success, create the replica channel,
  bind the exposure service, mark full-state publication due and become `active`.
- `active` publisher: prepare one full-state snapshot/withdrawal when due, or
  perform one send step. Otherwise idle.
- `active` subscriber: perform one receive step unless a candidate already awaits
  a decision. Never call accept/reject or overwrite application state.
- `closing`: drain an already pending publisher frame, then progress local write
  shutdown. No new publication or receive/import is started.
- `failed`: keep the diagnostic in `error`; automatic retry is off by default.
- `backoff`: count down positive `retry-delay` scheduler turns, then reopen on a
  subsequent turn. This is a turn count, not milliseconds or a wall-clock promise.

`retry` discards the old transport and starts the configured backoff. Set
`automatic-retry` to true to let failed ticks request that same retry path.
`stop` tears down transport and becomes stopped. `resync` is a publisher operation
that marks another full publication due. Applications call it after changing the
exposed scope; there is no implicit change detector or delta encoder.

A new connection always schedules fresh full state, using the publisher's
preserved identity and a new revision. If the publisher is disabled, it sends a
fresh withdrawal instead. An interrupted partial frame is discarded with its
connection; it is never appended to a new connection. This is full-state
resynchronization, not acknowledgement/replay of individual delivered events.
There is no guarantee that a completed local send was accepted remotely.

Subscriber candidates survive stop/retry/reconnect. They continue to block new
receive work until the application explicitly accepts or rejects them. This also
means a pending candidate delays noticing remote EOF. An idle publisher discovers
failure on later I/O in one-way mode. Optional heartbeat/idle work policy is
available below; readiness wakeups and elapsed-time deadlines remain unfinished. Pinned TLS authentication is available as described below. A malformed candidate can leave the session failed while
the candidate remains available for review.

`tick` catches ordinary failures and returns a nonnegative work result, allowing
other scheduler members to continue. Allocation/execution exhaustion still
bypasses image recovery and requires operator inspection. Service sequences are
not transactions: partial setup/teardown may need repair after interruption.
Periodic scheduler entries count as work even when polling; pacing and readiness
waiting remain explicit scheduling policy. The driver never busy-waits inside a
tick and does not change the scheduler's existing periodic semantics.

## Two live images

Minimal fixtures contain the service, session and existing scheduler code, with
local scoped grants. They have no endpoint until the operator grants one. Start
two Windows terminals from the repository root, using the target executable:

```text
neo live neo/fixtures/session-receive.neo
```

In that receiver:

```text
net-grant /session listen 127.0.0.1 9000
link /scheduler candidate /session 99
call /scheduler register
unlink /scheduler candidate
call /session start
drive /scheduler
resume
```

In the other terminal:

```text
neo live neo/fixtures/session-send.neo
net-grant /session connect 127.0.0.1 9000
link /scheduler candidate /session 99
call /scheduler register
unlink /scheduler candidate
call /session start
drive /scheduler
resume
```

Inspect the receiver with `show /service/inbox`. Acceptance remains explicit:

```text
pause
set /service/accept-name "first"
call /service accept
resume
```

On the sender, request fresh state after an edit:

```text
pause
set /scope/value 43
call /session resync
resume
```

To test recovery after a failure, inspect `show /session`, repair its cause, then
`call /session retry` on each affected side. Or explicitly enable automatic retry
on each session. Use a fixed listener port: reopening a port-0 listener can choose
a different port, which the connector's immutable grant will not discover.

Before saving: pause, stop the session, `unlink /session network`, then
`delete /session/host-network`. Live resources must be regranted after loading.
Service counters/candidates/accepted records remain ordinary persistent state.

The two packages also install independently through the package manager; the
shared16384-object whole-image profile applies to combined packages,
baselines, staging and runtime instances. The minimal fixtures avoid catalogue
baseline overhead. The OS seed remains generic.

## Verification

Windows universal selftest runs real TCP loopback through the actual image
scheduler: initial snapshot, explicit acceptance, dropped connection, failed-state
retention, delayed retry, reconnection, fresh full-state revision, unchanged
accepted revision, and preservation of an undecided candidate across another
reconnect. The test uses unchanged scheduler worker rights and explicit runtime
and service grants. The package installs/instances/saves independently. A separate
two-process live-operator test verified endpoint provisioning, scheduled transfer,
resync after an edit and preservation of the previously accepted state.

Next: active-session liveness and acknowledgement policy. Ownership,
merge and acceptance remain application choices.

## Authenticated endpoints (0.2.0)

Use [net-grant-tls](../../documentation/reference/authenticated-networking.md)
for immutable pinned mutual TLS. Set `require-authentication` true to reject a
plaintext endpoint before channel creation. `peer-identity` is a diagnostic cache
of the native verified-peer query, cleared at cleanup; stored text is not an
authority token. The TLS grant itself never downgrades to plaintext. Publication
labels/ownership/acceptance remain image policy. Next is N7 liveness and
acknowledgement design; forwarded snapshots do not gain signed provenance.

## Connection-attempt budget (0.3.0)

`connection-limit` is an image policy measured in unsuccessful `network-stream`
polls, including TCP connect/accept and TLS negotiation. Zero (the default)
permits indefinite waiting; a positive integer bounds the whole attempt. Negative
values fail configuration. Configure it before start/retry; changing it during an
attempt is unsupported. It is not a millisecond deadline or an idle timeout.
A listener's budget includes waiting for the first connection, not just its TLS
handshake. A success on the last permitted poll wins.

Opening an endpoint initializes `connection-remaining` from the configured limit.
Each unsuccessful poll consumes one unit. The last unsuccessful poll runs cleanup,
closing both the endpoint and any retained TLS handshake, then raises an ordinary
failure. `tick` records `error = "explicit failure"` and phase `failed`; an exhausted
`connection-remaining = 0` distinguishes the budget condition for inspection.
No application candidate, accepted record or revision is discarded. Automatic
retry remains off by default; enabled retry takes the existing backoff route.
Each reopened attempt receives a fresh budget. Direct users of native endpoint
APIs must still close their own pending attempts; this policy belongs to sessions.

For example, `set /session/connection-limit 10000` before `call /session start`
permits 10000 unsuccessful polls. Scheduler speed varies, so choose this as a work
budget rather than inferring an elapsed-time service guarantee. Pausing the
scheduler pauses consumption. Existing sessions keep indefinite listening by
default, and active traffic/candidate review does not consume this budget.

The universal Windows test checks negative configuration, exact exhaustion,
endpoint closure, candidate identity retention, restart and disabled limits. The
optional TLS live test also connects a silent TCP peer, consumes exactly three
TLS polls, verifies socket EOF with no peer publication, then retries successfully
with a replenished budget. Heartbeats and acknowledgement frames remain
unfinished N7 work. Graceful local close is available as described below.


## Graceful local finish (0.4.0)

While active, call `finish` to enter `closing`. The scheduled worker completes an
already prepared publisher frame, then polls `network-finish` on its peer. It
creates no new publication, so a dirty scope that has not yet been prepared is
not sent. Explicitly request resync and let the active worker prepare it first
when that update must precede shutdown. A subscriber stops receiving and keeps
its candidate/accepted state. On local shutdown completion, the session cleans
up transport and enters `stopped`. `stop` remains immediate cancellation from
any phase. `finish` outside active raises an ordinary failure.

`finish-limit` optionally bounds unsuccessful close/drain polls. Default zero
disables it; negative values are invalid. Set before requesting finish. Positive
values initialize `finish-remaining`; each still-pending send or close consumes
one unit. A completed pending send advances to a later close turn without
consuming another unit. A successful close wins on its last allowed poll.
Exhaustion closes resources, then fails the tick. Application state survives;
partial transmitted frames can be lost. Automatic retry, if explicitly enabled,
uses the existing failure/backoff path even after a failed finish. Disable it if
failure must retain the stopped intent for operator review. Polls are not time.

On Windows TLS, graceful finish generates and flushes authenticated close-notify
before socket write shutdown. Plain TCP uses write shutdown. A completed local
finish does not wait for a remote close, receipt acknowledgement or application
acceptance. Session cleanup then destroys its stream; use the native stream API
if an application must keep reading until a reciprocal close. Avoid unread inbound
data when relying on OS socket deletion to complete cleanly. Direct stream users
must complete a pending TLS write before finish (`work pending or out of order`
otherwise), and must keep polling a false finish result. Nonempty writes fail
after finish begins; reads remain available until deletion or peer EOF/failure.

Native selftest covers client- and server-initiated TLS close, reciprocal close,
WRITE denial, idempotent finish, write-after-finish rejection and continued bare
TCP truncation detection. Scheduled tests cover image finish and candidate
identity retention. The optional live TLS test sends a prepared update before
finish, verifies the receiver's prior accepted revision is unchanged, and tests
explicit finish-budget abort with the new candidate retained.


## Remote close observation (0.5.0)

After a subscriber receive step, `replica-ended` checks whether the frame layer
observed EOF between frames. If so, the session sets `remote-closed` true and
enters its existing `closing` path to finish its own write direction and stop.
It does not enter failed/backoff or automatically reconnect, even when automatic
retry is enabled. A reciprocal-close transport failure or an exhausted finish
budget still uses the failure path; explicit retry can reopen afterwards.
`remote-closed` is diagnostic image state, reset by start/retry, not authority.

A pending subscriber candidate blocks further receives, including close detection.
The application must explicitly accept or reject it before EOF is observed. Neither
EOF nor shutdown accepts, rejects, or deletes that candidate. Accepted records and
revision state survive cleanup. A publisher with acknowledgements disabled still does not read idle traffic.
Reply-enabled publishers poll reverse traffic and observe boundary EOF, but
optional probe/idle work policy is available below.

On TLS, boundary EOF requires verified close-notify; raw TCP FIN has no such
identity/integrity guarantee. EOF within a header or payload remains truncation
and fails permanently. Boundary close means no incomplete frame was observed,
not that all intended updates arrived or were accepted. Only complete imported
frames can become candidates. Existing callers of replica-receive must now query
replica-ended to distinguish pending from ended when it returns false.

Portable tests verify retained boundary EOF after import, no new import/read on
repeated polls, query authority, and partial-header/payload failures. The scheduled
reconnect regression now injects a partial frame to produce genuine TCP truncation.
The optional TLS live test verifies pending-candidate retention, explicit acceptance,
reciprocal close to stopped, accepted state preservation, and no automatic retry.


## Duplex replies (0.6.0)

Enable [exposure replies](exposure.md#opt-in-receipt-and-decision-replies-020) on
both services before start. Active publishers finish queued writes, otherwise
poll a reply and prepare dirty state only when no revision awaits a decision.
Receipt alone does not release that wait. Active subscribers flush pending replies
before receiving more state, including while a candidate awaits a decision. A
subscriber in closing drains queued replies before local network finish. That
close does not wait for a new application decision or remote acknowledgement.
Finish budgets also count still-pending reply-drain turns. Stopping immediately
can lose those replies; no accepted state is rolled back.

Cleanup resets wire reply bookkeeping on reply-enabled services while retaining
observations/candidates/accepted state. In one-way mode, services without the new
flag remain compatible. Do not switch modes on an active channel. The same
runtime119/service127/endpoint65/scheduler67 grants are used; no C primitive was
added for acknowledgement policy. Common phase/close/reply-mode handlers now use
ordinary call reuse to reduce graph duplication (session885 objects, down from
an intermediate1116). This keeps the combined networking regression within the
existing import budget.

Both package manifests now install in the full OS seed with catalogue baselines
and runtime copies under the shared16384-object/4MiB profile. The full-OS TLS
checkpoint/regrant workflow passes; minimal fixtures remain useful focused tests.
See [integrated capacity/restore](../../documentation/reference/network-restore.md).

The universal suite and optional TLS live test pass. The latter checks received1
without accepted1, explicit acceptance/rejection, reconnect with an undecided old
candidate, fresh-revision receipt, and a malformed revision999 reply retained for
inspection while prior accepted/rejected observations stay unchanged.

Optional heartbeat and bounded idle-work policy is implemented in0.7. There is
no elapsed-time, decision-deadline or delivery guarantee from poll budgets.


## Heartbeats and idle work (0.7.0)

Enable heartbeats and acknowledgements on both exposure0.3 services before start.
Session heartbeat-interval defaults32 active turns and must be positive. idle-limit
is nonnegative; default0 disables expiry. Activity changes reset idle-remaining;
otherwise a positive budget decrements after each active transfer turn. Expiry
marks liveness-expired, cleans transport and enters failed; explicit/opt-in retry
keeps its existing meaning. Successful connection setup reinitializes counters.
No C clock, primitive or broader grant was added.

Publishers send sequenced probes without overlapping frame writes. Subscribers
answer while keeping undecided candidates intact. A single deferred fresh resync
snapshot lets probes continue after reconnect while an old candidate waits for
its explicit decision. Active remote EOF is observable in this mode even with a
pending candidate; cleanup preserves that candidate. One-way mode is unchanged.

See [network liveness](../../documentation/reference/network-liveness.md) for exact
ordering, protocol/repair limits, configuration, tests and package capacity.
This completes the initial Windows N7 work-budget contract. N8 integrated restore
and sizing are now verified below. Wall-clock deadlines remain a future extension.


N8 integrated checkpoint: full OS installation now fits the shared native profile.
Stopping/removing resources preserves candidates/accepted data and counters;
restore is inert and new host endpoint grants are explicit. The live full-OS test
checks a new TLS handshake, retained old candidate and a fresh resync revision
without conflating their decisions. No package/session policy version changed.
