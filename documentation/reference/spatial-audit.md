# Milestone9 spatial acceptance and integration audit

[VM guides](../README.md) · [Spatial contract](../../neo/documentation/spatial-protocol.md)

Status: milestone9 complete for the documented reference subset.

Date:2026-10-22. Scope: the roadmap's non-graphical image-owned maths/spatial
foundation: vectors, matrices/transforms, explicit frame conversion, geometry
validation and selected projections, with dimensional/negative/round-trip tests.
Presentation/input, renderer clipping, GUI controls and developer tools remain
milestones10–16. The VM loads one image source; package artifacts import as graph
contents, not runtime source modules. The generic seed gains no demo content.

## Acceptance evidence

| Contract | Evidence and limits |
| --- | --- |
| Computed dimensions | Shared protocols exercise0,1,2,3,4,7 and dimensions through257; no separate2D/3D model or hard-coded dimensional ceiling. Counts, signed arithmetic, memory and invocation limits remain real. |
| Numeric policy | Signed64 fixed-point scale1,000,000; once-rounded dots, checked intermediates, explicit zero/overflow/precision failures. Decimal/rational references test approximation bounds. |
| Vectors and matrices | Arithmetic, normalization, compact identity, rectangular apply/composition and residual-checked inverse/affine operations pass. Singular forward maps are allowed; unsuitable inverse maps fail. |
| Frames and spaces | Explicit same-space/root identity and parent links, cycle rejection, point/displacement distinction and exact representable round trips. No authority from names or containment. |
| Geometry | Explicit vertex count including0D, packed shapes, point/pair/triple topology, bounds, exact checked degeneracy and source-preserving section provenance pass. |
| Views and edits | Axis views preserve hidden coordinates; general affine views require a caller-selected lift and final reprojection check. Shared-source updates are visible on refresh. |
| Perspective | Homogeneous coordinates, positive denominator guards, optional canonical vertex clipping and visibility/source maps pass. No implicit inverse edit or whole-primitive culling inference. |
| Failure behavior | Malformed shapes/kinds/indices, incompatible identity, cycles, precision, overflow and denied grants fail explicitly. Prepublication source/result retention is covered; publication remains sequential, not transactional. |
| One-image persistence | The combined catalogue and15 workspace instances save/reopen together with graph relationships, settings, source geometry and derived outputs; calls after reopening reproduce expected values. |

`neo/packages/tests/spatial_audit.py` runs12 independent reference suites:
vector Decimal precision; matrix arithmetic; rational inverse; affine transforms;
frame hierarchy; Gram-determinant geometry; axis editing; coordinate-band selection;
segment sections; triangle sections; constrained general views; perspective.
All12 pass in the audited Windows build. The universal C selftest additionally
covers authority/lifetime/allocation failures, exact host limits and the prior
runtime/networking foundations. The strict Windows build and full universal suite
pass, including exact65,536-object parse/format success and excess parse/format/
import rejection. High-dimensional spatial calls use bounded250,000
steps in relevant C tests; normal CLI invocation remains100,000 steps.

## Integration blocker found and corrected

The old16,384-object host profile allowed individual package workflows, but the
combined OS catalogue failed while installing the fifth spatial artifact. Four
previous records remained; isolated per-package success was insufficient evidence.

The shared trusted host object bound is now65,536 in resource_limits.h. Byte4MiB,
depth128, token4096 and independent replica wire1MiB/chunk4096 limits are unchanged.
No language-dimensional ceiling, image-editable quota or implicit extra authority
was introduced. Old VMs can reject larger images under their smaller local profile.
The profile still bounds codec/import/catalogue operations, not global allocation
or wall-clock time.

The native exact-boundary fixture now uses small grouped namespaces rather than
one maximum-width sibling list. It retains exact-node success and excess-object
parse/format/import rejection; duplicate-name semantics have separate regressions.
This avoids coupling the node-bound test to quadratic wide-namespace costs, which
are not a promised performance guarantee for arbitrary graphs.

## Measured single-image workflow

`python neo/packages/tests/spatial_integration.py` imports all eight spatial
artifacts (vector, matrix, frames, geometry, projection, section, linearview,
perspective), plus networking exposure/session, into the OS package catalogue.
It creates15 independent workspace instances and explicitly grants their links.
Network instances stay inert; this is graph coexistence, not a new live TLS audit.

The checked image contains50,968 graph objects and saves as2,623,370 bytes. It has
14,568 objects and1,570,934 bytes of profile headroom, not guaranteed room for
arbitrary future packages/updates/GUI data. Staging and replacement also need room.
The complete image exceeds the separate replica wire bound: networking exposes
selected authorized scopes, not the entire OS graph automatically.

The workflow edits one4D source through an axis view and then a general affine
view, revalidates changed degeneracy, refreshes multiple views, filters a slice,
generates segment/face contacts, projects perspective visibility and performs an
exact local/root/local frame round trip. The catalogue, explicit frame/geometry/
view links and expected results survive save, process exit and complete-image
reopen. No artifact reload occurs after reopening.

## Boundaries and handoff

The reference subset is Euclidean, same-space and fixed-point. Unit identifiers
are declarations, not an automatic conversion registry. Cross-space/unit bridges,
custom metrics, angle-based rotation constructors and generalized cells are not
implemented conveniences; callers can supply explicit affine matrices. Frame
conversion uses full root paths, without a least-common-ancestor optimization.
Quantization/residual checks do not prove conditioning or global path error.

Perspective is forward-only. Vertex visibility is not sufficient to discard
crossing edges/faces. Homogeneous primitive clipping, raster/depth/text/dirty views,
selection ambiguity, frame-aware GUI navigation and hidden-coordinate inspectors
belong to later rendering/interaction milestones. Frozen snapshots need explicit
refresh; automatic GUI invalidation has not yet been implemented.

Next is milestone10: inventory and normalize presentation/input mechanisms, define
the headless provider, and cover resize/focus/text/pointer/resource-loss behavior.
Windows is the actively verified target. Linux networking remains intentionally
stubbed; Linux presentation parity requires its own later verification and is not
claimed by this Windows spatial audit.
# Desktop integration update

The desktop/nD viewport work subsequently raised the shared native object profile
to131072. The combined desktop and10-package/15-instance spatial/network image
crossed the earlier65536 limit; byte/depth/token and independent wire limits remain
unchanged. Historical milestone9 measurements below describe the earlier profile.
