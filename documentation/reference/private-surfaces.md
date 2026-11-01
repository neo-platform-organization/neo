# Private display and buffer mechanisms

These primitives supply memory/event mechanisms to image services. They do not
implement a desktop, window records, application lifecycle, layout or widgets.
The [desktop session](../../neo/packages/desktop.md) implements those in neo.

| Form | Operands / contract |
| --- | --- |
| buffer-create | target reference, name text, positive width/height; INSERT required on parent; zeroed RGBA8 buffer |
| surface-create | target reference, name text, positive width/height; INSERT required; private bounded headless display endpoint, no native OS window |
| surface-focus | target endpoint reference, value boolean; WRITE+ACT controller authority; loss resets capture/input generation |
| surface-route | target endpoint reference, source window reference, x/y integers; WRITE+ACT on endpoint, READ snapshot on source; forwards key/text/pointer/button snapshot with explicit local coordinates |
| buffer-copy | target destination reference, source reference, x/y destination coordinates, width/height source region from0,0; WRITE destination/READ source, clipped at destination edges |
| buffer-scale-copy | Same, plus output-width/output-height; generic nearest-neighbour RGBA transfer into explicit destination extents; no camera, axes, picking or zoom policy |

Buffer copy is raw RGBA, without scaling/blending or z-order. Source and destination
must differ. Source region bounds are validated before writes; negative/offscreen
destination coordinates are clipped, including INT64_MIN without overflow. Buffer
allocations retain the existing64MiB limit. Copy work is charged before mutation
in64-byte units of the clipped destination region. Event queues retain the bounded headless
provider contract. App READ+WRITE grants cannot inject events or set endpoint focus.

Existing buffer/window operations also accept protected target references, while
retaining connection-name operands for existing apps. `window-present` continues
to resolve its buffer operand by explicit connection name. Presenting to a private
endpoint reaches only its provider; it never presents the native host window.

Resources are owned by graph objects and released on delete/unload/VM destruction.
They are not serialized or duplicated as graph data. The GUI runner accepts an
optional root `gui-entry` and positive `gui-width`/`gui-height` fields on that
actor, defaulting to the existing entry and640×480 when absent. These fields are
generic frontend configuration; desktop behavior remains in the image.

Normalized `pinch` input uses x/y as the anchor and positive width/height as an
incremental scale ratio. The core validates it; the image chooses how to interpret
it. Win32 translates GID_ZOOM and Ctrl+wheel, resets partial gesture state on input
loss, and leaves zoom behavior to neo. The headless provider supports deterministic
pinch injection; real touchpad/touchscreen delivery depends on device/OS support.
