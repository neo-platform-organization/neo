# Network liveness and decision boundaries

[VM guides](../README.md) · [Session policy](../../neo/packages/session.md)

The first Windows N7 subset is implemented in exposure0.3/session0.7. These are
replaceable image contracts, not mandatory ownership or acceptance semantics.
No C primitive, native grant, clock or automatic acceptance was added.

## Enable and configure

On both minimal session fixtures, before starting the session:

```text
set /service/acknowledgements true
set /service/heartbeats true
set /session/heartbeat-interval 32
set /session/idle-limit 512
```

These numbers illustrate work budgets, not recommended timeouts. Grant the network
endpoint explicitly and use the existing start/register/drive steps in the
[session guide](../../neo/packages/session.md#two-live-images). Both peers must
use compatible modes/versions; no negotiation or downgrade is provided. Pinned
TLS is the authenticated transport option; plain TCP still has no peer proof.

`heartbeats` defaults false. When enabled it requires acknowledgements true.
`heartbeat-interval` must be positive; `idle-limit` must be nonnegative. Default
idle-limit0 disables expiry, even when probes are enabled. Configure these fields
before start/retry. A one-way session does not silently acquire an idle detector.

## Probe and activity protocol

Publishers initiate a probe after the configured number of active turns, when no
publication/probe write is pending and no earlier probe awaits its response.
Subscriber responses use the reverse direction. Each control frame contains
exactly schema1, origin, publication, sequence, and event (`probe` or `alive`).
A positive monotonically increasing probe sequence is retained by the publisher.
The subscriber rejects repeated/backward sequences on that connection. An alive
reply must echo the currently outstanding sequence. Neither control frame changes
publication revision, received/accepted/rejected observations, or application state.

Probe, response, receipt and decision writes share one owned framing direction.
Only its image service prepares/sends frames; native framing still rejects a
second prepared frame. Publication writes finish before probes. Receipt/decision
responses take priority over alive responses. All partial transfer offsets remain
in the existing replica channel. No competing raw-byte reader is introduced.

A subscriber with an undecided candidate continues to receive control frames.
It never overwrites that candidate to do so. Heartbeats also work on idle
connections after an explicit accept/reject. Full validated incoming candidates,
valid probes and validated replies increment an ordinary service activity counter.
Sending bytes, merely polling, arbitrary partial bytes and local application
accept/reject are not observations of new remote activity.

## Idle budget and failure

Connection setup initializes idle-remaining from idle-limit and caches activity.
After each active transfer turn, fresh valid remote activity resets the full
budget. Otherwise a positive budget loses one unit. Expiry marks liveness-expired,
closes owned transport through cleanup, and raises the existing failed tick.
The default automatic-retry false retains failed state; explicit retry or opt-in
backoff follows the existing policy. Establishing a new connection clears the
expired diagnostic and starts new counters. Successful activity on the last
allowed turn wins before expiry. Stopped/connecting/closing phases use their
separate existing policies and budgets, not the active idle counter.

These are local scheduled-turn counts. Pausing the local scheduler pauses expiry;
pausing only its remote image can exhaust the local budget while TCP remains open.
Different scheduler rates/load can cause false suspicions. Large/slow frames need
sufficient budget because activity is counted only when validated completely.
There is no elapsed-second deadline, proof of remote failure, lease, failure
detector accuracy guarantee or mandatory decision deadline. A responsive image may
keep a publication undecided indefinitely. A missing specific echo does not erase
other validated peer activity; liveness tests responsiveness, not acceptance.

## Reconnect and bounded staging

An old candidate survives reconnect without belonging to the new reply exchange.
One fresh full resync snapshot may be stored as deferred while the old candidate
awaits its decision. That leaves the framing reader free to process probes behind
it. The deferred copy is inert, unvalidated application input, not a candidate or
receipt. A second deferred publication fails rather than growing an unbounded
queue. Stop-and-wait publishers normally prevent that second arrival.

After explicit decision of the old candidate, a later scheduled step promotes and
validates the deferred snapshot as the new candidate and queues its receipt. The
old decision never acknowledges it. Reset/stop discards incoming/deferred transport
staging and pending control replies, retaining the actual candidate and accepted
records. Failure/interruption can leave partial graph operations; inspect and
repair before retry, as for the rest of the exposure service.

With heartbeats disabled, a pending candidate still defers all reads including EOF.
With heartbeats enabled, the reader may observe clean boundary EOF while a candidate
is pending and close the session without deciding/deleting that candidate. TLS
requires authenticated close-notify for clean EOF; truncated frames still fail.
No shutdown, timeout, receipt or probe implicitly accepts any publication.

## Verification and capacity

The strict Windows build and full universal selftest pass. Portable partial-I/O
checks cover correlated probe/response frames, would-block and unchanged candidate
identity/acceptance. Configuration tests reject heartbeats without replies,
zero probe interval and negative idle limits. Existing authentication, truncation,
reconnect, close and acknowledgement tests still pass.

The optional [TLS live test](../../neo-vm/platform/win32/tests/tls_live.ps1) manually
steps both images, observes repeated probes with candidate42 still undecided,
then pauses one side while leaving TCP open. It verifies expiry in both directions,
including exactly three controlled idle turns, unchanged accepted state, deferred
resync after reconnect and rejection of an incorrect alive echo. It uses bounded
adaptive stepping around OS/network progress, not a fixed assumption that each
TLS read finishes in one poll. Temporary identities are removed on completion.

The shared native profile is now131072 objects/4MiB for local image operations.
Focused tests still use isolated fixture images with local schedulers and grants.
The N8 full OS test installs both catalogues and four instances in each image,
saves/restores accepted and pending state, then explicitly regrants fresh TLS
endpoints. See [capacity/restore](network-restore.md). Snapshot wire limits remain
separate and Linux networking remains stubbed by explicit user instruction.
