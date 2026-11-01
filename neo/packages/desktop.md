# Image desktop session

The desktop is one top bar over a navigable canvas. Apps opens a dropdown listing
Browser, Sketch, Text Editor and Task Manager. Every dropdown click creates a fresh
instance. Each running instance has its own top-bar button; clicking it navigates
to that window. X terminates the instance and releases its private buffers,
endpoints and transient state. Windows are never minimized.

Desktop, browser, editor, document history, app lifecycle and package publication
policy are neo image behavior. C provides generic protected graph/source/text,
stream and display mechanisms. GUI launch loads one prepared image source.

## Launch

From the repository root:

```powershell
./build-windows.ps1 -Compiler C:/msys64/ucrt64/bin/gcc.exe -Optimize
./build-gui-image.ps1
./build/x86_64/Windows/neo.exe --gui build/x86_64/Windows/image.neo
```

Preparation installs packages into one image through the existing package manager.
The canonical seed starts with an empty catalogue; prepare the GUI image first.
Older running executables/images are separate sessions and do not update themselves.

## Controls

| Action | Control |
| --- | --- |
| New instance | Apps dropdown, or Browser NEW INSTANCE |
| Visit an existing instance | Its top-bar button, Browser GO TO APP, or Task Manager |
| Move / resize view | Drag title / bottom-right corner |
| Terminate instance | Title X or Task Manager CLOSE WINDOW |
| Pan canvas | Middle-button drag |
| Zoom around pointer | Background wheel, pinch, or Windows Ctrl+wheel |
| Scroll text | Wheel over Browser/Text Editor content |
| Dismiss dropdown/error | Escape |

The top bar contains only running instances. Navigation changes the camera, not
window source coordinates. Windows retain nD fixed-point coordinate arrays;
model.view-axes selects the two projected axes and generates missing zero storage
as needed. Moving preserves all unselected coordinates. The sample session limits
open GUI windows to eight; this is image policy, not a dimensionality limit.

## Ownership and authority

model.windows references app actors in workspace. surfaces/windows owns each view's
private endpoint, pixels and cached title. App buffer/window grants are READ|WRITE;
apps cannot control the native window or consume another app's events. Browser's
embedded editor and renderer receive their own explicit grants from the desktop.
Task Manager gets a READ model plus a writable request mailbox.

Closing deletes the actor and its per-view resources. Reopening copies the installed
package prototype into a new actor with a unique instance key. It never copies a
running actor's native resources. Documents are separate objects in desktop.documents.
Each editor/browser registers ownership; the last owner closing releases an unkept
document. KEEP retains it in the loaded image, accessible under Browser DOCUMENTS.
This is session retention, not durable disk saving or a filesystem implementation.

Requests are serviced after app callbacks return. App callbacks have bounded child
budgets; an exhausted/broken app can be repaired or closed while the desktop stays
alive. Applied code affects subsequent calls and preserves unrelated app data.
See [source browser](package-browser.md) and [text editor](text-editor.md).
