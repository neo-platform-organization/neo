# Build the universal VM

[VM documentation](../README.md)

From the repository root:

```sh
make
make test
./build/x86_64/Linux/neo help
./build/x86_64/Linux/neo platform
./build/x86_64/Linux/neo --gui neo/shaded-cube.neo cube
```

Requirements for `x86_64/Linux`: C17 compiler, Make, pkg-config, and Xlib development
headers/libraries. `make WITH_X11=0` omits X11 and builds the same target-specific executable path.
GUI requests then return unsupported. CLI and GUI modes share one executable.

The default target is derived from `uname -m` and `uname -s`, normalized to the
architecture/OS directory names. The build compiles and links directly to
`build/$(TARGET)/neo` (`build/x86_64/Linux/neo` here). It leaves no object files,
static archives, alternate runners, or configuration stamps. `make clean` removes
build output, preserving any `.neo` files a user has placed there. The default uses
strict warnings and debug symbols. `make CFLAGS='-std=c17 -O2 -g'` builds optimized.
Compilation replaces the executable only after success.

## Target selection

```sh
make targets
make TARGET=x86_64/Linux
make x86_64/Linux
```

Target definitions live in `neo-vm/platform/ARCH/OS/platform.mk`. Only
`x86_64/Linux` and `x86_64/Windows` are implemented. Other target definitions fail with an explicit stub
message; they do not produce dummy binaries. `/neo` denotes future bare-metal
execution. `wasm32` and `wasm64` denote execution inside a WebAssembly runtime.

The executable has one VM per invocation. Runtime behavior and its limits are
specified in [the language reference](../../neo/documentation/runtime-format.md).

## Artifacts and regression checks

`make -s path` prints the selected executable path. `TARGET=ARCH/OS` overrides the
host default for explicit cross builds; it does not implement a missing backend.
Both the temporary linker output and final binary live under the selected target.
`clean` covers the complete build tree and preserves `.neo` files. `/build/` in
`.gitignore` already covers nested targets and needed no change.

No object files, static archive, or separate test executables are produced. They
were removed in the earlier restructuring and remain absent. `make test` now runs
`neo selftest` inside the universal binary, including the actual OS seed image.
Run it from the repository root. It needs no display server; fake output is used.

Verified: default and `WITH_X11=0` builds pass the regression checks. The same binary
also passed with AddressSanitizer/UndefinedBehaviorSanitizer (sandbox leak detection
disabled; tracked allocator checks balanced ownership). Restore the ordinary build
with `make` after changing instrumentation flags.

## Live terminal session

The image manager now performs dependency/version policy. Native duplicate
traversal/order checks have been removed; structural/grant/lifetime/publication
checks remain. Windows tests cover image pin/cycle failures, metadata-only
dependents, interrupted scratch recovery, editable version rules and grants changed
during preparation. The kernel still refuses unsafe publication. See
[kernel/image boundary](../reference/kernel-image-boundary.md).

Plain `neo IMAGE` now selects the image's declared entry or root `main` handler;
an object argument is optional. The local package manager supports inert staging,
installation, resolution, independent instances, export, guarded updates/removal
and terminal inspection. Strict Windows tests include package allocation-failure
sweeps and entry selection; separate processes verify package export/reimport and
checkpoint reopen. See [local packages](../reference/packages.md). Linux and
sanitizers were not rerun for this change.

The OS seed excludes sample workers, maths/shape demos and demo behavior.
It uses direct predicates and ordered `do` operations without `condition`/`stepN`
wrappers. `selftest` checks the clean boot catalogue and separately loads explicit
fixtures. Closed-region import checks cover rights, remapping, external-edge
refusal and failure at every allocation. The strict Windows build passes.

`neo live IMAGE` keeps one image loaded for inspection, editing, invocation, and
explicit scheduling. See the [live image guide](../reference/live-image.md) for commands,
a working counter experiment, authority boundaries, and save limitations. The
operator interface remains native C. `save` supports partial message checkpoints;
native registrations and external resources remain unsupported. See the
[verified foundation](../reference/runtime-foundation.md).

