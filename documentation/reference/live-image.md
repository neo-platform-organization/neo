# Live terminal image

[Documentation index](../README.md)

```sh
make
./build/x86_64/Linux/neo live neo/neo-os/image.neo
```

On Windows, build with `./build-windows.ps1 -Test` and launch
`./build/x86_64/Windows/neo.exe live neo/neo-os/image.neo` from the repository root.
See the [verified foundation](runtime-foundation.md) for the consolidated status.

The VM loads the image once. Commands inspect and mutate that same graph until
`quit` or input EOF. There is no automatic save. This is a trusted native operator
interface, with root authority over the loaded image; it is not yet an editor
implemented in neo. Invoked behavior receives its receiver's authority and explicit
connections, not an implicit reference to the image root.

## First experiment

Enter these commands at the `neo>` prompt:

```text
ls /
ls /packages
ls /packages/system/environment
add /workspace (clock (description "A live counter. tick adds increment to value; reset clears value.") (value 0) (increment 1) (handlers (tick (body (write (slot "value") (value (add (left (read (slot "value"))) (right (read (slot "increment")))))))) (reset (body (write (slot "value") (value 0))))))
call /workspace/clock tick
show /workspace/clock
set /workspace/clock/increment 10
call /workspace/clock tick
show /workspace/clock/value
```

The value is now 11. The example explicitly created a worker in the workspace;
the OS seed does not carry it as a built-in sample. You edited the live object
and ran its behavior without reloading.

## Commands

Paths are absolute containment paths starting at the image root. `/` and `.` mean
the root. Whitespace and slash in names cannot currently be addressed by this
operator command syntax. Commands and neo fragments occupy one line (8191 bytes
maximum); terminal line editing is provided by the host terminal.

| Command | Effect |
| --- | --- |
| `help` | Show available commands. |
| `ls PATH`, `show PATH` | Show payload, direct children, and connections with their rights. |
| `set PATH VALUE` | Replace a payload using neo notation: integer, Boolean, string, or integer array. |
| `add PARENT (name ...)` | Parse and insert an inert subtree. Reject imported connections; undo partial insertion on failure. |
| `copy SOURCE PARENT NAME` | Kernel duplication with internal connection remapping. |
| `move SOURCE PARENT NAME` | Kernel duplication followed by deletion; old identities become unavailable. |
| `rename PATH NAME` | Rename without changing identity; conflicting names fail. |
| `delete PATH` | Delete an ordinary subtree; the image root cannot be deleted here. |
| `call PATH HANDLER` | Execute with a 100000-operation budget and print result or failure location. |
| `register PATH`, `unregister PATH` | Enable or disable native scheduler participation. |
| `tick` | Run one native scheduler boundary or the selected image driver's `tick` handler. |
| `resume`, `pause` | Start or stop ongoing scheduling while waiting for terminal input. |
| `send SENDER RECEIVER HANDLER` | As trusted operator, submit a text-selector message to the receiver. Creates ETHER if absent. |
| `drive PATH`, `drive native` | Select an image object's `tick` handler or the native scheduler; pauses automatic activity. Selecting an image driver rejects active native registrations. |
| `ether PATH` | Create image ETHER if needed and grant the target a READ connection named `ether`. |
| `link OWNER NAME TARGET RIGHTS`, `unlink OWNER NAME` | Create/remove an explicit connection; RIGHTS is a decimal mask. |
| `retry PATH`, `discard PATH` | Explicitly resolve a paused scheduled failure. |
| `save FILE` | Atomically replace a host file with a supported quiescent image. |
| `service PATH` / `service native` | Select image environment policy or return to native recovery mode. |
| `request ACTION` | Submit a bounded request to the selected service's `execute` handler. |
| `pkg-load FILE`, `pkg ACTION` | Stage an inert artifact and invoke the image package manager. |
| `pkg-list`, `pkg-info ID`, `pkg-export FILE` | Browse catalogue/manifests or export the selected package. See [local packages](packages.md). |
| `quit` | Destroy this VM session; does not save. |

Behavior is editable data. For example, `add` can insert a new handler under an
object's `handlers` child; `set` can change an operand in an existing body. Pause
ongoing activity before multi-command structural edits. Each individual command
runs between behavior turns, so a running C activation is never edited in place.
Syntax errors are reported before insertion. Name conflicts do not replace existing
objects. Separate delete/add commands are not a transaction.

## Ongoing activity and failures

```text
register /workspace/clock
resume
pause
show /workspace/clock/value
```

