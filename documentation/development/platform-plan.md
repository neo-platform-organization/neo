# Platform implementation plan

Checkpoint, 2026-11-21: initial P1–P6 Windows/headless implementations
and their acceptance tests pass. See [implemented services and remaining
limits](../reference/platform-services.md). Storage is deliberately an
opaque managed key/byte store, keeping directory schema in the image.
Networking is initially IPv4 loopback. General host filesystem sandboxing,
full terminal integration, arbitrary network endpoints, and exhaustive
native error diagnostics remain outside this first bounded implementation.
P7 Linux and P8 standalone x86_64 now pass hosted and QEMU/OVMF tests. P9 has hosted software/OpenGL presentation, PCM audio, and clipboard mechanisms. Drag/drop, hotplug, and broader device recovery remain open. Standalone execution is trusted and integer-only.

Proposed sequence, 2026-11-20. This starts after the bounded language
foundation. It does not assume that registered Make targets are working
platform implementations.

## Existing starting point

Windows provides native worker execution, console input with idle
pumping, clocks, waits, and small asynchronous file reads. Generic code
includes platform.h; Windows APIs live in platform/windows.c.
The event scheduler already supports injected time and completion output.
At the starting checkpoint Linux and standalone x86_64 were build stubs. Normalized device input,
framebuffer presentation, general storage, and networking are absent.

## Boundary

| Layer | Responsibility |
| --- | --- |
| VM | Graph identity and memory safety, JIT, activation limits, grants, request validation, publication, and staged graph writes |
| Platform | Memory pages, executable-code publication, clocks, waits, byte transport, devices, surfaces, and host or hardware resource lifetime |
| Image | Command workflows, document and filesystem schema, network protocols, rendering, controls, focus, desktop, and app behavior |

The trusted bootstrap may select an image, attach grants, and report
startup failure. It must remain separate from application behavior.
Neither the platform nor a drawing primitive knows what a button,
package browser, text editor, or desktop is.

Use ordinary C functions and small service tables with a context pointer.
Introduce interfaces when their first implementation and contract test
arrive. Do not create a large unused device framework.

## P1. Settle the common service contract

Specify the shared rules before adding another backend:

- Explicit statuses, including unsupported, denied, pending, cancelled,
  closed, exhausted, timeout, and device loss. Preserve a diagnostic
  separately from the portable status.
- Opaque, typed resource handles with generations, ownership, close,
  and revocation rules. A stale handle cannot become valid through reuse.
- Per-instance adapter context, capability queries, and resource budgets.
  No global current platform and no host handles in serialized graphs.
- Requests with unique IDs, deadlines, and completion events. Define
  partial transfer, EOF, cancellation races, queue exhaustion, and
  borrowed versus owned buffer lifetime. An accepted request has one
  terminal result, including during shutdown; a rejected request starts
  no work. Late completions cannot access freed or repurposed buffers.
- One wait/poll boundary for input, deadlines, and I/O. Platform time
  and waits are mechanisms; image code chooses scheduling policy.

Keep adapter errors distinct from graph execution errors. A syscall
failure must not masquerade as a successful graph result.

Completion gate: a fake service passes lifecycle, stale-handle,
revocation, cancellation-race, saturation, and teardown tests. Existing
Windows behavior remains usable through the new contract.

## P2. Memory, native execution, time, and console transport

Route runtime allocation and executable page operations through the
boundary. Specify alignment, allocation failure, page permissions,
instruction-cache synchronization, and write-then-execute publication.
Do not allow simultaneously writable and executable code pages.
Separate the generated-code calling convention from the host ABI.

Retain the Windows worker's fault, timeout, and rollback behavior.
Move worker-specific controls out of the public cross-platform contract
where they do not represent a common service. A backend must report
its actual isolation capabilities; a worker PID is not a portable API.

Provide monotonic time with a documented unit and resolution, deadline
waits, wakeups, and secure random bytes. Wall time is a separate optional
service and is never used to order deadlines.

