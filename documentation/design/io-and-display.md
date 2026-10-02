# I/O, displays, and rendering

[Documentation index](../README.md) · [Architecture](architecture.md) · [Current window interface](../reference/window-interface.md)

## Status

The user has established that I/O and display access need backend-independent object interfaces before building the GUI and higher-dimensional scene objects. Rendering and maths belong in neo. The live graph is intended to include scheduling and system policy; the kernel supplies execution, protection, and external bindings.

The separation of platform-independent image capabilities from native kernel mechanisms is agreed. Detailed request/completion protocols below remain proposals except where the implementation reference says otherwise. The current implementation has host-granted RGBA buffers, a window callback table, and an X11 adapter. It now also has object-facing byte streams, a POSIX terminal/pipe adapter, and a bounded normalized X11 event queue. GPU rendering, automatic fallback, full text input, and graph-resident I/O scheduling remain unimplemented. See the [implemented I/O subset](../reference/io-interface.md).

## Separate responsibilities

| Object role in the graph | Responsibility | Native mechanism behind it |
| --- | --- | --- |
| Byte stream | Ordered reads/writes and completion/error information | OS stream or device driver |
| Terminal | Text encoding, terminal capabilities, input interpretation, modes | Granted input/output streams and terminal control operations |
| Input source | Produce ordered input/event objects | OS keyboard, pointer, window, or device events |
| Surface | Dimensions, pixel format, lifetime, and presentation requests | Window, framebuffer, image sink, or remote display adapter |
| Renderer | Convert scene/drawing descriptions into surface contents | Software behavior in neo; optional granted GPU operations |
| View/projection | Map a scene's spatial model to a renderable view | neo maths and behavior |
| GUI | Layout, widgets, interaction, focus, and application behavior | neo objects using renderer and input interfaces |

A window is one kind of presentation destination, not the definition of a renderer. A terminal is an interactive interpretation of streams, not the definition of all I/O. Higher-dimensional objects keep their own geometry; a view chooses how to project them onto a display surface.

## Kernel boundary

A hosted neo VM should use the underlying OS's supported interfaces. An eventual bare-metal backend may use device registers through its drivers. These are alternative implementations of external capabilities, not raw addresses available to arbitrary objects. Integer fields cannot manufacture access to hardware.

The host supplies initial capabilities. Objects request operations and receive results through explicit authority. Native handles remain private; the graph holds logical endpoint identity, policy, and observable request state. A primitive may initially execute in C without making the device cease to be an object-facing resource.

## Stream and terminal proposal

Provide separate input, output, and diagnostic endpoints. Preserve terminal use, redirected files, and pipes through the same byte-stream contract. Each request needs an identity and a result that distinguishes transferred byte count, end-of-input, pending/would-block, cancellation, and failure. Short writes must be explicit; accepting some bytes is not completion of the entire write.

Use bounded queues and buffers. A slow endpoint should put its requesting object into a waiting state rather than stall the whole world. Native readiness wakes the appropriate graph activity; graph scheduling policy decides what runs next. Until resumable invocations and graph scheduling exist, synchronous/nonblocking host calls are transitional mechanisms, not the completed object-message protocol.

A terminal layer can implement text and escape-sequence interpretation in neo. Native terminal-mode changes need scoped ownership and restoration on release. Never silently assume redirected output supports cursor movement, color, or interactive input.

## Presentation contract proposal

Separate surface properties from window properties: logical window size, drawable pixel size, buffer dimensions, supported formats, and whether a surface can accept CPU pixels or GPU resources. Requests need explicit ownership and completion rules, particularly if a backend retains a buffer while a transfer is pending.

Input events should carry endpoint identity and sequence information. Preserve close, resize, focus, keyboard, pointer, and text-input distinctions. Text input is not identical to key presses. Event queues need explicit overflow behavior; dropping events silently can leave buttons or keys stuck logically pressed.