The milestone-1 audit added regression groups for message checkpoint states,
accepted queue order, restored read grants and frozen payloads, and the documented
live image-driver disable/repair workflow. These run inside `neo selftest` alongside
the 60 expression cases. The current Windows build passes; a fresh Linux build
was unavailable because WSL Debian has no compiler or Make installed. Earlier
Linux/sanitizer results above remain historical.

Milestone 2 extends the suite to 71 expression cases plus message loss/release/order
and dispatch-recovery groups. It tests all five states with sender/receiver/both
loss, separated RELEASE permissions, stale links, nested payload freeze, scoped
failure references, managed-request guards, explicit discard and host limits.
The strict Windows build passes with balanced tracked ownership. The orphaned
pending-message cleanup/save/reopen workflow also passes in separate processes.

## Windows x86-64

Milestone 5 adds environment service groups alongside the 78 expression cases:
explicit grant denial, failure inspection, shared native/graph clients, workspace
creation/editing, scheduler invocation, save policy and native recovery. The
strict Windows suite passes with balanced tracked ownership. A separate-process
service save/reopen and policy-denied replacement check also passes. See
[environment services](../reference/environment-services.md). Linux and live GUI
checks were not run for this milestone.

Milestone 4 retains 78 expression cases and adds versioned checkpoint rejection,
invalid message/queue metadata and decoder allocation-failure coverage. The strict
Windows suite passes. Separate processes verified paused-failure repair/retry,
retained accepted work and destination preservation after an invalid save. See
[checkpoint contract](../reference/checkpoints.md). Linux/sanitizers were not rerun.

Milestone 3 extends the suite to 78 expression cases and image scheduling tests:
shared submission cutoff, rotating order, membership snapshots, grant validation,
explicit image retry/discard, reentrancy, budget recovery and idle waiting. The
strict Windows build passes with balanced tracked allocations. See the
[image scheduler contract](../reference/image-scheduling.md). Linux and sanitizers
were not rerun for these changes.

Use a native MinGW-w64 GCC with the Universal C Runtime (MSYS2 UCRT64).
Add its `bin` directory (typically `C:\msys64\ucrt64\bin`) to PATH. From the
repository root in PowerShell:

```powershell
./build-windows.ps1 -Test
./build/x86_64/Windows/neo.exe platform
./build/x86_64/Windows/neo.exe --cli neo/neo-os/image.neo browser
./build/x86_64/Windows/neo.exe --gui neo/shaded-cube.neo cube
./build/x86_64/Windows/neo.exe live neo/neo-os/image.neo
```

The script needs no Make or POSIX shell. It uses C17, debug symbols, and the same
strict warnings as the Makefile. `-Optimize` selects `-O2`; `-Compiler PATH`
selects another compatible GCC. It replaces `build/x86_64/Windows/neo.exe` only
after compilation succeeds. `-Test` runs the same built-in regression suite as
`make test`, from the repository root.

Alternatively, from an MSYS2 UCRT64 shell with GNU Make installed:

```sh
make TARGET=x86_64/Windows CC=gcc
make TARGET=x86_64/Windows CC=gcc test
```

The Makefile disables X11 for Windows and links User32, GDI32, and Shell32.
The native Win32 provider supports files, standard streams, millisecond waits,
opaque RGBA pixel presentation, and normalized window input events. Source paths
and command-line arguments use UTF-8 internally and UTF-16 Windows file APIs.
Saving uses a unique sibling temporary file, flushes it, then replaces the
destination. Console input is polled and line-buffered with basic backspace;
redirected pipes are polled for available bytes. Output writes are synchronous,
as on Linux. Console output follows the console code page (`chcp 65001` selects
UTF-8). Full terminal editing and text composition are not implemented.

Validated on Windows: strict GCC build, 60 expression cases and all built-in
regression groups, OS browser demo, redirected live commands, pipe EOF,
save/reload with replacement, and a Unicode source filename. The PowerShell
build was exercised locally; the Make path requires GNU Make and an MSYS2 shell.
An automated hidden-window smoke test also passed repeated buffer presentation,
key-message dispatch, and clean closing. Visual appearance and interactive
console typing have not been manually checked.

The spatial foundation now includes `neo/packages/vector.neo`; built-in selftest
checks its computed-length construction, addition and fixed-point dot products
through dimension 100. `array-new` charges allocation length against the shared
execution budget. See [package usage](../../neo/packages/README.md).