Expose arguments, standard streams, exit status, terminal size, optional
terminal capabilities, and EOF. Streams transfer bytes and support partial
reads/writes and backpressure. Line editing and CLI grammar remain above
the transport. Remove remaining direct host transport dependencies from
generic control code as the replacement services arrive.

Completion gate: native execution and crash recovery still pass; console
I/O uses the service boundary; a pending read cannot block event delivery;
injected allocation, clock, and stream failures leave resources balanced.

## P3. Build the headless adapter

Implement controllable time, supplied input events, memory streams,
memory storage, captured frame presentation, and scripted network
completions as their contracts become available. It is a test backend
selected by test configuration, not an additional OS target directory.

Use it to test ordering, delayed completion, partial I/O, denied grants,
device loss, resize, and shutdown without real clocks or windows.
Capture outputs through callbacks rather than console parsing where
the API can be tested directly.

Completion gate: replaying the same events produces identical graph
state and frame bytes. Scheduler tests do not require actual sleeping.
Later phases extend this same adapter instead of inventing separate
mock behavior for each platform.

## P4. Normalized input and software framebuffer presentation

Define surfaces independently of host windows. Acquire a writable frame
and present it explicitly. Specify dimensions, row stride, byte order,
RGBA8 channel layout, alpha convention, sRGB interpretation, buffer
ownership, and when presentation permits reuse. Start with copying into
an adapter-owned presentation buffer; optimize ownership transfer later.

Separate physical framebuffer pixels, logical surface coordinates, and
display scale. Resize advances the surface generation and invalidates
old frame leases. Cover show, close request, resize, focus, and display
loss without introducing a native widget vocabulary.

Normalize physical key events, composed Unicode text, pointer motion,
buttons, fractional wheel movement, and focus changes. Keep text entry
separate from key codes. Pointer events identify their surface, coordinate
space, and timestamp. Report optional touch, pen, and text-composition
support through capabilities.

Implement a headless surface and a Windows window that presents the
buffer. Window creation, event collection, and pixel transfer belong in
the adapter. Hit testing, text layout, controls, and drawing belong in
the image. Establish safe bulk pixel/buffer primitives instead of a host
call or native compilation for every pixel.

Completion gate: one control in the canonical exercise image reacts to
input and redraws. Replay the same normalized input headlessly and compare
state and frame bytes. Resize, fractional scrolling, close, and stale
frame rejection pass. No C widget or application implementation is added.

This is the first visual checkpoint. It is not a desktop milestone.

## P5. Storage and document authority

Expose scoped storage roots or opened resources, byte ranges, listing,
metadata, create/delete operations, flush, and replacement guarantees.
Host paths are adapter details; a graph cannot obtain ambient filesystem
access by constructing a path string. Define traversal and link handling
so a grant cannot escape its root.

Support asynchronous transfers and immutable byte-buffer ownership for
large data. Remove the current small text-token limit from the I/O
transport; it remains a language-value limitation until separately changed.
The image owns the Documents/Projects/Media/Downloads organization,
document headers, and file associations.

Document saves need a generation check and publication performed under
one enforced operation. Neither a timestamp check followed by rename nor
an in-process mutex is sufficient for arbitrary concurrent writers.
Specify the supported storage transaction model and report unsupported
guarantees rather than promising more than a backend can provide.

Completion gate: two writers conflict predictably, read-only grants
cannot write, revoked or stale handles fail, partial transfers work,
and an interrupted save preserves the promised previous state.

## P6. Networking and external effects

Provide scoped endpoint creation, resolution where supported, connect,
listen/accept, stream transfer, datagrams, shutdown, and cancellation.
Define endpoint-address types without exposing sockaddr or socket handles.
Specify backpressure, timeouts, peer closure, message truncation for
datagrams, and bounded receive-buffer ownership.

Implement a headless scripted transport and Windows loopback tests first.
Grant endpoint/port/direction access explicitly. Opening a connection does
not grant authority to mutate the image. Entropy and the byte transport
support a later secure-channel service; do not invent cryptography or
treat a transport address as authenticated identity.

The image owns ether exposure, replication, ownership policy, and wire
protocols. No replication implementation belongs in a socket primitive.

