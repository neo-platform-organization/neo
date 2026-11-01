# Universal neo VM documentation

These Markdown guides describe the implemented VM, C interfaces, compilation,
and usage. Language/image documentation is isolated in
[neo/documentation](../neo/documentation/README.md).

| Guide | Subject |
| --- | --- |
| [Source editing](reference/source-editing.md) | Generic UTF-8 text, inert source validation, atomic replacement and callback bounds. |
| [Text Editor](../neo/packages/text-editor.md) | Shared documents, embedded/pop-out editing, undo/redo and session retention. |
| [Image desktop](../neo/packages/desktop.md) | Silk-inspired managed desktop, browser shell app, controls and launch. |
| [Task Manager app](../neo/packages/task-manager.md) | Ordinary windowed app over read-only window metadata and explicit navigation/close requests. |
| [Private display mechanisms](reference/private-surfaces.md) | Protected off-screen endpoints, event routing and raw rectangular buffer copy. |
| [Syntax cheat sheet](reference/syntax-cheat-sheet.md) | Current syntax, primitives, and launch examples. |
| [Build](development/build.md) | Compile the single executable and select a target. |
| [Kernel/image boundary](reference/kernel-image-boundary.md) | Native enforcement/execution versus image services and policy; current audit limits. |
| [Code tour](learning/code-tour.md) | Follow the subsystem implementation. |
| [Graph operations](reference/graph-operations.md) | Protected references, inspection, mutation, and invocation. |
| [Live image](reference/live-image.md) | Inspect, edit, invoke, and save one persistent terminal image. |
| [Verified foundation](reference/runtime-foundation.md) | Current message, scheduling, checkpoint and environment status with validation limits. |
| [Messages and failures](reference/message-lifecycle.md) | Lifecycle decisions, participant loss, protected release and scoped failure inspection. |
| [Image scheduling](reference/image-scheduling.md) | Image membership, turn snapshots, ordering, diagnostics, recovery and idle waiting. |
| [Checkpoints](reference/checkpoints.md) | Versioned graph/message persistence, explicit recovery, publication and resource boundaries. |
| [Environment services](reference/environment-services.md) | Image-owned workspace/execution requests, explicit grants, save policy and shared frontend contracts. |
| [Packages](reference/packages.md) | Local installation, dependency/grant validation, browsing, instances, export and guarded update/removal. |
| [Spatial acceptance audit](reference/spatial-audit.md) | Milestone9 contracts, reference checks, shared-image integration and capacity limits. |
| [Perspective views](../neo/packages/perspective.md) | Homogeneous projection, denominator safety and explicit vertex clipping rules. |
| [General affine views](../neo/packages/linearview.md) | Dense affine projections and caller-selected lift constraints for editing. |
| [Triangle sections](../neo/packages/triangle-sections.md) | Generated contacts, coplanar faces, degenerate hulls and rational provenance. |
| [Segment sections](../neo/packages/section.md) | Generated segment/plane intersections, coplanar topology and rational provenance. |
| [Coordinate-band slices](../neo/packages/slices.md) | Explicit vertex selection, visibility masks, source-row mappings and membership edits. |
| [Axis projections](../neo/packages/projection.md) | Derived views, shared source identity and edits preserving hidden coordinates. |
| [Geometry](../neo/packages/geometry.md) | Packed nD vertices, explicit topology, degeneracy and shape/index validation. |
| [Spaces and frames](../neo/packages/frames.md) | Explicit space/root identity, parent paths and checked coordinate conversion. |
| [Affine transforms](../neo/packages/affine.md) | Point/displacement semantics, translation, composition and inverse residual checks. |
| [Linear matrices](../neo/packages/matrix.md) | Compact identity, rectangular application/composition, numeric and budget contracts. |
| [Vector maths](../neo/packages/vector.md) | Dimension-driven arithmetic, magnitude/normalization, precision, overflow and checkpoint behavior. |
| [Planned spatial protocol](../neo/documentation/spatial-protocol.md) | Numeric/frame/geometry/projection contract with initial vector/matrix subsets implemented; frames and geometry pending. |
| [Project Substrate](reference/project-substrate.md) | Use the self-contained OS browser and prototype demo. |
| [Platform](reference/platform-interface.md) | Environment metadata and host-service callbacks. |
| [Authenticated networking](reference/authenticated-networking.md) | Explicit pinned mutual TLS grants and verified immediate-peer identity. |
| [Network restore and capacity](reference/network-restore.md) | Shared host bounds, full OS networking packages and explicit checkpoint/regrant workflow. |
| [Network liveness](reference/network-liveness.md) | Opt-in image heartbeats, idle work budgets, candidate-safe reconnect staging and acknowledgement boundaries. |
| [Scheduled network sessions](../neo/packages/session.md) | Image scheduling, reconnect/backoff and explicit full-state recovery. |
| [Exposure services](../neo/packages/exposure.md) | Image-owned publication identity/revisions, explicit acceptance and withdrawal. |
| [Snapshot replication](reference/replication-interface.md) | Explicit selected-scope snapshots, bounded transport and inert receipt/import. |
| [Networking](reference/network-interface.md) | Optional platform TCP transport and the image replication boundary. |
| [Streams and input](reference/io-interface.md) | Protected byte streams and normalized events. |
| [Focus and input reset](reference/input-reset.md) | Explicit capture, reset epochs and stale-event suppression. |
| [Committed text](reference/text-input.md) | Validated UTF-8 commits, Windows UTF-16 decoding and key/text separation. |
| [Surface state](reference/surface-state.md) | Rational display scale, presentation availability and coalesced change events. |
| [Native display validation](reference/native-display-validation.md) | Hidden Win32 message, capture, GDI pixel and lifetime checks with explicit interaction limits. |
| [Presentation acceptance audit](reference/presentation-audit.md) | Milestone10 evidence, provider failure checks, original exit gate and remaining gaps. |
| [Projected vertex renderer](../neo/packages/point-renderer.md) | Milestone11 image point plotting, spatial views and exact pixel checks. |
| [Viewport line renderer](../neo/packages/line-renderer.md) | Source segment topology, row mapping, bounded clipping and rational pixel checks. |
| [Homogeneous segment clipper](../neo/packages/segment-clipper.md) | Near/far/side-plane clipping before division, generated edges and rational provenance. |
| [Filled triangles and depth](../neo/packages/triangle-renderer.md) | Top-left coverage, explicit screen-depth interpolation, occlusion and exact pixel checks. |
| [Minimum GUI environment](reference/gui-environment.md) | Single-image package browser, read-only inspector and foreground apps. |
| [UI canvas](../neo/packages/canvas.md) | Verified bitmap text and clipped rectangles in image behavior. |
| [Desktop model](../neo/packages/desktop-model.md) | Canvas/window ownership, focus, placement and hit-test foundation for a separate desktop. |
| [Headless presentation](reference/headless-display.md) | Deterministic pixel/event provider, overflow semantics, failure injection and ownership. |
| [Windows](reference/window-interface.md) | Buffers, presentation, and resource lifetime. |
| [C conventions](development/c-conventions.md) | C API names, formatting, ownership, and errors. |
| [Writing docs](development/writing-docs.md) | Markdown and source-comment documentation. |

Plans, TODOs, design decisions, and collaboration memory live in the external
`brain/` directory. They are not part of this usage manual.
