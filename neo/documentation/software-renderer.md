# Software rendering images

[Language documentation](README.md)

From the VM repository root:

```sh
make
./build/x86_64/Linux/neo --gui neo/cube.neo cube
./build/x86_64/Linux/neo --gui neo/shaded-cube.neo cube
./build/x86_64/Linux/neo --gui neo/shaded-sphere.neo sphere
./build/x86_64/Linux/neo --gui neo/triangle.neo triangle
```

Each image owns its renderer. No `--template`, include, or external renderer source
is loaded. The kernel grants a 640×480 RGBA buffer and window connections. The
selected `frame` handler performs rendering and calls `window-present`.

The wireframe cube owns packed vertex/edge arrays, rotation/projection behavior,
and its line rasterizer. The shaded images own their mesh and filling behavior.
Arithmetic uses checked signed integers, including fixed-point conventions chosen
by each image. These conventions are not a universal dimensional library.

`call` invokes a handler on the same receiver, sharing authority and the execution
budget. A nested return ends only that handler. Scratch fields belong to the
receiver; these drawing handlers are sequential and not reentrant.

GUI launch polls input and executes frames with a one-million-step budget. Window
resizing changes the presentation area, not buffer storage or geometry. Native GPU
commands, a complete GUI, and general n-dimensional transforms are not provided.

Milestone11 now adds a reusable [projected vertex-cloud package](../packages/point-renderer.md)
over the common spatial view result and protected buffer/window interfaces. It
plots points from n-dimensional source projections; its rendering policy is
literal image behavior. The older demo rasterizers above remain independent.

The [line renderer package](../packages/line-renderer.md) now reads source segment
topology through the selected full-row view and clips deterministic integer line
samples to the viewport. Missing/compacted vertices are rejected until homogeneous
primitive clipping is implemented. No native line rasterizer was introduced.

The [segment clipper](../packages/segment-clipper.md) now implements homogeneous
near/far/side clipping before division and supplies generated topology to the line
package. Crossing edges survive even when both original endpoints are rejected.
Filled triangles/depth remain unfinished.

The [triangle package](../packages/triangle-renderer.md) now supplies filled
pixel-centre/top-left coverage, flat face colours and explicit screen-space depth.
It requires full projected rows; homogeneous triangle clipping is still pending.
Existing standalone demo rasterizers remain independent.
