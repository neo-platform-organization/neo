# Project Substrate: the first usable image

[VM documentation](../README.md) · [Graph operations](graph-operations.md)

The OS seed lives in one file: [neo/neo-os/image.neo](../../neo/neo-os/image.neo).
It contains package containers, owned prototypes, a workspace, and browser behavior.
Its catalogue is read from the live graph, not hard-coded into the host.

From the VM repository root on x86_64/Linux:

```sh
make
make test
./build/x86_64/Linux/neo --cli neo/neo-os/image.neo browser
./build/x86_64/Linux/neo --cli neo/neo-os/image.neo browser demo
```

Use `make -s path` to obtain the selected target's executable path on other hosts.
Other platform definitions remain unsupported stubs.

The first command lists packages/prototypes and the workspace count. `demo` then
copies two prototypes through the kernel, changes the copies, invokes their owned
behavior, and prints:

```text
4D dot product: 70
4D point shape valid: true
Original point dimension: 3
Independent workspace copies: 2
```

## Image contents

| Object | Responsibility |
| --- | --- |
| `packages/maths/vector` | Checked integer dot product over equal-length arrays, including zero-length vectors. |
| `packages/spatial/point` | Initial coordinate-count validation: nonnegative dimension equals array length. |
| `workspace` | Contains independent prototype copies made by the demo. |
| `browser` | Enumerates packages/prototypes, prints through granted stdout, and performs the demo. |

The vector's input fields are `left-values` and `right-values`; `dot` returns the
sum of pairwise products. Unequal lengths raise a failure; integer overflow reports
an overflow. No C maths/rendering library performs this computation. The point's
`valid` handler checks shape only; it does not define units, axes, metrics, spaces,
non-spatial semantics, or transforms. Those contracts remain future dimensional work.

The browser holds READ access to the package catalogue, READ/COPY grants to two
prototypes, and full authority over its workspace. It explicitly handles partial
stdout writes. Would-block raises a failure rather than pretending output completed.
Its scratch fields make it sequential, not reentrant.

Each launch starts a fresh VM/image. Workspace mutations disappear on exit; this
milestone does not save a running world. The browser is a listing/demo interface,
not yet an interactive shell or editor. Package names are contained-object
conventions, with no separate template files or filesystem include mechanism.

## Verification and remaining work

`make test` invokes the same executable's `selftest` subcommand. It checks graph
rights, stale handles, activation protection, budget sharing, independent copies,
ordinary graph persistence, and this image's output through a deliberately partial
stream backend. Dot products exercise dimensions 0, 2, 3, 4, and 7, mismatched shapes,
and overflow. A tracked allocator also checks cleanup.

Next steps are failure/message-lifecycle primitives and image-level scheduling,
then fuller dimensional protocols and package export/import. Running-image
persistence, an interactive prototype editor, dimensional GUI, other platform ports,
and GPU backends remain unfinished. Duplication continues to live in the kernel.