External effects require an explicit commit boundary. A sent packet or
an externally committed file cannot be undone by rolling back a graph
handler. Validate and admit a request before submission, then deliver
its success, failure, or uncertain outcome through the completion model.
Do not automatically retry operations whose effects may already exist.

Completion gate: two independently granted image sessions exchange bytes
over loopback. Denial, partial transfer, cancellation, revocation, and
peer failure pass. The same request lifecycle passes headlessly. This
does not yet claim distributed graph replication or secure networking.

## P7. Implement x86_64 Linux

Implement the established services behind the same interfaces. Honor the
generated JIT ABI and preserve an explicit native-execution isolation
contract rather than replacing the worker with unrestricted execution.
Choose and document the first Linux display backend before coding it;
Wayland, X11, and direct display are separate adapter capabilities.

Completion gate: Make produces build/x86_64/Linux/neo on a Linux toolchain.
The common runtime tests and normalized input/frame replay pass. Native
fault and timeout tests prove the declared containment. Validate on Linux;
cross-compilation alone is not evidence that the backend works.

## P8. Implement standalone x86_64

First choose the supported boot contract and emulated hardware profile.
A practical first proposal is x86-64 UEFI with a supplied framebuffer
and a read-only image bundle. Confirm that choice when beginning this
phase. A raw binary alone is not a boot protocol or an installer.

Implement initialization, physical and virtual memory, page protection,
exceptions, interrupts, clock/wakeups, console transport, framebuffer
presentation, and the minimal input driver set for that profile.
Image discovery reads the bundle through the storage interface.
Networking can initially report unsupported until an actual NIC driver
is implemented. Other devices report their real capabilities.

Specify the execution protection model before enabling untrusted images.
Hosted process containment does not exist automatically on metal. Kernel
mode JIT code cannot be advertised as isolated. Required privilege and
address-space separation must be implemented and tested, or the initial
boot must explicitly remain a trusted-code development configuration.

Completion gate: Make produces build/x86_64/x86_64/neo.bin and the chosen
bootable packaging without changing the canonical image's source location.
A virtual machine boots, reports available services, discovers the bundled
exercise, and runs its visible interaction. Input/state replay and frame
bytes agree with hosted execution at the same resolution, subject only
to explicitly documented presentation conversion.

ISO installation, writable disk support, hardware breadth, and an ether
network stack are separate acceptance gates, not implied by first boot.

## P9. Optional devices and acceleration

Add audio capture/playback buffers, optional clipboard and drag/drop,
external host-window placement, and device change notifications as actual
image use cases require them. Fonts, layout, and themes remain image
libraries backed by generic buffer or library services.

Implement OpenGL behind a renderer interface after software presentation
works. Keep the image's scene and drawing model independent of that API.
OpenGL is the preferred renderer where implemented; software remains an
explicit selectable path. Report the selected backend and any fallback.
Define device-loss recovery and resource release before adding another
accelerated backend. GPU rendering is distinct from GPU compute or JIT.

Completion gate: the same image works with software and accelerated
presentation, with documented differences in rasterization. Audio underrun,
device loss, unsupported host integrations, and cleanup are tested.

## Work order and build rules

Implement P1 through P4 first. That proves the platform boundary and one
image-owned visual interaction. Then complete storage and networking,
bring up Linux, and implement the standalone x86_64 boot profile. Optional services
can be added when needed without blocking the initial interaction.

Keep one root Makefile. Backend selection changes sources and libraries,
not graph APIs. Output paths remain:

```text
build/x86_64/Windows/neo.exe
build/x86_64/Linux/neo
build/x86_64/x86_64/neo.bin
```

ARM and ARM64 remain explicit stubs. The canonical source remains
neo/neo-os/image.neo and is never copied into build/. Metal bundles may
embed a built representation without creating another source image.

For each stage: implement its contract and first adapter together, add
focused failure and lifetime tests, run the relevant Make checks, rebuild
the Windows executable after worthwhile code changes, and record actual
support. Do not mark a stage complete from headers or build target names.
