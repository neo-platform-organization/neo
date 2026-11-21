# Language foundation acceptance

Platform update, 2026-11-21: Windows/headless presentation, normalized
input, asynchronous transport, managed byte storage, and loopback
networking now have initial implementations. See [platform services](../reference/platform-services.md)
for current capabilities. The absence of those services described in
the original language-only checkpoint below is historical.

Verified on Windows x86-64, 2026-11-20. This closes the current bounded
language-foundation milestone sequence. It establishes a base for the
first image-owned interaction and presentation work. It does not mark
the desktop, OS, engine, or complete language design as implemented.

## Reproduce the gate

From the repository root:

```text
make test TARGET=x86_64/Windows
```

Output is `build/x86_64/Windows/neo.exe`. The canonical image remains
`../neo-image/neo-os/image.neo`. Acceptance fixtures are temporary test data;
the build does not publish additional images.

## Integrated proof

The test lowers a Weft package into Warp and runs it through the native
JIT. A handler schedules a continuation, invokes an explicitly granted
service import, updates its local state using Knight, and schedules the
next step. Three steps produce 27. A live provider edit changes its
calculation while retaining the client's state. Another step produces 37.

A snapshot saves the current code and values. Reopening it restores 37,
but the import grant is absent. The next queued service call fails and
leaves 37 intact. A new grant permits another step, which produces 47.
The suite compares every completion and final value, not just exit codes.

The native headless event test injects time and captures completion
callbacks without terminal output or real clock delays. A message at
time 100 schedules a child for 105, which schedules a final child for
110. Polling early cannot execute a child. A poll processes only its
initial queue, so self-scheduling cannot monopolize one poll call.

## Other verified contracts

| Contract | Coverage |
| --- | --- |
| Graph and views | Warp round trip, aliases and cycles; Weft lowering and projection; inert inspection; bounded Lattice projection |
| Explicit interpretation | Detached Knight probing, pure state-transition execution, revision-checked publication |
| Values | Integer, text, bytes, integer vectors, graph-local references, binary64, and binary64 vectors within fixed buffers |
| Objects | Receiver-local delegated state, live reference dispatch, cloning, deletion, and lifecycle rollback |
| Authority | Import/export boundaries, grant revocation, package-scoped document grants, and fresh-session grant denial |
| Transactions | State and document rollback after overflow, native failure, missing scheduler, or queue rejection |
| Execution | Native worker reuse, process crash containment, timeout, recovery, and explicit error reporting |
| Scheduling | Timers, cancellation, asynchronous bounded text reads, image post/after, batch limits, stale revisions, and idle-console delivery |

The scheduler's completion callback carries an event ID, optional error,
and integer result. The CLI supplies console formatting. A future GUI
can consume completions without changing delivery code. Clock, waiting,
and file-read callbacks remain separate from completion reporting.

## Limits and next boundary

This is a Windows-hosted prototype. The new VM has no graphical adapter,
OpenGL backend, metal target, audio, or network implementation yet. Old
artifacts and retired guides do not establish support in the new VM.

Graphs, code, values, activations, and queues have fixed limits. Most
rich values use a 64-byte source token. Calls have up to four arguments.
The queue holds 32 pending operations, with at most eight new requests
per send. Delays in image code are nonnegative integer literals. Queue
requests currently carry integer values, not arbitrary rich messages.

Scheduling is sequential. Cancellation removes pending work; it does
not interrupt an active native send. Snapshots save graph values and
definitions, not pending queues, active native frames, or grants.
There is no unrestricted filesystem authority or network completion.

The trusted parser and compiler run in the broker. One native worker
contains a whole image; package access checks are compiler-enforced.
Process isolation is not a proof of compiler correctness or a separate
memory compartment for each package. General garbage collection,
unbounded collections, and JIT caching remain future work.

The original next phase was to establish normalized input and a framebuffer
presentation contract, with deterministic headless replay. Implement
one graph-owned visible interaction before desktop applications.
Controls, layout, drawing policy, and application behavior belong in
the image. Platform adapters supply input and buffer transport.

Current checkpoint: the lifecycle exercise now verifies this boundary on Windows and Linux. See [image exercise](image-exercise.md) for the tested contract and unfinished OS prerequisites.
