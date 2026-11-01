# Integrated networking capacity and restore

[VM guides](../README.md) · [Checkpoint contract](checkpoints.md) · [Session policy](../../neo/packages/session.md)

The first Windows N8 workflow is verified with the generic OS seed, installed
exposure0.3/session0.7 packages, publisher/subscriber and two session instances.
The OS seed remains generic; networking is installed/configured explicitly.
Linux networking remains stubbed by user instruction.

## One bounded host build profile

The original4096-object ceiling was duplicated across the decoder, encoder,
object imports, package schema checks, catalogue publication and baseline equality.
They now share [resource_limits.h](../../neo-vm/resource_limits.h):

| Bound | Current profile |
| --- | --- |
| Objects in a decoded/encoded image or completed import/catalogue publication | 131072 |
| Image/checkpoint source and formatted output, including header | 4 MiB |
| Containment depth | 128 |
| Decoded token/string bytes | 4096 |
| Snapshot wire payload | 1 MiB, unchanged and independently enforced |
| Replica transfer chunk | 4096 bytes, unchanged |

These are trusted native build limits, not editable image policy. Changing the
profile requires rebuilding the VM. Depth/token limits and authority checks did
not broaden. Source/checkpoint and replica wire versions remain1; old VMs may
reject larger images under their smaller local resource profile. Graphs within
the old profile remain compatible. A snapshot must fit both the codec and the
separate wire-byte limit; a large local OS image is not implicitly publishable.

The profile bounds codec/import/package operations, not global allocator usage.
Ordinary live creation can exceed what will serialize, and multiple images have
independent codec bounds. There is no new global memory quota, wall-clock budget
or guarantee every topology is fast. Namespace encoding no longer performs cubic
ordered pair traversal for duplicate names; uniqueness checks remain enforced.

## Measured integrated workflow

The test installs both packages through the OS image package manager, clears
staging, creates publisher/subscriber and two session instances, grants local
scope/service/runtime connections, and enables pinned TLS/replies/heartbeats.
The complete base image contains13536 objects and saves as a624975-byte version1
checkpoint in the checked build. Exact byte counts/IDs can change with source.
This leaves52000 objects of the current profile for this measured base, not a
promise of room for arbitrary future packages or application data. Re-evaluate
sizing when the seed/packages change; do not silently enlarge every bound.

Both live OS processes use this complete installed base, not the earlier minimal
network fixtures. Each process schedules one selected session. Their catalogue
baselines, other unused service/session copies, OS tools and workspace coexist
in each image. Publication snapshots expose only the explicit scope.

## Save and resume

Pause at the operator boundary, stop the session, and remove its live authority:

```text
pause
call /workspace/outgoing stop
unlink /workspace/outgoing network
delete /workspace/outgoing/host-network
save environment.neo
quit
```

Use incoming instead of outgoing on the receiving side. Stop retains actual
candidates/accepted records, publication counters and last observed replies;
transport staging and pending wire flags are discarded under the documented
session policy. Do not clear counters/candidates to make a save succeed. Other
live resources also require their normal quiesce/remove procedure. Native handles
are rejected, never silently stripped by the encoder. Saving may fail if the
program is inconsistent or over its profile; it does not repair arbitrary state.

Reopen the checkpoint with `neo live environment.neo`. Loading executes nothing.
The session remains stopped; native endpoints/streams, operator driver selection,
and resume state are absent. Image scheduler memberships/ordinary local grants
survive through newly allocated identities. Provision a fresh immutable host grant:

```text
net-grant-tls /workspace/outgoing connect 127.0.0.1 9000 LOCAL_SHA256 PEER_SHA256
call /workspace/outgoing start
drive /packages/system/scheduler
resume
```

The receiver separately grants incoming listen at the same port. Supply actual
provisioned fingerprints; no test identity is a production credential. Register
the session only when first configuring the image; persisted membership is reused
after restore. Each side controls its own grants. A checkpoint cannot recreate
network authority from saved text, carry private keys, or skip a new handshake.

Reconnection publishes fresh full state at a new preserved revision. If an old
candidate is undecided, the receiver keeps it and can defer one new snapshot while
answering probes. Accepting that old candidate does not acknowledge the new
publication. Only explicit decision on the new validated candidate produces its
correlated terminal reply. Accepted prior copies remain unchanged.

## Reproduce verification

Build the Windows target, then run from the repository root:

```powershell
./build-windows.ps1 -Compiler C:/msys64/ucrt64/bin/gcc.exe -Test
./neo-vm/platform/win32/tests/tls_live.ps1 -Python python
```

The optional Python runtime is test tooling only. The PowerShell script creates
and removes temporary current-user TLS identities in finally; the full-OS test
[network_restore.py](../../neo-vm/platform/win32/tests/network_restore.py) runs after
the transport/liveness tests with the same public pins. It uses ephemeral loopback
ports, not a public service. Logs/checkpoints stay in ignored build output.

Checks include:

- Exact65536-object parse/format success, excess-object parse/format rejection,
  atomic over-capacity import failure and source beyond4MiB rejected.
- A padded valid OS checkpoint beyond the former1MiB file-reader ceiling loads.
- Full OS package install/instances/save, TLS receipt1 and explicit acceptance1.
- Receipt2 held as an undecided candidate while accepted value42 remains.
- Stop/remove resources/save, inert reload, retained counters/candidate/accepted
  state, fresh host grants on a new port and a new pinned handshake.
- Resync revision3 deferred behind old candidate2; deciding2 does not acknowledge3.
  Explicitly deciding3 advances the publisher observation; accepted42 stays intact.
- Final save/reload validation, no live endpoint in the checkpoint, and existing
  allocation/authority/lifetime/message/scheduler/package/TLS regressions.

This completes the initial Windows networking foundation N1–N8. It does not finish
GUI/spatial work, production failure-detector tuning, durable exactly-once receipts,
discovery, application merge rules or global host resource accounting. The next
roadmap item is the unfinished dimension-driven spatial library milestone9.

Milestone9 subsequently raised only the object bound from16384 to65536 after
co-installing spatial packages exposed the smaller profile. Source bytes, depth,
token and replica wire bounds remain unchanged. The integrated spatial/network
catalogue is covered by the [spatial audit](spatial-audit.md); the original N8
13536-object measurement above remains a historical networking-only base.
