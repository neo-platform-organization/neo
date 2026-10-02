# 0001: Platform-independent image capabilities

Status: accepted direction, with provisional implementation details.

[Documentation index](../../README.md) · [Architecture](../architecture.md)

## Context and decision

The user explicitly confirmed that image objects should use windows, buffers, streams, and rendering capabilities without knowing Vulkan/OpenGL, X11/Win32, or OS descriptor details. The kernel implements platform-specific mechanisms. Rendering logic, maths, views, and GUI behavior belong in neo.

Separate rendering from presentation. A software renderer produces pixels; an eventual accelerated renderer uses abstract graphics capabilities. A pixel buffer alone does not define portable GPU rendering. Backend capability and failure information remains observable without leaking native handles.

## Alternatives and consequences

Exposing platform APIs directly to ordinary image code would couple objects to one host. Hiding everything behind a pixel buffer would support software rendering but would not define portable GPU execution. Separate interfaces preserve both possibilities.

External endpoint state cannot be restored merely by serializing an object graph. Resource rebinding, pending operation ownership, and completion ordering remain explicit design work. Different backends need not claim support for operations they cannot implement.

## Current evidence and provisional subset

[The I/O reference](../../reference/io-interface.md) describes the first implementation: capability-checked stream callbacks, a POSIX adapter, RGBA presentation, and normalized input polling. Current scalar read/write results and native event queues are provisional; asynchronous graph messages and scheduling remain future work. Tests cover partial transfers, EOF/would-block, authority, cleanup, normalized input, and explicit overflow.
