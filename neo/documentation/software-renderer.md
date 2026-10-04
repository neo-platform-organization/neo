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
