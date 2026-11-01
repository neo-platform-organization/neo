# neo

neo is a live object environment built around one object graph. Objects hold data
and behavior, connect through explicit capabilities, and communicate by messages.
A C17 VM handles execution, memory, permissions and host devices. The image holds
the desktop, apps, package manager and system policy.

The Windows image has a desktop, source browser, text editor, Sketch and Task
Manager. You can run several instances of an app, inspect its handlers, edit code
while it is running, and publish package changes for future instances. Spatial
packages work with arbitrary dimensions. Windows networking supports TCP, pinned
mutual TLS and selected-state replication. Linux networking is still a stub.

This is a working prototype. Fonts are basic, documents are kept in memory, and
there is no filesystem-backed document workflow, JIT or GPU renderer yet.

## Build and run on Windows

Install MSYS2 UCRT64 GCC and add its bin directory to PATH. From PowerShell in the
repository directory:

```powershell
./build-windows.ps1 -Optimize
./build-gui-image.ps1
./build/x86_64/Windows/neo.exe --gui build/x86_64/Windows/image.neo
```

The names stay `neo.exe` and `image.neo`. Close a running executable before
replacing it. The image builder installs the bundled GUI packages into one image;
launching it does not load separate source files.

Apps in the top-left menu creates a new instance. A top-bar button takes you to
an existing window. X closes the app and releases its state and resources. Drag a
window title to move it, use the middle mouse button to pan, and scroll over the
background to zoom. Scrolling over an editor scrolls its text. Ctrl+wheel zooms
the desktop.

In Browser, select a package or app and a handler to read its source. OPEN IN
EDITOR enables the embedded editor. TEXT EDITOR opens the same document in a
separate window. Ctrl+Enter applies code; invalid or stale edits keep the old
code. PUBLISH updates the package for future instances. KEEP retains a document
in the current session, not on disk.

See the [desktop guide](neo/packages/desktop.md),
[text editor](neo/packages/text-editor.md) and
[Windows build notes](documentation/development/build.md#windows-x86-64).

## Linux

Install a C17 compiler, Make, pkg-config and Xlib development headers:

```sh
make
make test
./build/x86_64/Linux/neo help
./build/x86_64/Linux/neo live neo/neo-os/image.neo
```

Use `make WITH_X11=0` for a build without X11. `make targets` lists platform
choices. Windows and Linux x86-64 have implementations; other targets are stubs.
Windows is the current desktop development target.

## Images and the command line

```text
neo [--cli] [--gui] IMAGE [ACTOR [HANDLER]]
neo live IMAGE
neo selftest [gui]
```

`neo main.neo` uses the image's entry point. An explicit actor and handler are
optional debugging arguments. One process owns one VM and one loaded image graph.
The `live` command opens an interactive terminal session for inspecting, editing,
invoking and saving that graph. Native resources cannot be saved in an image.

## Tests

```powershell
./build/x86_64/Windows/neo.exe selftest
./build/x86_64/Windows/neo.exe selftest gui
```

The full suite covers permissions, object lifetime, allocation failures, messages,
image save/reload, scheduling, packages, networking, spatial operations and the
headless desktop/editor workflow. Networking tests use local sockets and temporary
TLS certificates. Optional Python scripts in `neo/packages/tests` compare spatial
results with independent calculations. Python is not part of the runtime or GUI
build.

The Windows suite passes. The latest editor checks use headless input and pixels;
physical input and native clipboard behavior still need an interactive check.

## Source layout

- `neo-vm/`: C runtime, platform adapters and command-line tools.
- `neo/neo-os/image.neo`: OS seed image.
- `neo/packages/`: package sources, guides and spatial test scripts.
- `neo/fixtures/`: test images.
- `documentation/`: VM and platform documentation.
- `neo/documentation/`: language and image documentation.
- `build/ARCH/OS/`: generated executables and prepared images, excluded from Git.

Start with the [documentation index](documentation/README.md),
[syntax guide](documentation/reference/syntax-cheat-sheet.md) or
[runtime format](neo/documentation/runtime-format.md).

Licensed under [GPLv3](LICENSE).