While waiting for input, the VM runs registered objects using the existing
sequential scheduler. Objects with a `tick` handler execute it; accepted messages
select the receiver's named handler. No fixed application tick rate is imposed.
The terminal and scheduler share one thread. Input is checked between bounded
turns; long turns therefore delay input. This is not preemptive execution.

A scheduled failure pauses automatic activity and reports receiver, operation,
and step count. Earlier mutations remain. Repair the graph, explicitly choose
`retry PATH` or `discard PATH`, then `resume`. Retry starts the failed handler again
and may repeat effects. Discard completes a failed message; discarding a failed
periodic tick disables that receiver. Direct `call` failures return to the prompt.
These are operator policies layered over the existing native scheduler. Image
code can use `attempt`, `failure`, and `rethrow` for explicit non-resumable
recovery. The OS also includes an image scheduler prototype with a different
per-entry failure policy, described below.

## Image-owned scheduler experiment

Start a fresh `live` session and enter:

```text
copy /packages/system/scheduler /workspace scheduler
add /workspace (clock (description "A live counter. tick adds increment to value; reset clears value.") (value 0) (increment 1) (handlers (tick (body (write (slot "value") (value (add (left (read (slot "value"))) (right (read (slot "increment")))))))) (reset (body (write (slot "value") (value 0))))))
add /workspace/scheduler/entries (clock (enabled true) (periodic true) (error ""))
link /workspace/scheduler/entries/clock target /workspace/clock 67
drive /workspace/scheduler
tick
tick
show /workspace/clock/value
```

The value is 2. Rights 67 grant READ|WRITE|ACT to the scheduler entry's target.
Do not also register `clock` with the native scheduler. The image policy dispatches
one queued message, if any, then invokes `tick` for enabled periodic entries.
The scheduler now also owns registration/removal and recovery handlers, snapshots
membership and rotates the first entry. For that interface and host-limit reset,
see [image-owned scheduling](image-scheduling.md). A zero-work result lets the
native poll loop wait briefly instead of spinning when idle.

Test failure and repair:

```text
set /workspace/clock/increment "broken"
tick
show /workspace/scheduler/entries/clock
set /workspace/clock/increment 10
set /workspace/scheduler/entries/clock/enabled true
tick
show /workspace/clock/value
```

The entry records `wrong kind` and becomes disabled. The final value is
12 after repair. An ordinary entry failure does not stop later entries. Host
execution limits still escape this image recovery.

To dispatch a message without native registration:

```text
send /workspace/clock /workspace/clock reset
tick
show /workspace/clock/value
```

The result is 10: dispatch runs `reset`, then the periodic tick adds 10. For a
failed message, repair its handler, use `retry RECEIVER` or `discard RECEIVER`, and
explicitly re-enable its image entry. Re-enabling alone does not clear an active
failed message. Switching drivers does not translate their scheduling state.

## Save and reopen

```text
pause
unregister /workspace/clock
save saved-image.neo
quit
```

```sh
./build/x86_64/Linux/neo live saved-image.neo
```

The Linux backend writes a temporary file in the destination directory, flushes
it, and renames it over the requested destination. Serialization completes before
any destination is touched. A failed serialization leaves an existing file intact.
This provides atomic replacement, not a promise of directory durability after a
power failure. Saving requires write access to the destination directory.
Windows uses a unique sibling file, flush, and native replacement through its
platform adapter. Both serialize before opening the destination replacement.

`save` uses the checkpoint encoder. ETHER, messages, grants, active-message state,
and accepted inbox order can be saved between invocations. Image scheduler entries
are ordinary graph objects and survive too. Reopening is inert and paused; select
`drive /workspace/scheduler` explicitly before `tick` or `resume`. A restored
processing request is still active and needs explicit retry/discard; no handler
stack or instruction position is restored.

Enabled/failed native registrations and external resources still reject saving.
`pause` alone does not unregister objects. Missing message participants or stale
connections also prevent a valid checkpoint. The separate `format`/`clone` commands
still reject messages and ETHER. Operator stdin lives in a separate host image,
outside the saved graph. Saves use `!neo-checkpoint 1`; legacy unversioned inputs
remain readable. Automatic resource restoration remains later work; see the
[checkpoint contract](checkpoints.md).

## Current boundary

Image-owned workspace/edit/execution requests and save-readiness policy are now
available through the opt-in [environment service](environment-services.md).
Native commands remain the recovery interface; selecting a service is not saved.

There is no new GUI mode in the live editor. Existing standalone rendering demos
remain unchanged. Complete failure/message protocols, runtime-state persistence,
image-owned scheduling, core OS libraries, and dimensional contracts precede any
new OS GUI work. No package filesystem splitting or external editor is needed for
these live experiments.
