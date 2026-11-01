# OS seed image

[VM documentation](../README.md) · [Environment services](environment-services.md)

The boot image is [neo/neo-os/image.neo](../../neo/neo-os/image.neo). It contains
only generic environment services, execution policy, a catalogue browser and an
empty workspace. Application objects, sample vectors/points and test workers are
not part of the boot image. Rendering demonstrations are separate images.

From the repository root:

```sh
./build/x86_64/Linux/neo --cli neo/neo-os/image.neo browser
./build/x86_64/Linux/neo live neo/neo-os/image.neo
```

On Windows use `./build-windows.ps1 -Test` and
`./build/x86_64/Windows/neo.exe` with the same arguments.

| Object | Responsibility |
| --- | --- |
| `packages/system/environment` | Explicitly granted inspection, editing, execution, failure inspection and save policy. |
| `packages/system/scheduler` | Membership, rotating/snapshot turns, dispatch and explicit recovery. |
| `workspace` | Initially empty; user-created live objects belong here. |
| `browser` | Lists the system catalogue and workspace count through granted stdout. |

The browser holds READ access to the catalogue and workspace and handles partial
output writes. It creates no demo objects and has no `demo` handler. Its scratch
fields make it sequential. One-shot browsing starts a fresh VM; `live` retains
edits until exit and supports explicit saves.

The seed uses direct predicates and ordered `do` operations, without `condition`
or `stepN` containers. Handler `body`, control-flow branch/body roles and primitive
operand roles remain where required by the evaluator. Bootstrap prototypes stay in
`packages/system`; the root `package-manager` owns a separate initially empty
managed catalogue and inert staging area. See [local packages](packages.md).

## Explicit test fixtures

`neo/fixtures/counter.neo` is an isolated test worker. Scheduler tests import it
with protected closed-region transfer: internal edges remap and external edges
are rejected. The boot image does not load it.

`neo/fixtures/substrate-tests.neo` retains the maths/spatial and copy demonstration.
`selftest` runs its dimensional, shape, overflow and ownership checks independently
from the actual boot seed. To run that fixture explicitly:

```sh
./build/x86_64/Linux/neo --cli neo/fixtures/substrate-tests.neo browser demo
```

Windows strict build and 78 expression cases plus built-in groups pass. A boot
check verifies the clean catalogue, empty workspace and partial-write browser
output. Import checks cover authority, internal cycles, external-edge refusal and
allocation failure cleanup. Linux and GUI checks were not rerun for this change.
