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
On Windows x86_64, `platform/win32/window.c` implements the same contract using
User32 events and GDI presentation. It converts RGBA to a top-down BGRX bitmap;
rendering algorithms and buffer ownership remain in neo and the portable VM.

Reads require READ, writes require WRITE. Opening requires INSERT on the parent.
Connection grants never infer authority from resource availability. Resource
contexts are released on deletion/unload/VM destruction. Live resources block
copy/move and serialization; reconnecting them after persistence is not implemented.
Native connection-loss recovery is also not implemented.

See [language primitives](syntax-cheat-sheet.md) for byte
access, buffer fill, presentation, event inspection, and width/height queries.

A [portable headless provider](headless-display.md) now implements these same window
callbacks for trusted host/tests, with injected existing normalized events and
independent presented-pixel captures. It creates no OS display. Initial drawable
size is host-selected; resize remains independent of the submitted RGBA buffer.
There is no new launcher mode yet. Committed text, capture/reset and the bounded
[surface-state contract](surface-state.md) are implemented. Native adapter parity
and interactive validation remain milestone10 work.

[Focus/capture/input reset](input-reset.md) now defines explicit window-capture,
cached focused/captured/epoch fields and synthetic reset before stale queued input.
Win32 and headless implement capture; X11 currently reports unsupported. Native
adapter parity remains incomplete; no automatic image control policy is added.

Image services can now allocate [private endpoints and buffers](private-surfaces.md),
route event snapshots with controller authority and compose raw clipped buffer
regions. Desktop behavior is supplied by the image-owned
[session](../../neo/packages/desktop.md), not by this C resource interface.

window-event exposes boolean control/shift modifiers for command-key handling.
The [source editing contract](source-editing.md) describes their editor use.
