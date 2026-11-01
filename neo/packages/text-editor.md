# Text editor and shared documents

neo.apps.text-editor exports text-editor. Apps > Text Editor creates a fresh blank
document. Browser TEXT EDITOR creates a new view of its selected document, sharing
text, undo/redo and draft state while keeping independent caret/scroll/selection.

Click to place the caret; drag or Shift+arrows selects text. Arrow keys, Home/End,
Backspace/Delete, Enter and Tab provide basic multiline editing. Wheel scrolls the
text; Ctrl+wheel/pinch zooms the desktop. Text storage is UTF-8 with boundary-aware
cursor/splice operations. The current bitmap renderer is ASCII with fallback glyphs;
full Unicode shaping and font quality remain deferred.

| Shortcut | Action |
| --- | --- |
| Ctrl+A / C / X / V | Select all / copy / cut / paste |
| Ctrl+Z / Y | Undo / redo, up to 32 text snapshots per document |
| Ctrl+Enter | Request validated code application after the input callback returns |
| Ctrl+S | KEEP in this loaded image session |

The toolbar exposes APPLY, PUBLISH, KEEP, UNDO and REDO. Plain text has no execution
target; APPLY explains that KEEP retains it. Package drafts publish through the
package manager. Live handler drafts edit the selected instance, preserving state
outside the handler. Invalid or stale source leaves running code intact.

The desktop supplies a shared internal clipboard and optionally a native Windows
clipboard stream. Native clipboard access happens only on copy/paste. Headless
tests use the internal fallback and do not replace the user's host clipboard.

Closing X terminates the editor, releases its glyph/pixel resources and removes its
document ownership. A document stays alive while another view owns it, or after KEEP.
Kept documents appear in Browser DOCUMENTS. There is no file dialog or durable disk
save workflow yet. The default document bound is 256 KiB and history is session data.
