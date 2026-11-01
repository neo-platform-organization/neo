# Read the VM

The current image desktop is a single top bar with Apps dropdown and pinned/current
app navigation. Task Manager is an ordinary app, implemented in
neo/packages/task-manager.neo; close is the only window-title button. See the
desktop guide for the retained pan/zoom navigation and current launch commands.

[VM documentation](../README.md)

Start with [main.c](../../neo-vm/main.c): it forwards process arguments to
[cli.c](../../neo-vm/cli/cli.c). Utility arguments inspect or run an image;
[launch.c](../../neo-vm/cli/launch.c) selects CLI, GUI, or both interfaces, loads one
image, grants resources, and invokes the selected receiver. Both interfaces share
that VM and image lifetime.

Read [objects.c](../../neo-vm/objects/objects.c) next. It allocates identities,
contains payloads, validates capabilities, and performs independent deep copying
with internal-reference remapping. C storage implements the graph; it is not a
class system. Duplication stays in the kernel.

[image.c](../../neo-vm/image/image.c) constructs an inert graph, then resolves
connections. Loading does not execute it. [evaluator.c](../../neo-vm/execution/evaluator.c)
walks receiver-owned behavior objects, dispatching protected primitive mechanisms
in C. `==`, `!=`, `<`, `>`, `<=`, and `>=` are primitive object names with two
operand children. They retain the object-graph syntax.

[message_ops.c](../../neo-vm/execution/message_ops.c) bridges authenticated message
operations, dispatch, recovery and explicit release into the evaluator. The
mechanisms in [messages.c](../../neo-vm/messages/messages.c) enforce state/policy
and retain stale-handle tombstones after releasing payloads. Failure-reference
inspection in the evaluator checks a caller-supplied existing READ scope; numeric
diagnostics never manufacture authority. See the
[message/failure contract](../reference/message-lifecycle.md).

[messages.c](../../neo-vm/messages/messages.c) controls message authority and
acceptance. [scheduler.c](../../neo-vm/execution/scheduler.c) supplies the current
legacy sequential host policy. The reference policy now lives in the OS image's
`packages/system/scheduler`: membership, rotation, failure isolation and recovery
are graph-owned. The evaluator retains bounded execution and captures one message
submission cutoff across nested invocations. See the
[image scheduler contract](../reference/image-scheduling.md).
Queues/activations are not yet fully graph-resident.

The image codec emits `!neo-checkpoint 1` for live saves. It validates all nodes,
edges and message metadata before publication; processing requests reopen blocked
for explicit recovery. Legacy unversioned graphs remain readable. See the
[checkpoint contract](../reference/checkpoints.md).

[platform.c](../../neo-vm/platform/platform.c) supplies the generic host contract.
The [Linux adapter](../../neo-vm/platform/x86_64/Linux/platform.c) opens sources,
streams, windows, and waits. [io.c](../../neo-vm/io/io.c) and
[display.c](../../neo-vm/display/display.c) validate protected resource access;
[POSIX streams](../../neo-vm/platform/posix/streams.c) and
[X11](../../neo-vm/platform/x11/x11.c) translate native mechanisms.

For a language example, read [shaded-cube.neo](../../neo/shaded-cube.neo). Vertex
coordinates are a packed integer payload owned by the cube, not individual active
objects. Rotation, projection, and rasterization run in neo. The kernel presents
completed bytes. General dimensional protocols are not implemented by this demo.

## Inspecting and changing the world from neo

[graph.c](../../neo-vm/execution/graph.c) evaluates operand values first, then uses
held capabilities to resolve objects. Reference results carry VM-owned handles;
text cannot manufacture one. Mutation uses existing kernel APIs. A chain of active
receivers protects every suspended caller's behavior while `invoke` runs another
receiver under its own authority and the shared instruction budget.

