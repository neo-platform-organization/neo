# Verified runtime foundation

[VM documentation](../README.md)

This is the consolidated implemented-status reference from the first live-image
roadmap milestones, verified on Windows x86_64 on 2026-10-22. It distinguishes
working mechanisms from the remaining environment work.

## Feature status

| Area | Implemented behavior | Remaining limit |
| --- | --- | --- |
| Launch | One VM per process; one-shot CLI, GUI, or both through common launch flags. | `live IMAGE` is a separate terminal operator mode; there is no live OS GUI frontend yet. |
| Live editing | Native recovery commands plus image-owned environment requests for inspection, workspace mutation, invocation, scheduling, failures and save readiness. | Service selection is opt-in; no live graphical environment yet. See [environment services](environment-services.md). |
| Recovery | `attempt`, `failure`, `rethrow`; diagnostic IDs plus references bounded by an explicit existing READ scope. | No frozen failure snapshots, reflective activations or instruction resumption; host limits bypass recovery. |
| Messages | Protected creation, grants, submission, acceptance, payload access, inspection, begin, finish, retry and explicit payload release. | No automatic cancellation/rejection/retry. Orphans without any surviving cleanup authority remain until teardown. |
| Native scheduler | Registration order; boundary acceptance cutoff; at most one message then optional periodic tick per receiver. | A failure stops that scheduler invocation and pauses automatic operator activity. |
| Image scheduler | Image-owned registration/removal, metadata snapshots, rotating first entry, shared dispatch cutoff, diagnostic records, explicit recovery and idle work reporting. | Sequential, globally budgeted reference policy; asynchronous completion and resource restoration remain later work. |
| Ordinary formatting | `neo_image_format` and CLI `format` preserve ordinary graphs and connection relationships. | Reject ETHER/messages/queues, resources, and enabled/failed native registrations. |
| Checkpoints | Version 1 `neo_image_checkpoint`, used by `save`, preserves graph relationships, ETHER roles, message states, grants, submissions, active request and accepted queue order. | Native registrations/resources are rejected; no C activation is resumed. Processing requires explicit recovery. See [checkpoint contract](checkpoints.md). |
| Packages | Local schema-1 artifacts, exact dependency pins, protected staging/bindings/baselines, export, guarded update/removal, image manager and terminal browsing. | One version per ID, explicit activation; no live migration, registry or graphical browser. See [local packages](packages.md). |
| Spatial model | Maths/shape test fixtures and separate rendering demo images, excluded from the OS seed. | Common numeric/frame/transform/projection contracts and n-dimensional GUI remain unfinished. |

Object identity relationships survive serialization through remapped labels;
numeric IDs need not remain equal across sessions. Saved graph scheduler entries
and their error fields are ordinary objects. The native operator's selected driver
and running flag are not saved: reopen paused and explicitly select `drive PATH`.

## Message and failure behavior

Message states are `created -> pending -> accepted -> processing -> processed`.
Retry returns a processing request to the front of the accepted inbox. Acceptance
preserves pending submission order for each sender/receiver pair; accepted queue
order then determines begin order. Native scheduler acceptance uses one cutoff per
tick. Image dispatches share their outer behavior invocation's cutoff; work emitted
during that invocation waits for the next outer invocation. See
[image scheduling](image-scheduling.md) for membership, failure and idle policy.

The maker can edit before acceptance. Submitted grants and recipient are fixed;
accepted payloads, including nested fields, are immutable for every actor.
Message links carry zero ordinary object rights; message APIs recheck actor policy.
Generic object access cannot bypass it. Sender/receiver diagnostic numbers do not
grant references. Only the recipient accepts/begins/finishes its request.

The evaluator exposes `message-create`, `message-link`, `message-info`,
`message-read`, `message-write`, `message-field`, `message-grant`, `message-submit`,
`message-accept`, `message-finish`, `message-pending`, `message-begin`,
`message-active`, `message-retry`, and `message-release`. It also exposes `dispatch`,
`receiver-retry`, and `receiver-discard` for image scheduling. See
[messages.h](../../neo-vm/messages/messages.h) for the protected C contract and
[message_ops.c](../../neo-vm/execution/message_ops.c) for exact operand names.

`dispatch` requires target READ|ACT, refuses a natively scheduled receiver, accepts
pending work, and invokes at most one text-selected handler. Success finishes its
message; failure leaves it processing for explicit retry/discard. A handler cannot
finish or retry a request owned by an active scheduler/evaluator frame.

The image scheduler catches ordinary errors, records status in its entry's `error`
field, disables that entry, and continues with later entries. Budget exhaustion or
allocation failure is not caught and escapes to the operator. Retrying a failed
message is separate from re-enabling the image entry; earlier effects remain.

Completed messages remain retained until explicit release or teardown. Deleting
an inactive participant does not change message state; inspection reports sender
and receiver availability. The milestone-2 contract adds authorized release of
completed/unsubmitted payloads and receiver-orphaned payloads, plus failure
references constrained by existing authority. See [messages and failures](message-lifecycle.md).
Checkpoint encoding still rejects retained missing participants and stale links.

## Checkpoint guarantees verified here

The built-in `selftest` now checks all five stored message states, a processing
request, a two-message accepted inbox, pending work, restored observer read grants,
denied inspection and edits, and inbox order after parse. It also checks the live
`drive` command workflow, entry disable/repair, and rejection of native registration
while using an image driver. These extend the original 60 expression cases and
authority/lifetime/copy/allocation/OS-image tests; no separate executable is built.

An additional process check saved a pending request, loaded it in a fresh VM,
explicitly selected the saved image scheduler, dispatched it, and saved its
processed state. A native-registration save rejection left an existing destination
unchanged. These establish a partial between-turn checkpoint, not arbitrary
instruction resumption or complete crash recovery.

Fresh Windows build: `./build-windows.ps1 -Test`. Linux source/backend exists, but
was not rebuilt during this audit: the available WSL Debian has no `cc`, `gcc`,
`clang` or `make`. Earlier Linux/sanitizer results are historical, not a new
cross-platform validation of this checkpoint.

For reproducible operator workflows, see the [live image guide](live-image.md).