The current tightly packed RGBA8 buffer is a useful initial common presentation format. Its existing synchronous callback does not yet implement the richer request/event model above.

## Rendering and fallback: open choice

There are two separate portability questions:

1. Can the same completed pixels be presented through different window/display backends?
2. Can the same rendering request be executed by either a GPU or a software renderer?

The existing interface begins to answer the first. It does not answer the second. Sending CPU-rendered pixels through a GPU upload is accelerated presentation, not automatically GPU rendering.

**Recommended first milestone:** portable pixel surfaces plus streams/events, with rendering in neo. Add GPU capabilities separately when the renderer's needs are concrete.

**Alternative:** define a portable rendering-command contract and implement both GPU and software execution for its supported subset. This requires agreed operations, resource formats, blending/clipping semantics, precision tolerances, and completion rules. Arbitrary GPU shaders cannot be promised to fall back to software without implementing their execution model.

Fallback belongs to a graph-level policy object. Backends report capabilities and errors. Policy chooses a compatible backend or an explicit degraded representation; it must not claim success while dropping unsupported rendering. A text terminal might offer a textual or coarse character-cell view, but that is a different view with declared limitations, not an equivalent pixel display.

Switching backend requires a way to reconstruct resources and redraw. Outstanding requests must complete, fail, or be cancelled explicitly. External resources also require restoration protocols after image loading; serializing logical endpoint objects does not serialize an OS connection or GPU context.

## Implementation sequence

1. Define stream results, byte-buffer ownership, and capability grants; implement terminal/pipe adapters with partial-I/O and failure tests.
2. Define surface properties and input events; adapt X11 and add a headless surface for deterministic tests.
3. Connect waiting/completion behavior to graph-resident requests and scheduling as that execution model becomes available. Keep any interim host loop visibly provisional.
4. Resolve the rendering-contract choice above, then implement the chosen software/GPU capabilities and explicit fallback policy.
5. Build GUI and spatial views in neo on these interfaces.

Tests should cover short I/O, end-of-input, denied operations, cancellation, endpoint loss, queue limits, resize, surface lifetime, and resource cleanup. Backend-equivalence tests should compare only operations whose semantics the contract actually guarantees.

## Pharo comparison and recommendation

Research reference, not an accepted neo dependency choice:

- Pharo 13's [OSWindow source](https://github.com/pharo-project/pharo/blob/Pharo13/src/OSWindow-Core/OSWindow.class.st) separates operating-system window control and event handlers from renderer selection. It exposes Form, OpenGL, Athens, and generic renderer factories; these are distinct rendering paths, not a guarantee that arbitrary OpenGL operations have a software equivalent.
- [Bloc](https://github.com/pharo-graphics/Bloc) provides UI infrastructure. [Alexandrie](https://github.com/pharo-graphics/Alexandrie) provides a 2D canvas above native Cairo, FreeType, and HarfBuzz bindings. Pharo therefore uses native graphics libraries as well as image-level objects; it does not keep every rendering algorithm in Smalltalk.
- The [July 2026 Bloc release](https://pharo.org/news/2026-07-18-BlocRelease.html) documents an optional OSWindow-SDL3 host package. This reinforces the separation between UI, rendering, and the window host; it does not establish universal automatic fallback.

Recommendation for neo: define separate presentation and rendering interfaces. Implement portable streams/events and CPU-pixel presentation first. Then define a small drawing protocol in neo with a software implementation as the reference. A later GPU renderer may implement that same protocol without making GUI objects depend on GPU command syntax. Keep CPU readback optional on GPU paths; do not force every accelerated frame through a CPU pixel buffer. Only promise software fallback for the shared drawing subset. Spatial projection remains above these backends, in neo.

This refines the earlier two-way question: pixel surfaces and drawing commands belong at different boundaries. They need not be mutually exclusive architecture choices. The implementation sequence can still begin with the existing pixel surface.
