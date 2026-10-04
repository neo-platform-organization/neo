# Software renderer and rotating cube

[Documentation index](../README.md) · [Window interface](window-interface.md)

The first renderer is a sequential software renderer written in neo. It supports clipped pixels and endpoint-inclusive integer lines. The demo is a rotating **wireframe** cube: all twelve edges are visible, including hidden edges. There is no GPU backend, lighting, depth buffer, filled-face rasterization, or automatic renderer selection yet.

## Run

From the repository root:

```sh
make window
./build/neo-window neo/cube.neo cube neo/software-renderer.neo
```

The [cube image](../../neo/cube.neo) contains geometry, rotation, projection, and frame behavior. The [renderer template](../../neo/software-renderer.neo) contains pixel and line algorithms. The existing X11 adapter presents the completed RGBA buffer; no renderer or maths library was added to C.

The launcher accepts `IMAGE ACTOR [TEMPLATE...]`. Before creating external resources or invoking any behavior, it loads each template as a separate inert image and copies its fields and handlers into the selected actor. It then unloads the source template. Each actor owns its own copy; this is not delegation to shared mutable code. Templates must have no connections or live resources. Names must not conflict with existing fields/handlers.

This is explicit **host bootstrap composition**, not a settled language import system. `neo_actor_install` is a host API for this purpose. If installation fails after copying some objects, the host must discard the bootstrap image; the launcher does so. Ordinary cross-image `copy` remains forbidden. The installer reconstructs inert data using host authority; it does not transfer capabilities across images.

## Renderer contract

Grant a connection named `buffer` with READ and WRITE. Call `r-init` before drawing. The current renderer is selected simply by copying this template; there is no backend-switching policy to configure.

| Handler | Inputs in the receiving object | Effect |
| --- | --- | --- |
| `r-init` | `buffer` connection | Read dimensions into `r-width`, `r-height`. |
| `r-clear` | `buffer` connection | Zero the buffer bytes, including alpha. |
| `r-point` | `r-x`, `r-y`, `r-red`, `r-green`, `r-blue` | Write an opaque pixel if coordinates are within the buffer. Out-of-bounds coordinates are skipped. |
| `r-line` | `r-x0`, `r-y0`, `r-x1`, `r-y1`, RGB fields | Rasterize a line in any direction, including both endpoints. A zero-length line writes one point. |

Coordinates are integers and RGB channels must be 0–255. The `r-*` fields also hold scratch state, so these handlers are sequential and not reentrant. Out-of-bounds points are rejected before computing their buffer address. Lines are clipped per sample, not geometrically shortened before iteration; extremely long lines may exhaust the execution budget even if mostly offscreen. Arithmetic remains checked signed 64-bit arithmetic.

The line algorithm is DDA: take as many steps as the larger absolute coordinate difference, interpolate integer positions, then call `r-point`. This is a small reference implementation, not an optimized or antialiased rasterizer. The tests cover all eight directions, degenerate lines, endpoint inclusion, pixel counts, and clipping.

## Rotation and projection

The cube's eight vertices have coordinates ±80. Each frame rotates them around two axes, moves them 400 units in front of the camera, and projects them with a focal length of 300 pixels. The buffer center is the viewport center. Cube dimensions and camera distance keep every vertex ahead of the camera; this demo does not implement general near-plane clipping.

Because neo currently has integer arithmetic, the demo stores sine/cosine values at a scale of 10000. `c-sine` uses a rational approximation over a half-turn and restores the sign for the other half. The approximation is described in [The Bhaskara–Aryabhata Approximation to the Sine Function](https://www.tandfonline.com/doi/abs/10.4169/math.mag.84.2.098). The formula used in the image is:

```text
p = angle * (180 - angle)
scaled sine ≈ 40000 * p / (40500 - p)
```

Angles wrap in degrees. Every frame recomputes rotations from the original vertices, avoiding cumulative matrix drift; integer rounding and approximation error remain. Yaw advances three degrees and pitch two degrees per frame. Speed depends on execution/presentation rate, not wall-clock time. The launcher pauses 16 ms after each completed frame but does not promise 60 FPS.

## Runtime mechanisms added for reuse

| Primitive | Example | Meaning |
| --- | --- | --- |
| `call` | `(call (selector "r-line"))` | Invoke another handler on the same receiver. Preserve authority and current message. Share the original instruction budget and recursion-depth limit. A nested `return` ends only the called invocation. |
| `buffer-fill` | `(buffer-fill (target "buffer") (value 0))` | Fill all buffer bytes with one byte value, requiring WRITE; return unit. This is a bounded memory operation, not a shape or color renderer. |

Local call arguments/results currently use receiver fields and scalar returns. This does not add cross-object synchronous invocation or function-local bindings. Writes cannot replace the receiver's active handlers subtree. One buffer fill consumes one primitive operation plus operand evaluation; instruction budgets are not byte-work or wall-clock budgets.

Repeated connection lookups now reuse an existing immutable capability with the exact same target and permissions. This avoids allocating another VM-lifetime handle for every channel written, while retaining stale-reference and authority checks.

## Validation and limits

`make test` loads the actual cube and renderer files, copies the template, removes its source image, and executes the copied behavior against a headless presentation callback. It tests renderer invariants, cardinal sine values, angle changes, known perspective coordinates, multiple orientations, and clearing old frames. Selected frames are written to `build/cube-0.ppm` through `build/cube-3.ppm` for inspection.

Tested poses stayed below 201,000 steps per frame, within the launcher's one-million-step budget. Local-call tests separately cover nested returns, missing handlers, invalid selectors, recursion limits, and shared-budget enforcement. Renderer, evaluator, and display sanitizer tests pass with leak detection disabled in the sandbox.

This establishes the software path for a future common drawing interface. Extending it to filled triangles, depth testing, text, and GUI composition remains image-level work; a GPU implementation is intentionally deferred.

## Compact geometry and faster execution

The cube owns a packed `vertices` array (8 × 3), `edges` array (12 × 2),
`projected` array (8 × 2), and three RGB colors. Matrix rows are a neo convention
on contiguous integer storage. Two loops replace the previous per-vertex and
per-edge instruction blocks. See [array primitives](runtime-format.md#packed-integer-arrays).

Use `make release-window`, then
`./build/release/neo-window neo/cube.neo cube neo/software-renderer.neo`
for the optimized C build. The default debug build also benefits from indexed
graph lookup. The 16 ms host pause and per-frame rotation increments remain;
this demo still does not use elapsed-time animation.
