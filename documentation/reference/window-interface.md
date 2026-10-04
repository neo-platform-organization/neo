# Window and pixel-buffer interface

[VM documentation](../README.md)

```sh
make
./build/x86_64/Linux/neo --gui neo/shaded-cube.neo cube
```

The host creates an ordinary protected window resource and a separate RGBA byte
buffer. The current launcher grants READ/WRITE connections named `window` and
`buffer` to the selected actor. Its `frame` handler computes pixels in neo;
`window-present` sends the buffer through the native adapter.

The fixed surface is 640×480, four bytes per pixel in red, green, blue, alpha order,
row-major. Presentation is opaque and 1:1; the backend clips when the window is
smaller. Window resizing does not resize or rescale buffer contents.

[display.h](../../neo-vm/display/display.h) defines portable C buffer/window
callbacks, resource ownership, polling, and normalized events.
[x11.h](../../neo-vm/platform/x11/x11.h) defines the Linux X11 opener. The platform
factory selects it beneath the host-independent resource contract.

Reads require READ, writes require WRITE. Opening requires INSERT on the parent.
Connection grants never infer authority from resource availability. Resource
contexts are released on deletion/unload/VM destruction. Live resources block
copy/move and serialization; reconnecting them after persistence is not implemented.
Native connection-loss recovery is also not implemented.

See [language primitives](syntax-cheat-sheet.md) for byte
access, buffer fill, presentation, event inspection, and width/height queries.
