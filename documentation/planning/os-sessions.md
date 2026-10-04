# OS work: bounded sessions

[Documentation index](../README.md) · [Architecture](../design/architecture.md)

## Session rule

Finish each session at a coherent checkpoint before exhausting the available
context: scoped work complete, appropriate checks run, documentation updated,
and a short handoff identifying what is done and the next task. Do not begin a
second substantial change after the checkpoint is ready. If a task proves larger
than expected, finish an independently useful slice and explicitly record remaining
work rather than claiming the whole task is complete.

Reserve time/context for validation and review; do not spend the entire session
implementing. Estimates are planning aids, not measured token or credit costs.
Exact usage must come from actual usage records when available, not guesses.
A session does not need to span an entire phase of the roadmap.

## Agreed boundary

- The live object graph contains the OS and applications. Start with one image
  file at `neo/neo-os/image.neo`; packages are contained objects, not separately
  running images. Directory export/import is later external tooling.
- C retains the evaluator, basic protected storage/reference operations,
  authority enforcement, execution bounds, and platform bindings.
- Object duplication stays in the kernel. Preserve authority checks, internal
  remapping, atomic publication, and cleanup on failure. `move` remains duplicate
  then delete; its orchestration layer remains to be specified.
- Existing scheduler policy, parser, and bootstrap utilities may migrate in
  explicit stages; keeping duplication native is now an accepted boundary.
- Universal dimensionality and its maths/spatial packages are the first OS
  libraries. Define their contracts before further graphical work. The GUI is
  a direct representation of graph objects, not a separately maintained UI world.
- Implement hosted Linux x86_64 first. Separate hosted/bare-metal execution from
  physical/virtualized hardware. Unknown virtualization status is valid; it must
  not choose an I/O backend or grant authority.
- Platform capabilities describe available mechanisms; actual resource grants
  determine what an object may access. Host filesystem access is optional, not
  the definition of image storage or ordinary object containment.
- GUI, GPU backends, other platforms, and self-hosting are deferred.

## Roadmap source and unresolved proposals

The user supplied `project-substrate.md` as a planning reference. Its phase and
cost estimates are provisional. Direct conversation decisions take precedence.
The user has since retained duplication in the kernel. This does not approve
the roadmap's exact `copy`/`move` API. The proposed primitive names, implicit
iteration bindings, invocation/failure contracts, and queue representation require
design before implementation.

The roadmap proposes consolidating the current runners into one `neo` executable
with subcommands. Plan that refactor separately from platform extraction; retain
existing behavior until the replacement has integration coverage. No new standalone
OS runner is needed. A command table and reusable CLI entry point are implementation
proposals, not new language semantics.

## Immediate sessions

| Session | Deliverable | Completion evidence |
| --- | --- | --- |
| 0 — boundary and scope | This guide and an explicit kernel/OS boundary | Documentation reviewed; next task bounded. |
| 1 — platform descriptor and contract | Portable platform interface; hosted Linux x86_64 adapter; explicit environment/capability reporting | Tests for descriptor values, unsupported/unconfigured cases, and no implicit grants; existing suite passes. |
| 2 — platform service extraction | Route existing host loading, streams, timing, and optional window creation through the contract | Equivalent CLI output, stream behavior, and failure handling; portable core does not depend on Linux/X11. |
| 3 — one CLI | One executable, one VM per process, common CLI/GUI mode arguments | Strict-warning compilation; old targets retired; CLI/GUI share the VM. Further testing deferred by user. |
| 4 — reference design | Specify authorized object-reference values, lifetime, inspection, mutation, and persistence implications | Concrete operation contracts and security cases; distinguish decisions from proposals. |
| 5+ — structural primitives | Implement that contract in small batches with negative authority/lifetime tests | Authorized graph access from neo; each batch builds and passes tests. |

After these foundations: design and implement universal dimensionality as the first
OS libraries/packages (maths, spaces, coordinates, geometry, transforms, and
projection are proposed package boundaries). Establish dimension/space checks and
non-graphical tests before GUI work. See the [spatial decision](../design/decisions/0002-universal-dimensionality.md).

Further work includes image-level execution policy, save/load and external-resource
restoration, package hierarchy with split/join round trips, and a terminal prototype
browser. Split each into implementation sessions after
its prerequisites are established. Do not treat a rendering demo or successful
ordinary-graph serialization as evidence of full OS persistence.

## Current checkpoint

Sessions 0–2 are complete. Session 3 now follows the user's corrected launch model:
`make` builds one `neo` binary, one process owns one VM, and shared arguments select
CLI, GUI, or both modes. The old standalone runners and build targets are retired.
The unified code compiled with strict warnings. At the user's request, no further
tests were run after this correction; earlier tests do not validate the new modes.

The GUI mode currently opens the existing pixel/window interface. It is not the
future OS GUI. Duplication stays in the kernel. Universal dimensionality remains
the first OS library work, before new graphics.

**Next brief:** Session 4, authorized reference and structural-access design.
Connect its contracts to the needs of the first maths/spatial packages, including
non-spatial objects, coordinates, packed vertices, and dimension/shape validation.
Specify proposals before implementing new graph access. Do not build GUI elements
or move duplication out of the kernel.
