# Source tour

The repository contains one native VM and one canonical source image.

| Source | Responsibility |
| --- | --- |
| neo-vm/jit.c and jit.h | Warp parsing, graph validation, native code generation, transactional sends, source views, and grants |
| neo-vm/weft.c and weft.h | Readable source projection and lowering |
| neo-vm/lattice.c and lattice.h | Dimensional placement and projection support |
| neo-vm/literal.h | Shared literal limits and encoding |
| neo-vm/events.c and events.h | Bounded scheduled messages and asynchronous read bridge |
| neo-vm/services.c and services.h | Opaque resource handles, requests, buffers, managed storage, and headless implementation |
| neo-vm/presentation.c and presentation.h | Framebuffer transport and normalized input dispatch |
| neo-vm/exercise.c and exercise.h | Hosted lifecycle acceptance harness and scalar persistence bridge |
| neo-vm/hosted.c | CLI entry point, live session commands, and native acceptance tests |
| neo-vm/console.c | Common console line reader and idle pumping |
| neo-vm/platform/ | Host SDK and architecture adapters |
| neo/neo-os/image.neo | Image-owned exercise behavior, state, drawing, and arithmetic |
| tools/regression.mjs and regression-data.json | Cross-process regression checks and inert fixtures |
| tools/embed-image.mjs and test-boot.mjs | Standalone image embedding and QEMU/OVMF boot checks |

Start with [the exercise guide](../development/image-exercise.md), then follow
its graph handlers into the JIT and the explicitly granted service callbacks.
The C runtime must not acquire application or desktop policy as OS work grows.