[image.neo](../../neo/neo-os/image.neo) uses these mechanisms for a prototype
catalogue and empty workspace. The browser reads the system catalogue. Sample
workers and maths/shape demonstrations live in `neo/fixtures/`, outside the OS
seed. [selftest.c](../../neo-vm/cli/selftest.c) covers both the actual clean boot
image and these explicit fixtures inside the same executable.

## Live terminal session

[packages.c](../../neo-vm/packages/packages.c) validates package structure, topology
and grants, stages bundle/baseline records, and commits prevalidated replacement
without remaining allocations. The image `package-manager` owns pin matching,
topological dependency ordering, version eligibility and declared-dependent
removal. Terminal `pkg` commands transport requests. See
[local packages](../reference/packages.md). The launcher also supports `neo IMAGE`
through an inert entry declaration or root `main` handler.

The OS image's `packages/system/environment` supplies bounded inspection,
workspace mutation, invocation, failure records and save-readiness policy.
`service`/`request` in [live.c](../../neo-vm/cli/live.c) transport requests to it;
native commands remain recovery tools. Graph views can invoke the same service
against the same objects through existing protected invocation, without another
VM or root grant. See [environment services](../reference/environment-services.md).

`neo live IMAGE` keeps one image loaded for inspection, editing, invocation, and
explicit scheduling. See the [live image guide](../reference/live-image.md) for commands,
a working counter experiment, authority boundaries, and save limitations. The
operator interface remains native C. Partial message/queue checkpoints exist;
selected drivers and native registrations are not serialized.

The optional transport boundary is `platform/network.h`; Windows TCP lives in
`platform/win32/network.c` and attaches connected sockets to the existing
`io/io.c` stream mechanism. It does not implement graph replication.

`replication/replication.c` supplies closed-region snapshot preparation and bounded
framing over existing streams. Receipt buffers bytes; explicit import uses the
existing inert parser and atomic closed-region copier. Ordinary objects remain
the exposed entities; this subsystem supplies transport machinery.

Windows `platform/win32/tls.c` drives Schannel with fixed buffer bounds and direct
certificate-pin trust. `platform/network.c` owns pending authenticated sockets;
`authenticated-peer` exposes READ-protected native identity inspection. TLS tests
remain part of the universal executable, with an optional live-process relay test.


