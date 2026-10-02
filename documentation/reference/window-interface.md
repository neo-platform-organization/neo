# Window and pixel-buffer interface

[Documentation index](../README.md) · [Syntax cheat sheet](syntax-cheat-sheet.md)

The host supplies a window and a packed pixel buffer. neo writes the pixels and decides when to present them. The current backend uses X11 directly, with no OpenGL dependency. It has no renderer, scene traversal, shapes, projection, transforms, or maths library. Those belong in neo.

```text
neo behavior -> buffer object -> window-present -> host backend -> X11 window
```

## Build and run

Run from the repository root:

```sh
make window
./build/neo-window neo/window.neo display
```

The optional target needs Xlib development headers/libraries and pkg-config (`libx11-dev` and `pkg-config` on Debian). Running needs an accessible X server through DISPLAY; XWayland also provides this interface. The normal `make` and `make test` remain headless and do not link Xlib.

For a renderer written in neo, run `./build/neo-window neo/triangle.neo triangle`. It fills a cyan triangle on the fixed 640 × 480 buffer, eight scanlines per frame, then keeps presenting the completed pixels. Its vertices are (320,80), (160,400), and (480,400). Resizing the window clips or exposes margins; it does not rescale the triangle.

The supplied `window.neo` image presents a black buffer. It deliberately contains no renderer. The launcher loads the image, creates a 640 × 480 buffer and window, grants the selected actor connections named `buffer` and `window`, and repeatedly invokes its `frame` handler. Closing the window ends the launcher; a failed invocation reports the error and ends it. Its 16 ms pause is example-host policy, not a VM scheduling rule. It invokes frame directly, without starting the message scheduler.

## Buffer contract

| Property | Current contract |
| --- | --- |
| Layout | Four bytes per pixel, in R, G, B, A order; eight bits per channel. |
| Coordinates | Top-left origin; rows run left to right, then top to bottom. |
| Byte offset | `4 * (y * width + x) + channel`, with channels 0–3. This calculation belongs in neo. |
| Initial contents | All bytes zero. |
| Ownership | VM-owned private byte storage attached to a capability-protected object. Image code never receives a C pointer. |
| Dimensions | Chosen by the host at creation; independent of window size. No automatic reallocation on resize. |
| Limit | 64 MiB per buffer; nonzero width/height; checked size and offset arithmetic. |
| Submission | Synchronous copy; backend must not retain the source pointer after present returns. |
| X11 presentation | One pixel to one pixel at the top-left. Excess is clipped; uncovered window area is black. No scaling or blending. |
| Alpha | Stored/read unchanged, but ignored by the opaque X11 window. |

Buffers and windows are runtime resource roles attached to ordinary objects, not new scalar payload kinds. Writing an ordinary object's payload does not create a resource. Generic scalar reads do not expose the native storage. The host explicitly creates resources and grants connections.

## neo primitives

All `target` and `buffer` operands below are text names of the receiver's granted connections, not global object names or host handles.

| Primitive | Example | Result / required authority |
| --- | --- | --- |
| `buffer-write` | `(buffer-write (target "buffer") (index 0) (value 255))` | Write one byte and return it; WRITE on buffer. Index must be a nonnegative integer; value must be an integer from 0 through 255. |
| `buffer-read` | `(buffer-read (target "buffer") (index 0))` | Read one byte as an integer; READ on buffer. |
| `buffer-size` | `(buffer-size (target "buffer"))` | Total byte count; READ on buffer. |
| `window-present` | `(window-present (target "window") (buffer "buffer"))` | Submit the buffer; return unit. WRITE on window and READ on buffer. |
| `window-poll` | `(window-poll (target "window"))` | Nonblocking event processing; return whether close was requested. WRITE on window. |
| `window-width` | `(window-width (target "window"))` | Cached window width in pixels; READ on window. |
| `window-height` | `(window-height (target "window"))` | Cached window height in pixels; READ on window. |

Poll before reading dimensions; the launcher already polls before invoking frame. A generic host backend initially has zero cached dimensions until its first successful poll. The X11 constructor performs that first poll.

These low-level operations provide byte access and presentation only. For example, writing the first pixel's red channel is:

```text
(buffer-write (target "buffer") (index 0) (value 255))
```

No drawing algorithm is implied. Byte-at-a-time interpreted writes are a correctness baseline, not a fast rendering API. Bulk buffer operations, resizing, and additional event objects can be added when their language-level contracts are defined.

## C embedding and backend boundary

See [neo_display.h](../../neo-vm/include/neo_display.h) and [neo_window_x11.h](../../neo-vm/include/neo_window_x11.h). The [launcher](../../neo-vm/examples/window.c) demonstrates host setup.

| C entry point | Purpose |
| --- | --- |
| `neo_buffer_create` | Create a zeroed RGBA buffer under a host-authorized parent. |
| `neo_buffer_read`, `neo_buffer_write` | Bounds-checked byte ranges; enforce READ or WRITE. |
| `neo_buffer_size` | Query byte length. |
| `neo_window_create` | Attach a backend callback table and context to a window object. |
| `neo_window_poll` | Poll and cache width, height, close status, and redraw status. |
| `neo_window_get_state` | Read the cached state. |
| `neo_window_present` | Validate both capabilities, then pass completed pixels to the backend. |
| `neo_window_x11_create` | Open an X11 window and attach the Xlib backend. |

The generic backend has three callbacks: `present`, `poll`, and `destroy`. The callback table is copied. Context ownership transfers only when `neo_window_create` succeeds. Callbacks are synchronous, run on the creating thread, and must not reenter the VM. An alternative backend can implement the same contract without changing neo behavior.

Deleting the resource, unloading its image, or destroying its VM releases the storage/window exactly once. Stale capabilities report unavailable. Copying, moving, duplicating, and serializing a region containing either resource currently report unsupported: silently discarding external state would be incorrect. Text images can still be saved before host resources are attached.

## X11 scope and limitations

The adapter uses Xlib's [image-transfer API](https://www.x.org/releases/X11R7.6/doc/libX11/specs/libX11/libX11.html) to convert RGBA bytes to the display's TrueColor channel layout and transfer them. It uses the [window-manager protocol](https://xorg.freedesktop.org/archive/X11R6.8.1/doc/XSetWMProtocols.3.html) for close requests.

The first version supports TrueColor displays, close/resize/expose events, and ordinary opaque windows. It does not expose keyboard, mouse, clipboard, compositing, vsync, GPU commands, or a general event queue. X11 dimensions are limited to 32767 per axis. Pixel conversion uses the native image masks and XPutPixel rather than assuming a particular byte order. Re-present after expose or resize; the launcher does this every frame.

Presentation flushes requests; success means submission, not a guarantee that a compositor has displayed the frame. Xlib retains its default fatal handling for a broken X connection and certain asynchronous protocol errors. This initial standalone backend does not promise recovery from display-server loss or concurrent calls. Window resource creation is a trusted host operation, not ambient language authority.

## Validation

```sh
make test                  # Headless fake-backend and runtime tests
make window                # Strict-warning X11 build
make test-window           # Opens and closes a temporary window on DISPLAY
```

Headless tests cover buffer bounds, initialization, capability denial, stale references, primitive calls, nested return, backend failure propagation, unsupported persistence/copy/move, and allocator-failure cleanup. The X11 test checks red/green pixel readback, resize reporting, close handling, and cleanup on a real display. Sanitizer targets include the headless display tests.