Windows links Winsock (`ws2_32`) for the optional platform TCP provider. Built-in
selftest includes real ephemeral-port IPv4 loopback; local socket permission is
required. No public endpoint is used. See [networking](../reference/network-interface.md).

The image-network grant selftest exercises endpoint control from neo handlers,
ACT/INSERT denial, copy/serialization rejection, stream publication and listener
close independence. The CLI supports explicit `--connect`/`--listen` endpoint
grants; `neo/fixtures/network-client.neo` is a separate transport fixture.

Snapshot replication lives in `neo-vm/replication/`. Universal selftest checks
partial framing, scope closure, authority, frozen state, inert import, malformed
frames and allocation-failure retry. Windows two-process loopback was also
verified with `neo/fixtures/replica-send.neo` and `replica-receive.neo`.

The exposure-package selftest exercises two successive revisioned snapshots,
explicit acceptance, replay rejection, withdrawal/resume and editable revision
policy. Removing source COPY still fails natively. `neo/packages/exposure.neo`
adds no C primitive; package installation/instances/checkpoint were also checked.

Scheduled networking checks now run real TCP through the existing image scheduler,
drop/retry/reconnect, and verify full-state refresh without automatic acceptance,
including a pending candidate surviving another reconnect. Windows live operator
grants and a two-process scheduled resync were also checked. Session package
installation/copy/checkpoint is checked independently of the exposure package.

Windows now links Schannel/Windows certificate/CNG libraries (`secur32`, `crypt32`,
`ncrypt`) for pinned mutual TLS. The universal selftest creates and removes only
its temporary current-user certificates/private keys and runs local TLS tests.
Optional cross-process fragmentation/tamper/missing-certificate coverage is
`neo-vm/platform/win32/tests/tls_live.ps1`; see
[authenticated networking](../reference/authenticated-networking.md). Python is
needed only for that optional test, not the VM. Linux networking remains stubbed.


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
Committed text uses portable `display/text.c` validation/UTF-16 decoding and the
existing event snapshot interface. Win32 emits UTF-8 commits separately from keys;
headless validates injection. Universal tests cover malformed encodings, Unicode
boundaries, surrogate repair, image delivery, byte limits and stale text after
focus loss. See [text input](../reference/text-input.md). Scale/resource loss,
IME preedit and X11 text support remain pending. Native foreground input has not
been interactively tested; GUI policy stays image-owned.

The surface-state checkpoint adds positive rational layout scale, recoverable
presentation availability and monotonic generations through existing window-state
queries. A coalesced synthetic surface event survives input queue overflow. Win32
handles DPI changes and GDI presentation failures; headless injects scale/loss
transitions. Universal tests cover image delivery, coalescing, recovery, authority,
ratio validation and generation limits. Event-name result storage now reserves
the longest kind (`input-reset`) before consuming an event. See
[surface state](../reference/surface-state.md); native interaction/parity remains
unverified and milestone10 remains partial.

Windows universal selftest now includes hidden native-window message checks for
keys/text, focus epochs, capture/release/loss, signed pointer/button coordinates,
overflow, DPI notification and close/destruction. A real GDI off-screen bitmap
checks RGBA conversion and paint output. It uses the production provider and paint
path, with an internal hidden creation mode; the normal opener remains visible.
See [native display validation](../reference/native-display-validation.md) for
coverage and remaining physical-input/monitor/native-failure limitations.

The [presentation acceptance audit](../reference/presentation-audit.md) inventories
milestone10 mechanisms and preserves its incomplete cross-platform exit gate.
Universal provider-boundary tests verify rejected state/event responses and
callback errors preserve successful snapshots. The core now rejects input epochs
above INT64_MAX, matching the reset helper and image integer contract. X11 parity
and physical Windows interactions remain unverified; no Linux work was added.

Milestone11 starts with `neo/packages/point-renderer.neo` (neo.render.points0.1.0).
All drawing is image behavior over existing inspection/array/buffer primitives.
Universal tests install/instantiate the artifact and compare exact headless pixels
from 2D projections of dimensions2,3,4,33; edits, view switching, empty inputs,
overflow/denied writes and checkpointed source links are covered. Frame step counts
are reported for a four-point5×5 reference workload under100,000 steps. See
[point renderer](../../neo/packages/point-renderer.md). Lines/depth/text/dirty
updates and GUI remain open; milestone10's native/platform gaps remain tracked.

