# neo language documentation

Current desktop: [single top bar and launcher](../packages/desktop.md), with
[Task Manager](../packages/task-manager.md) and [Text Editor](../packages/text-editor.md)
as ordinary app windows. The [source browser](../packages/package-browser.md) edits
handlers inline, pops out shared documents and publishes package drafts. Existing
canvas pan/zoom is retained; windows have only a close control and no minimized state.

These Markdown guides travel with the language/image directory when it moves to
its own repository. VM compilation and C APIs are documented separately in
[the VM documentation](../../documentation/README.md).

- [Syntax cheat sheet](../../documentation/reference/syntax-cheat-sheet.md): literals, operations, and launch examples.
- [Runtime format](runtime-format.md): executable graph format and behavior contracts.
- [Software renderer](software-renderer.md): self-contained rendering images.
- [Projected vertex renderer](../packages/point-renderer.md): image package over compact spatial views and protected RGBA buffers.
- [Viewport line renderer](../packages/line-renderer.md): source topology, row mappings and bounded viewport sampling in image behavior.
- [Homogeneous segment clipper](../packages/segment-clipper.md): exact edge intervals, generated projected topology and provenance.
- [Triangle renderer](../packages/triangle-renderer.md): pixel-centre coverage, flat face colour and explicit screen-space depth.
- [UI canvas](../packages/canvas.md): minimum bitmap-text and rectangle foundation.
- [Source browser](../packages/package-browser.md): package drafts, live handler editing and app instances.
- [Sketch](../packages/sketch.md): inspectable image-owned drawing app.
- [Desktop model](../packages/desktop-model.md): a separate canvas/window policy foundation.
- [Whitepaper](whitepaper.md): unchanged original reference, not an executable specification.

The examples currently use commands from the VM repository root. Collaboration
planning and design decisions live outside this repository in the project brain.
# Image desktop

See [desktop session](../packages/desktop.md) for managed applications, the browser
shell, compositor policy and launcher, all implemented as neo graph behavior.
