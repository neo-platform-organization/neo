# Image source browser

neo.ui.package-browser exports browser. Each instance has its own window,
selection, editor view and document cache. The root browser retains the existing
terminal main/emit handlers; GUI browsers are fresh package instances.

PACKAGES lists installed packages. Select a row, then use the export arrows and
CODE arrows to select an export and handler. The pane shows canonical neo source,
initially read only. Packages are copied into document working copies; inspecting
or applying a draft never changes the installed package directly.

APPS selects live workspace objects and their handlers. Applying one of those
drafts edits that particular instance. DOCUMENTS opens retained/shared documents.
A browser caches visited drafts, so switching selections does not discard edits.

| Action | Result |
| --- | --- |
| OPEN APP / OBJECT | Create a fresh selected package export through package-manager.instance |
| GO TO APP | Navigate to the selected existing window |
| NEW INSTANCE | Copy its package prototype, including for a selected running app |
| OPEN IN EDITOR | Enable editing in the embedded pane |
| TEXT EDITOR | Open that same document and history in a new full editor window |
| APPLY / Ctrl+Enter | Validate syntax, check the baseline, and replace selected code |
| PUBLISH | Apply package draft and submit a versioned update through package-manager.update |
| KEEP / Ctrl+S | Retain the document in this loaded image session |

Invalid syntax reports line/column diagnostics and keeps previous code. A stale
baseline is rejected instead of overwriting another editor's applied change.
Publication increments the patch version and goes through dependency/grant policy.
Existing app instances retain their code; future copies use the published version.
Non-GUI exports are created as inert workspace data, without auto-running main.

Multiple windows share text/history only when explicitly opening the same document.
The pop-out button does not clone its draft. Caret, selection and scrolling remain
per view. Closing a browser releases its ownership; an open pop-out still owns the
document. Closing the last view frees unkept text/history and package draft data.

Browser receives explicit manager/workspace/document service authority. It does
not obtain authority by guessing object names. Rendering, source selection and
publication workflows are neo code. See [text editor](text-editor.md) and
[generic editing mechanisms](../../documentation/reference/source-editing.md).