Milestone11 line checkpoint adds neo.render.lines0.1.0 as literal image behavior.
Universal selftest verifies32 exact headless frames and topology/shape/overflow
failures; maximum2,554 steps on the5×5 one-segment reference. Optional
`python neo/packages/tests/line_rasterization.py` verifies356 exact-rational masks,
package installation, source-row permutation and one-image save/reopen. Sampling
work is viewport-bounded, not offscreen-length-bounded. Compact views are rejected;
homogeneous clipping and filled/depth/text/dirty rendering remain open. See
[line contract](../../neo/packages/line-renderer.md).

Milestone11 adds neo.render.segment-clipper0.1.0: exact rational edge intervals,
near/far/side planes, generated projected vertices/segments and original-edge
provenance, all in image behavior. The unchanged line renderer consumes its
generated full-row view. Universal tests check24 exact frames through perspective,
clipper and line stages over source dimensions3,4,7,33 (max2,575 steps/clip,
perspective/raster excluded). Optional homogeneous_segments.py checks107 exact
rational cases, signed ties/raw-unit precision and one-image package save/reopen.
See [segment clipping](../../neo/packages/segment-clipper.md). Filled triangles,
depth, glyphs and dirty updates remain open; no C renderer primitive was added.

Milestone11 now includes neo.render.triangles0.1.0, entirely image-owned. Full-row
spatial views and source triangle topology feed top-left pixel-centre coverage and
explicit source-indexed screen-depth interpolation. Universal tests verify12
exact overlap frames across dimensions2,3,4,33, shared-edge ownership, colours,
depth ties/gradients and failure/authority retention. The5×5 two-face reference
measured14,538 maximum steps/frame, projection excluded, under100000. Optional
triangle_rasterization.py verifies84 rational coverage/depth scenes and single-image
package save/reopen. See [triangle contract](../../neo/packages/triangle-renderer.md).
Homogeneous triangle clipping, glyphs and dirty updates remain open.

## GUI and live editing checkpoint

Use neo.exe with image.neo; see [desktop](../../neo/packages/desktop.md)
for build/preparation/launch. Apps dropdown creates independent Browser, Sketch,
Text Editor and Task Manager instances. Top-bar buttons navigate; X deletes an
instance and its transient/native resources. Browser previews handlers, edits a
draft inline and pops the same document into a standalone editor. Apply validates
and replaces inactive code; Publish performs a versioned package-manager update
for future instances. KEEP retains a document only in the loaded session.

UI, history, lifecycle and publication remain image code. C additions are generic
UTF-8 text, source validation/atomic replacement, bounded attempt, modifier fields
and an optional clipboard stream. See [mechanism contracts](../reference/source-editing.md).
The final full Windows selftest passes, including headless checks for multiple
instances of every app class, editing, stale publication, source allocation failures
and runaway repair (maximum502285 steps/GUI turn). Physical/native clipboard acceptance has not been rerun for this change.

The host profile remains131072 objects,4MiB source/output, depth128, decoded token4096
bytes. The shared10-package/15-instance spatial/network image saves/reopens at73228
objects and3920368 bytes. Live native resources still prevent ordinary full-session
serialization. Font quality and the optimization/JIT/GPU roadmap remain deferred.

Interpreter lookup optimization adds exact `(parent,name)` and capability indexes
and expands identity/parent hash buckets. Rename/delete/package publication update
the indexes synchronously. Unknown capability addresses are checked for membership
before dereference; stale, cross-VM and attenuated grants retain their checks.
An unchanged optimized GUI test workload measured16.171s before and6.192s after
(about2.6×); step counts stayed identical. This is a workload measurement, not a
universal throughput guarantee. Later zoom/nD tests add work to that test group.

The reader also indexes staged names, labels and identities, including forward
edge resolution. These indexes are private to the staged load and freed on every
success/failure path; no partial objects are published. Final optimized Windows
full selftest passes with balanced ownership and exact131072/excess profile
boundaries. The shared10-package/15-instance image now saves/reopens at65593
objects and3453239 bytes, including the desktop seed.