Session package 0.3.0 adds an optional image-owned connect/accept/TLS poll budget.
The Windows universal suite checks exact exhaustion, cleanup and retained candidate
identity; the optional TLS live test checks a silent peer and replenished retry.
See [session policy](../../neo/packages/session.md#connection-attempt-budget-030).


Windows graceful network finish is verified for both TLS initiating roles,
reciprocal EOF, WRITE denial and post-finish write denial. Session 0.4.0 owns
publisher drain/closing policy; the optional TLS live test verifies draining a
prepared update without acceptance and candidate preservation on budget abort.
The single selftest executable remains the regression entrypoint. See
[session finish](../../neo/packages/session.md#graceful-local-finish-040).


Session 0.5.0 handles frame-boundary EOF through replica-ended, reciprocal close
and stopped state. Portable selftest checks ended observations and authority;
partial frames still fail. Windows live TLS checks pending-candidate deferral,
explicit acceptance and clean stopping even with automatic retry enabled. See
[remote-close policy](../../neo/packages/session.md#remote-close-observation-050).


Exposure0.2/session0.6 add opt-in image-only receipt/acceptance/rejection replies.
Universal tests cover partial/would-block replies, stop-and-wait, independent
observations and minimal-catalogue package install/export/serialization. Live TLS
checks explicit decisions, reconnect correlation and invalid-reply retention.
N8 originally verified generic OS seed installation under the then16384-object/4MiB
profile; see the integrated restore checkpoint below. Common session handlers reuse
ordinary calls to keep the combined networking regression within budget. See
[reply policy](../../neo/packages/exposure.md#opt-in-receipt-and-decision-replies-020).


Exposure0.3/session0.7 add image-only heartbeat/idle work policy. Full strict Windows
suite passes, including portable partial probe/response and preserved candidate
identity. Live TLS checks repeated probes, paused peers with open TCP in both
directions, exact controlled idle-budget expiry, deferred reconnect and incorrect
echo denial. Two isolated fixture images retain local scheduler/grant boundaries.
Catalogue installation and exported-image serialization pass separately; combined
OS capacity is verified under the N8 profile below. No Linux networking implementation or C clock was added.
See [network liveness](../reference/network-liveness.md).


N8 Windows verification: shared resource_limits.h bounds parser/formatter, host
file loading, imports, package validation/publication and baseline matching.
The original N8 exact16384-object and over-limit boundaries passed. Encoder namespace validation
no longer performs cubic ordered pair traversal. Both networking packages,
baselines and four instances fit in the full OS seed (13536 objects;624975 checkpoint
bytes in the checked build). tls_live.ps1 now also runs network_restore.py:
full-OS TLS receipt/acceptance, pending state save, inert reopen, fresh grants,
new handshake and correlated resync all pass. A padded source over1MiB loads;
over4MiB is rejected. Depth/token/wire snapshot bounds remain independent.
See [capacity/restore](../reference/network-restore.md). Linux networking stays stubbed.


Vector0.2 is image-only: subtract, fixed-point scale, integer-root magnitude and
precision-preserving normalization. Full strict Windows suite passes dimensions
through257, signed ties, zero/tiny vectors, shape/kind failures, checked raw
product/sum overflow and retained previous results. Optional Decimal oracle
neo/packages/tests/vector_precision.py checks101 vectors/404 operations against
independent80-digit arithmetic and unit-error bounds. OS package install, instance,
save/reopen and normalize pass. No C runtime primitive or seed demo was added.
See [vector numeric contract](../../neo/packages/vector.md).

The matrix0.1 checkpoint adds image-only compact identity, explicit dense
construction/materialization, rectangular application and left×right composition.
Strict Windows selftest covers dynamic dimensions, empty/singular maps, rounding,
shape/overflow/budget failures and retained results. The optional independent
integer oracle passes 72 cases/144 outputs; full OS package install/instance/save/
reopen/recompute passes. See [matrix contract](../../neo/packages/matrix.md).
No runtime primitive or seed demo was added. Inversion and frames remain pending.

Matrix0.2 adds image-only pivoted inversion with checked arithmetic, configurable
small-pivot rejection and two-sided rounded residual validation. Strict selftest,
48 independent rational comparisons, strict residual rejection/retention, existing
144 arithmetic checks and full OS package/save/reopen/recompute pass. Thresholds
are library policy, not a condition/error guarantee; see the matrix guide above.

Matrix0.3 adds image-only affine point/displacement operations, translated
composition and inverse translation with residual validation. Strict suite,
220 independent affine outputs plus overflow/shape/precision retention cases,
previous matrix oracles and full OS install/save/reopen/recompute pass.
See [affine contract](../../neo/packages/affine.md); coordinate frames remain next.

The image-only neo.spatial.frames0.1 package pins matrix0.3 and exports space,
frame and converter prototypes. Explicit space/root identities and parent links
control conversion. Strict suite covers hierarchical points/displacements through
257 dimensions; optional frames_conversion.py checks60 integer-reference results,
dense inward conversion and malformed identity/graph/authority cases. Full OS
install/instance/checkpoint/reopen passes. See [frame contract](../../neo/packages/frames.md)
for rights, scratch mutations and per-edge precision limits. Geometry is next.

The image-only neo.spatial.geometry0.1 artifact adds vertex-major nD vertices,
explicit point/segment/triangle indices and exact checked degeneracy flags.
Strict Windows suite through257 dimensions,144 independent Gram-oracle/extraction
outputs plus invalid shape/kind/index/overflow/budget cases and full OS image
save/reopen pass. See [geometry contract](../../neo/packages/geometry.md).
Package artifacts are imported into one loaded graph, not source modules used
at runtime; the saved image includes installed artifacts and instances.

The image-only projection0.1 artifact pins geometry0.1. Explicit source links
support derived axis views and edits preserving hidden coordinates of the latest
source. Strict Windows suite through257 dimensions and195 independent outputs
across48 shapes pass, including shared-source views, rejected edits and full OS
single-image save/reopen. See [projection contract](../../neo/packages/projection.md).
Projected snapshots require explicit refresh; slices/general projection remain next.

Projection0.2 adds coordinate-band stored-vertex selection, source-index maps and
visibility masks, plus current-membership source-index editing. Ordinary project
refreshes unfiltered maps. Strict suite,157 new slice reference outputs over50
shapes through257 dimensions, previous195 view/edit checks and full OS single-image
save/reopen pass. See [slice contract](../../neo/packages/slices.md). This selects
existing vertices; it does not yet generate edge/face intersections or topology.

The image-only section0.1 artifact pins geometry0.1 and generates segment/coordinate-
plane intersections, endpoint contacts, coplanar topology and rational provenance.
Strict suite through257 dimensions,361 exact-rational oracle outputs over40 shapes,
failure retention and full OS single-image save/reopen pass. Source remains
unchanged. See [section contract](../../neo/packages/section.md). Triangle sections
and general projection remain next; no new runtime primitive/source loader added.

Section0.2 adds a separate image-owned triangle-section export. Exact checked rank
reduces degenerate faces, preserves coplanar faces and generates point/segment
contacts with rational provenance and rounded-collapse flags. Universal regression
uses a bounded250,000-step invocation for worst-case257D crossings (CLI default
unchanged).52 independent cases/780 output arrays, prior361 segment outputs and
full OS single-image install/save/reopen pass. See [triangle section contract](../../neo/packages/triangle-sections.md).

The image-only linearview0.1 artifact adds arbitrary dense affine projection and
explicit caller-supplied lift constraints for editing. Every edit validates the
approximate right inverse and candidate reprojection before mutation. Strict
Windows spatial regressions through257 dimensions use bounded250,000-step calls;
134 independent outputs plus policy/shape/precision/overflow/authority failures
and full OS single-image save/reopen pass. See [general affine view contract](../../neo/packages/linearview.md)
for complement/rounding limits. Perspective/clipping and milestone9 audit remain.

Perspective0.1 adds image-owned homogeneous maps, positive denominator near/far
bounds, optional canonical vertex clipping and explicit source visibility maps.
No edit inverse or whole-primitive clipping is inferred. Strict Windows spatial
suite through257,312 independent outputs across52 shapes, rejected-input/overflow
retention and geometry+perspective full OS single-image save/reopen pass.
See [perspective contract](../../neo/packages/perspective.md). Milestone9 integration/
acceptance audit is next; renderer primitive clipping remains milestone11 work.

Milestone9 acceptance audit complete: current shared host object profile65536,
with4MiB bytes/depth128/token4096 and wire1MiB unchanged. Strict full Windows suite
covers exact/excess boundaries using grouped namespaces;12 independent spatial
reference suites pass. spatial_integration.py verifies8 spatial+2 network packages/
15 instances in one50968-object/2623370-byte OS image, shared edits/frame roundtrip
and complete-image reopen. See [audit and limits](../reference/spatial-audit.md).
Next presentation/input milestone10; renderer clipping and GUI remain later work.

Milestone10 first checkpoint adds portable display/headless.c through the existing
protected window backend. Both build routes include it; universal selftest covers
image-owned presentation, deterministic events/overflow/state, byte captures,
authority, forced failure and ownership cleanup. No visible window or separate
test binary is needed. See [headless contract](../reference/headless-display.md).
Milestone10 remains partial: focus/capture/reset, text, scale/resource loss and
native adapter parity still require work.

Milestone10 focus/capture/reset checkpoint adds optional backend capture, monotonic
input epochs, unqueued reset priority and bounded stale-event filtering. Win32
and headless implement capture; X11 explicitly reports unsupported. Universal
headless tests exercise actual image drag cancellation and capture/authority/
focus-loss/overflow behavior. See [input contract](../reference/input-reset.md).
For committed text, read `display/text.c` for UTF-8 validation and UTF-16 scalar
decoding, then the Win32 `WM_CHAR` adapter and evaluator event snapshot access.
Universal headless tests deliver actual UTF-8 into an image behavior and suppress
stale commits after focus loss. See [text input](../reference/text-input.md).
Scale/loss, IME preedit and native parity remain; GUI policy stays image-owned.

Next read `neo_window_surface_change` and core state acceptance in
`display/display.c`. Scale and presentation availability use one generation and
an unqueued surface notification, with reset taking priority. Headless controls
exercise the same path used by Win32 DPI/paint state. Layout/retry stays in image
behavior. See [surface contract](../reference/surface-state.md) for validation and
remaining native parity work.

`neo_win32_window_selftest` in `platform/win32/window.c` exercises the actual
registered procedure, poll/capture callbacks, core event delivery and native
cleanup with a hidden window. Its GDI bitmap check invokes the same paint path.
Read [native validation](../reference/native-display-validation.md) before treating
these automated message checks as evidence of physical-input or monitor parity.

`neo_test_display_provider_boundary` deliberately supplies invalid host responses
through the ordinary window callback table. Follow its poll/event failures into
state acceptance and snapshot replacement: rejected results preserve the previous
successful state. The [presentation audit](../reference/presentation-audit.md)
maps these tests and provider gaps to milestone10's original exit criteria.

For milestone11's first image renderer, read `neo/packages/point-renderer.neo`.
It inspects a compact 2D view, stages pixel addresses, then clears/writes an RGBA
buffer. `neo_test_point_renderer` links geometry, views and a headless window into
one graph and checks pixels, edits, view selection and save/reopen. No C drawing
primitive was added. See the [package contract](../../neo/packages/point-renderer.md).

`neo/packages/line-renderer.neo` adds source segment lookup through the view's
source connection, an inverse source-row map, coordinate staging and a bounded
integer line sampler. Its prepare handler builds an ordinary pixel mask; render
then writes the protected buffer. Universal pixels and an optional exact-rational
oracle cover the [line contract](../../neo/packages/line-renderer.md). It does not
silently omit edges whose endpoints were removed by a compacting view.

`neo/packages/segment-clipper.neo` bridges that gap for segments: intersect exact
rational parameter intervals on the full homogeneous source payload, then divide
retained weighted coordinates and emit derived topology/provenance. Its source
self-connection makes generated output directly readable by the existing line
package. Read [clipping contract](../../neo/packages/segment-clipper.md) for checked
arithmetic/publication limits and the remaining triangle/depth work.

`neo/packages/triangle-renderer.neo` maps source-indexed topology and depths into
projected rows, normalizes winding, samples pixel-centre edge functions and stages
owner/depth arrays before clearing the protected buffer. Its top-left edge rule
prevents shared-edge overlap. Read the [triangle contract](../../neo/packages/triangle-renderer.md)
for the explicit screen-depth convention, quantized tie rule and clipping limits.
# GUI and desktop direction

Read `neo/packages/canvas.md`, `package-browser.md` and `sketch.md` for the current
bitmap canvas, live graph inspector and foreground application protocol.
`desktop-model.md` describes the next image-owned foundation: app references and
window geometry/focus/lifecycle. It is tested independently of a display. The
desktop session now supplies the compositor and private app input/presentation;
see [desktop guide](../../neo/packages/desktop.md) for current behavior and limits.
# Desktop implementation

`neo/packages/desktop.neo` and the canonical image's desktop actor own the GUI
session, app windows, composition and routing. The generic C resource operations
are documented in reference/private-surfaces.md; they contain no desktop policy.
