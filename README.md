# neo

neo is a live programming platform built around Warp, a mutable object graph.
Weft presents object behavior in a readable form; Knight provides pure
calculations. The long-term aim is an OS and game engine whose tools and
applications live in the image.

This release is the foundation snapshot before OS development. It includes
the native JIT, scoped package services, hosted platform backends, a standalone
x86_64 boot profile, and an image lifecycle exercise. It is not a finished OS.

## Build and test

GNU Make is the single build entry point. On Windows, use MSYS2 UCRT64 GCC
and `mingw32-make` in place of `make`.

```text
make test TARGET=x86_64/Windows
make test TARGET=x86_64/Linux
make test TARGET=x86_64 BOOT_MODE=surface
make test TARGET=x86_64 BOOT_MODE=cli
```

Windows builds Linux and standalone targets through WSL. See the
[build guide](documentation/development/build.md) for dependencies and limits.
Release archives contain source, not generated binaries or a build directory.

| Target | Output |
| --- | --- |
| x86_64/Windows | build/x86_64/Windows/neo.exe |
| x86_64/Linux | build/x86_64/Linux/neo |
| x86_64/x86_64 | build/x86_64/x86_64/neo.bin and neo.iso |

The canonical image is always `neo/neo-os/image.neo`. Builds never place a
loose copy of it in the build directory. The standalone build embeds it.

## Run the exercise

From the repository root, create a directory for persistent exercise data:

```text
mkdir exercise-data
.\build\x86_64\Windows\neo.exe exercise .\neo\neo-os\image.neo lifecycle-exercise .\exercise-data
```

Click or type to change the value. F2 saves, F3 restores, Escape closes.
Closing does not save. Green indicates saved state; orange indicates changes.
On Linux, use `./build/x86_64/Linux/neo` with the same arguments.

The exercise tests input, scoped access, timers, failed edits, durable scalar
saves, snapshot reopening, and shutdown. The regression suite verifies saved
state across separate processes. See [the exercise guide](documentation/development/image-exercise.md).

## Boundaries and limits

C provides graph memory and validation, JIT execution, capability checks,
resource lifetime, and platform mechanisms. The image owns exercise behavior,
state, arithmetic definitions, drawing, and save/restore choices. The hosted
exercise harness supplies a narrow scalar persistence bridge.

Windows and Linux provide software/OpenGL framebuffer presentation, managed
byte storage, loopback TCP/UDP, PCM audio, and clipboard mechanisms. The
standalone x86_64 UEFI profile provides framebuffer output, timers, PS/2 input,
serial CLI, and trusted integer JIT execution. It lacks hosted worker isolation,
binary64, persistent disk storage, networking, USB, and audio drivers.
The ISO is boot media, not an installer. Physical hardware is unverified.

General asynchronous graph I/O and atomic whole-image checkpointing remain
unfinished. Collections, values, native activations, and event queues have
fixed limits. This snapshot establishes a tested starting point for the OS,
not a promise that the remaining language or OS work is complete.

- [Runtime and image syntax](neo/documentation/runtime-format.md)
- [Platform services](documentation/reference/platform-services.md)
- [Foundation acceptance](documentation/development/foundation-acceptance.md)
- [Source tour](documentation/learning/code-tour.md)
- [Whitepaper](neo/documentation/whitepaper.md), preserved design reference
- [Release notes](documentation/releases/v0.1.0-foundation.md)
