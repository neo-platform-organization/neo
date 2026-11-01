# GUI environment

The [desktop](../../neo/packages/desktop.md) owns one top bar, an Apps dropdown,
private app windows and the infinite canvas viewport. Dropdown selection creates
a fresh Browser, Sketch, Text Editor or Task Manager; top-bar buttons visit running
instances. X terminates an app and releases transient state/resources.

The [source browser](../../neo/packages/package-browser.md) previews and edits neo
handlers in an embedded editor, with a pop-out into the [text editor](../../neo/packages/text-editor.md).
Documents, history, safe-application requests and package publication policy are
image code. Native mechanisms and validation boundaries are documented in
[source editing](source-editing.md).

```powershell
./build-windows.ps1 -Compiler C:/msys64/ucrt64/bin/gcc.exe -Optimize
./build-gui-image.ps1
./build/x86_64/Windows/neo.exe --gui build/x86_64/Windows/image.neo
```

Preparation installs all packages into one saved image source. GUI launch loads
only that image, without a source-module loader. Existing open sessions remain
independent. Font shaping, durable document storage and JIT/GPU work are deferred.

Run neo.exe selftest gui for portable headless UI/editor/lifecycle checks.
The full selftest includes those tests and the networking/runtime regression suite.
New editing workflows are validated headlessly; this revision does not claim a
new physical keyboard/mouse acceptance pass. Headless clipboard checks use local
image data and never overwrite the host clipboard.
