# Image-owned scheduling

[VM documentation](../README.md) · [Live terminal](live-image.md)

`packages/system/scheduler` is the executable reference policy. Its entries,
membership handlers, ordering, failure records and recovery decisions are neo
objects/behavior in the image. C retains protected dispatch, authority checks,
the invocation budget and native waiting. This is a provisional sequential policy,
not parallel or preemptive execution.

## Register and run a worker

From the repository root, launch `neo live neo/neo-os/image.neo` using the binary
for your target. In a fresh session:

```text
copy /packages/system/scheduler /workspace scheduler
add /workspace (clock (description "A live counter. tick adds increment to value; reset clears value.") (value 0) (increment 1) (handlers (tick (body (write (slot "value") (value (add (left (read (slot "value"))) (right (read (slot "increment")))))))) (reset (body (write (slot "value") (value 0))))))
link /workspace/scheduler candidate /workspace/clock 99
set /workspace/scheduler/candidate-name "clock"
set /workspace/scheduler/candidate-periodic true
call /workspace/scheduler register
drive /workspace/scheduler
tick
tick
show /workspace/clock/value
```

The value is 2. Candidate rights 99 are READ|WRITE|DELEGATE|ACT. Registration grants
the entry's worker connection rights 67 (READ|WRITE|ACT). Duplicate workers or
names are rejected. A member is prepared under reserved `pending-entry` before
kernel move publishes it in `entries`; grant failures do not publish a half-built
member. Allocation exhaustion can leave the isolated draft for explicit reset.

Entries are data records, each with `enabled`, `periodic`, `error`, and a `target`
connection to a worker outside the entry catalogue. The prototype is data-only;
do not put workers/resources/messages inside catalogue entries. Direct graph
editing remains available, but can violate this reference policy's schema.

Before registering a different candidate, use `unlink ... candidate` then `link`
with the next grant. `selected` is a zero-based index in the current catalogue:

```text
set /workspace/scheduler/selected 0
call /workspace/scheduler unregister
```

Unregister removes membership, not the worker or its queued messages. A worker
already captured for the current turn remains eligible once in that turn.
Register/remove/enable/periodic changes take effect in the next membership snapshot.
The native operator commands `register`/`unregister` still refer to the separate
legacy scheduler; `call SCHEDULER register/unregister` invokes image policy.

## Turn boundaries and ordering

Each `tick` captures an owned metadata snapshot under reserved `turn`. It includes
the current entry flags and worker connections, not copies of worker objects.
The snapshot is removed after the turn. Entry removal or configuration changes
while workers run cannot shift the iteration indices or skip a captured worker.

The image's `cursor` rotates the first entry on each turn. Every captured enabled
entry receives at most one dispatch and its optional periodic `tick`, in that
order. Finite, successful turns therefore visit all entries without a fixed first
worker advantage. Host budgets still bound the entire invocation; an unbounded
worker interrupts it and requires explicit recovery, not automatic time slicing.

The native evaluator captures the submission cutoff once per outer behavior
invocation. Nested dispatches inherit it, so messages submitted by workers in that
turn become eligible on the next outer invocation. No image-supplied number can
forge a new cutoff or message authority. Native scheduler ticks retain their
existing global submission cutoff.

`tick` returns a nonnegative work count: successfully dispatched messages, periodic
invocations, and ordinary failed attempts. Zero means no work was attempted. When
resumed through the native operator, zero requests a 1 ms host wait between polls.
Runnable periodic work receives no added pacing. A periodic handler polling I/O
is still runnable; graph-owned asynchronous completion/wakeup protocols remain
later work. CLI input is checked between bounded turns.

## Failure records and recovery

An ordinary entry failure records `error`, `error-operation` and `error-receiver`
on the original entry and disables it. The scheduler also retains the latest
`last-error`, `last-operation` and `last-receiver`. These identities are diagnostic
observations, never capabilities. Later entries continue. If an entry was removed
during the turn, only the scheduler's latest diagnostic remains. Successful work
clears that surviving entry's error fields; the latest global diagnostic remains.

After repairing a periodic handler, select its entry and call `enable`:

```text
set /workspace/clock/increment "broken"
tick
show /workspace/scheduler/entries/clock
set /workspace/clock/increment 10
set /workspace/scheduler/selected 0
call /workspace/scheduler enable
tick
```

For a failed message, call scheduler `retry` or `discard` instead. They perform
the explicit receiver lifecycle decision and then enable the selected entry.
Retry restarts the handler and can repeat earlier effects; discard completes its
lifecycle without asserting request success. `enable` alone does not clear an
active failed message. No retry, discard, rollback or resource restoration occurs
automatically.

Step/allocation exhaustion bypasses image recovery. The native operator pauses
activity. A partial turn leaves `running=true`; reentrant `tick` is refused.
Repair or disable the culprit and explicitly reset before resuming:

```text
pause
set /workspace/scheduler/entries/clock/enabled false
call /workspace/scheduler reset
```

Reset removes reserved `turn`/`pending-entry` scratch objects and clears the running
marker. It does not undo effects, acknowledge messages or retry anything. Invoke
it between turns through the operator; do not call it from a running worker.

## Native/image separation and checkpoints

Selecting an image driver refuses active/failed native registrations. Native
operator registration is blocked while an image driver is selected. Protected
`dispatch` also refuses a natively scheduled receiver, including embedding calls
that bypass the operator. The legacy scheduler remains an explicit recovery/test
option; switching drivers does not translate their state.

Membership, cursor and failure fields are ordinary graph state. Save after a
completed turn or explicit reset. Partial message checkpoints remain supported;
the host's selected driver and resume flag are not saved. Reopen paused and choose
`drive PATH` explicitly. See the [versioned checkpoint contract](checkpoints.md).

## Verification

`neo selftest` runs 78 expression cases plus scheduling groups: deferred delivery,
rotating first entry, mid-turn removal snapshots, registration grant failure and
duplicates, explicit image retry/discard, native/image exclusion, reentrant ticks, budget interruption/reset and
idle waiting through a fake callback. Existing lifecycle, checkpoint and ownership
groups also pass. Windows x86_64 was rebuilt; Linux/sanitizers were not rerun.

[Scheduled exposure sessions](../../neo/packages/session.md) now use this scheduler
unchanged. Workers retain explicit self-scoped runtime grants to manage resources;
scheduler dispatch remains rights 67. Real loopback reconnect/resync is covered by
the universal selftest. Periodic I/O polling still counts as scheduler work.
