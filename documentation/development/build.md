# Build neo with Make

For the current persistence and lifecycle acceptance image, see
[image exercise](image-exercise.md). Its Windows and Linux checks run as
part of `make test`; its interactive runner uses the same `neo` executable.

The repository-root Makefile is the single build entry point. There are
no PowerShell or Bash build scripts. GNU Make selects sources, compiler
flags, target paths, linking, and tests.

On Windows, use the MSYS2 UCRT64 GCC toolchain and GNU Make. Its Make
executable is named `mingw32-make`; substitute that for `make` below.
Windows recipes explicitly use cmd.exe, regardless of an installed sh.
Node.js runs the regression driver, standalone source embedding, and boot tests. The driver starts
processes directly; it does not invoke a shell or compile the VM.

## Targets and paths

| Target | Executable path | Status |
| --- | --- | --- |
| x86_64/Windows | build/x86_64/Windows/neo.exe | Implemented |
| x86_64/Linux | build/x86_64/Linux/neo | Implemented |
| x86_64/x86_64 | build/x86_64/x86_64/neo.bin | UEFI boot profile |
| arm/Windows, arm/Linux, arm/arm | build/arm/PLATFORM/neo with the corresponding suffix | Stubs |
| arm64/Windows, arm64/Linux, arm64/macOS, arm64/arm64 | build/arm64/PLATFORM/neo with the corresponding suffix | Stubs |

A standalone target uses the architecture again in the second directory. No metal,
baremetal, or neo platform directory is introduced. `x86_64` is also a
Make target and TARGET alias for x86_64/x86_64.

A stub exits unsuccessfully and explains which adapter is absent. It
never creates a dummy executable or reuses an old artifact as a working
build. Naming a target does not establish runtime support.

```text
make targets
make x86_64/Windows
make TARGET=x86_64/Windows
make path TARGET=x86_64/Linux
make path TARGET=x86_64
make x86_64/Linux
make x86_64
```

The default target is x86_64/Windows on Windows and x86_64/Linux
elsewhere. This initial default assumes an x86-64 host. Other
architectures must select a listed TARGET explicitly.

## Compiler and tests

```text
make CC=C:/msys64/ucrt64/bin/gcc.exe
make test TARGET=x86_64/Windows
make run TARGET=x86_64/Windows
make -B OPT=-O0
```

Default flags are C17, -O3, and strict warnings treated as errors.
CC, CPPFLAGS, CFLAGS, OPT, LDFLAGS, LDLIBS, and NODE can be overridden.
Use -B after changing compiler or flags to force recompilation. Source,
header, and Makefile changes trigger an ordinary rebuild automatically.
Standalone builds use intermediate object files. Executable names remain neo.exe, neo, or neo.bin for the selected target.

The Windows adapter links userenv. Platform services also link Winsock,
BCrypt, and GDI for loopback
networking, secure entropy, and opaque framebuffer presentation.
After linking, Make grants
AppContainers read/execute access to neo.exe, preserving the isolated
native worker launch contract. Tests fail if the worker cannot launch;
there is no unrestricted execution fallback.

`make test` runs the native selftests and the migrated regression suite,
including live editing, rich values, object lifecycle, package grants,
event delivery, idle input, source views, and snapshot reopening.
Test data is under ../depth/tools/regression-data.json. Temporary source files
are created in a unique temporary directory and removed afterwards.

`make clean` removes only the selected target executable. It does not
recursively delete the build tree, images, or unrelated files.

The sole canonical image remains ../neo-image/neo-os/image.neo. Builds never copy
it to build/. Existing binaries or ISOs from earlier runtimes do not
represent supported targets in this rebuild.

## Current platform implementations

Linux provides a seccomp-contained native worker, X11 input, GLX/software
presentation, managed byte storage, loopback networking, ALSA PCM audio,
and UTF-8 clipboard. Windows provides software/OpenGL surfaces, WinMM PCM
audio, and clipboard mechanisms. Drag/drop and hotplug remain open.

The standalone backend is ../depth/plugins/x86_64/, alongside architecture
names such as arm64. Output is build/x86_64/x86_64/neo.bin and neo.iso.
neo.bin is a UEFI PE/COFF executable, not a flat raw kernel. The ISO boots
through UEFI and is not an installer. The canonical image is embedded as
build data; image.neo remains exclusively under neo/neo-os/.

This profile exits firmware services and provides framebuffer output,
timers, PS/2 input, serial CLI, and trusted integer JIT execution. It has
no hosted worker isolation, binary64 support, persistent storage, network,
USB, or audio drivers. QEMU/OVMF surface and CLI tests pass. Physical
hardware remains unverified.

```text
make test TARGET=x86_64/Linux
make test TARGET=x86_64 BOOT_MODE=surface
make test TARGET=x86_64 BOOT_MODE=cli
make TARGET=x86_64 BOOT_MODE=surface
```

On Windows the same Makefile delegates Linux and standalone builds to WSL
Debian; WSL_DISTRO selects another distribution. Linux requires GCC, Make,
Node.js, libseccomp, X11, OpenGL, and ALSA development packages. Standalone
builds also require GNU-EFI, binutils, mtools, dosfstools, and xorriso.
Boot tests use QEMU x86 and OVMF. GNU-EFI notices accompany build artifacts.

## Current launch milestone

The [launch and language milestones](launch-milestones.md) track the next phase.
M1 adds inert launch metadata and package export, plus a text-tool deployment
diagnostic. Run these with the selected target's neo executable:

```text
neo launch-info ../neo-image/neo-os/image.neo text-tool
neo program-check ../neo-image/neo-os/image.neo text-tool
```

The diagnostic is not the finished script or GUI launcher. Its checks are part
of make test. Image behavior stays in neo; orchestration is implemented in C.

## Sibling repository build

The actual implementations are in ../depth and the live image is in
../neo-image/neo-os/image.neo. The root Makefile remains the only build file.
Use DEPTH_DIR and IMAGE_DIR to override the sibling roots. A matching component
revision pair is recorded in components.json. No submodules or nested Git
repositories are used.
