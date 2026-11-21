# neo: three repositories

neo is split into three independent repositories. They are checked out beside
each other, not nested or embedded as submodules:

| Repository | Local directory | Responsibility |
| --- | --- | --- |
| [neo](https://github.com/neo-platform-organization/neo) | neo/ | Surface VM: public language contract, minimal entry, and validated plugin ABIs |
| [neo-depth](https://github.com/neo-platform-organization/neo-depth) | depth/ | Trusted compiler/graph engine, platform plugins, resources, preOS, launcher, and tests |
| [neo-image](https://github.com/neo-platform-organization/neo-image) | neo-image/ | Live image, programs, application state, and image-side behavior |

```text
git clone https://github.com/neo-platform-organization/neo.git neo
git clone https://github.com/neo-platform-organization/neo-depth.git depth
git clone https://github.com/neo-platform-organization/neo-image.git neo-image
cd neo
make test TARGET=x86_64/Windows
```

Use mingw32-make with the MSYS2 UCRT64 toolchain on Windows. One Make entry
coordinates all three repositories. DEPTH_DIR and IMAGE_DIR can override the
sibling paths. Compatible component commits are recorded in components.json.

```text
make test TARGET=x86_64/Linux
make test TARGET=x86_64 BOOT_MODE=surface
make test TARGET=x86_64 BOOT_MODE=cli
```

Outputs remain in build/x86_64/Windows, build/x86_64/Linux, or
build/x86_64/x86_64. Executable names remain neo.exe, neo, and neo.bin.
The sole canonical image is ../neo-image/neo-os/image.neo.

## Surface and depth

The surface owns the public language and execution contract and validates the
language/depth interfaces. The graph engine, compiler, platform code, and
preOS machinery are supplied by trusted depth plugins.

Depth owns OS/hardware SDK code and replaceable native mechanisms. Its first
provider is linked at build time so the same interface works during standalone
boot. Dynamic library loading and hot replacement are not implemented.
Depth is trusted code; plugins are not an additional security sandbox.

This split reduces the surface but does not make the parser or JIT trivial.
Further simplification must preserve language rules and tests rather than hide
semantic complexity behind arbitrary plugin calls.

## Current execution

Programs run within an image. The same package can be exported to a minimal
image or kept in a full live environment. Host windows will present the same
application instance; window placement is not image or application instantiation.
Internal presentation remains the developer-default unless overridden.

```text
.\build\x86_64\Windows\neo.exe script ..\neo-image\neo-os\image.neo --program text-tool --print-result -- "hello world"
.\build\x86_64\Windows\neo.exe script ..\neo-image\neo-os\image.neo --program text-tool --repl -- hello
```

The sample counts UTF-8 bytes. The second command enters the REPL on that same
running image and instance. Script streams/arguments are explicitly granted;
REPL command input is reserved while attached. Async byte I/O and cleanup have
acceptance checks. General filesystem/network constructors, remote REPL attach,
complete language specifications, and the launcher GUI remain unfinished.

The standalone x86_64 profile boots through UEFI with framebuffer, timer, PS/2,
serial CLI, and trusted integer JIT execution. It lacks hosted worker isolation,
binary64, persistent storage, networking, USB, and audio drivers. The ISO is boot
media, not an installer. Physical hardware remains unverified.

See [architecture](documentation/development/three-repositories.md),
[build](documentation/development/build.md), and
[milestones](documentation/development/launch-milestones.md).
The [foundation snapshot](https://github.com/neo-platform-organization/neo-foundation-snapshot)
is archived and unchanged.
