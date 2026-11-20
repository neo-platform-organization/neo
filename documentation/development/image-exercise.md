# Image lifecycle exercise

The canonical `neo/neo-os/image.neo` contains `lifecycle-exercise`.
This is an acceptance exercise, not a desktop. Its handlers own editing,
save/restore choices, dirty state, drawing, and timer behavior. Knight
functions perform arithmetic. `exercise-outsider` tests denied access.
Existing foundation and surface exercises remain in the same image.

## Run on Windows

From the repository root, create a directory for the granted byte store:

```text
mkdir exercise-data
.\build\x86_64\Windows\neo.exe exercise .\neo\neo-os\image.neo lifecycle-exercise .\exercise-data
```

On Linux, use `./build/x86_64/Linux/neo` with the same arguments.
The store directory must already exist. It is never chosen by image code.
This persistence exercise requires a hosted storage backend. Standalone
x86_64 still lacks a persistent storage driver.

Click to increment the value. Composed text adds its UTF-8 byte length.
F2 saves, F3 discards unsaved changes, and Escape closes. The top strip is
green when the value matches the last save and orange when it differs.
White pixels encode the current value, clipped to the framebuffer.
The window close control also stops the session. Closing does not save.
Reopening starts from the explicitly saved value.

There are intentionally no widgets, font system, or desktop dependencies.
This is a test of the boundary and lifecycle, not a proposed visual design.

## Automated checks

```text
make test TARGET=x86_64/Windows
make test TARGET=x86_64/Linux
```

Make's regression driver creates a private temporary store. Two independent
processes run the exercise: the first restores zero and saves five; the
second restores five and saves ten. The checks cover:

- Button and text messages changing graph state and image-generated pixels.
- Explicit package grants, a denied outsider, and rejection of foreign handles.
- Binary scalar persistence, including a save of an unchanged value.
- Arithmetic failure discarding both graph and document changes.
- A denied save preserving the prior durable value and saved-state marker.
- Rejection of a send combining durable storage with another external effect.
- Rejected live edits preserving the active graph; accepted edits retaining
  explicitly granted runtime services.
- A timer delivered through the ordinary event pump, with injected time.
- Stop, pending timer cancellation, surface revocation, cancellation of pending
  I/O exactly once, and session teardown.
- Graph snapshots saved through the managed byte store and reopened without
  authority. Another process reads that checkpoint and opens it as Warp.
- Unsaved edits disappearing when a fresh session opens the canonical source.

For a manual test directory:

```text
neo exercise-test neo/neo-os/image.neo lifecycle-exercise exercise-data
neo exercise-snapshot exercise-data
```

`exercise-test` modifies its granted directory. It stores a binary document
and a graph checkpoint; it is not a read-only diagnostic. The snapshot
command prints validated Warp source. Interactive F2 saves the document
only; automatic whole-image checkpoint management is not implemented.

## C boundary and limits

The hosted harness creates a surface, grants one scalar document to the
selected package, dispatches normalized input, polls scheduled messages,
waits while idle, and releases resources. It contains no editing or drawing
rules. The test branch necessarily knows the exercise's expected results.

The scalar document now supports an optional durable commit callback.
`write-document` stages its value during native execution. After validation,
the callback conditionally replaces one managed byte-store record before
publishing graph writes. Rejection leaves the previous graph and document
unchanged. The bridge serializes a version marker and an explicit
little-endian signed 32-bit payload, not a native C structure.

Durable writes cannot share a send with scheduled messages, framebuffer
publication, or object lifecycle operations. This prevents a false promise
of atomicity across unrelated external effects. Storage uncertainty is
reported as an error; the harness does not automatically retry a possibly
completed write. An uncertain save requires reopening or inspecting storage
before retrying. Competing writers receive a conflict rather than overwriting
each other's revision.

This is a narrow scalar persistence bridge, not a general image-facing
asynchronous storage API. Networking and arbitrary byte-store operations
still need graph bindings. The snapshot and binary document are independent
records, not one atomic whole-image transaction. Power-loss recovery,
process termination during an exercise save, general application ownership,
and stable graph identity across durable checkpoints remain further work.
Existing platform tests cover conditional replacement and complete-record
recovery separately. The exercise does not establish that the OS is finished.
