# Platform services

Initial Windows and headless implementation, 2026-11-21. This is the
implemented P1–P6 foundation from the [platform plan](../development/platform-plan.md).
It supplies transport and resource mechanisms. It does not implement
desktop applications, a filesystem schema, or ether replication.

## Run and verify

From the repository root, using MSYS2 UCRT64 GNU Make:

```text
mingw32-make test
build\x86_64\Windows\neo.exe platform
build\x86_64\Windows\neo.exe surface neo\neo-os\image.neo surface-exercise
```

The surface exercise fills the viewport with a color and changes it on
a left click. Its state, input response, and drawing instructions live
in the canonical image. The host creates a surface and forwards events.
Closing the host window ends the exercise. This is the first interaction
proof, not the planned desktop or theme.

## Contracts and lifetime

services.h defines portable statuses, typed generation handles, input,
frame leases, requests, completions, capabilities, and backend callbacks.
services.c owns common validation and lifetime. platform.h selects a
backend; Windows API types remain under platform/.

Each service instance has a distinct owner identity. Reusing a slot does
not reuse its generation. A handle from another instance, or a revoked
handle, cannot access the resource. Constructors and resource adoption
are trusted host APIs. A graph cannot reconstruct a grant from a source
literal. Current graph exposure is through explicit package-bound buffer
grants; stream, storage, and network handles are host-side interfaces for
subsequent image bindings.

Requests copy submitted write bytes and own read buffers. Accepted work
returns a nonzero request ID. A rejected submission starts no work.
There is at most one pending request per operation on a resource;
connection reads and writes can proceed independently. Reported buffers
remain valid until release. A completion is returned once. Shutdown
reports unconsumed results through its sink, cancels pending operations,
and releases storage. close is the convenience operation that discards
remaining results when the owner no longer wants them.

Time and deadlines use monotonic nanoseconds. Windows uses the performance
counter; the headless adapter owns controllable time. Waits pump I/O with
a bounded polling interval and a per-instance wake event. Secure entropy
uses the OS provider. Headless entropy reports unsupported, rather than
pretending deterministic test bytes are secure randomness.

Public errors carry portable status and operation/request context. Native
OS error-code diagnostics are not yet retained for every failure path.
There is no global current service instance. The existing native execution
worker remains a separate, single-live-session facility.

## Runtime memory and CLI

VM graph, service, and Lattice heap allocation goes through platform
allocation functions. Explicit alignment, zeroing, overflow rejection,
reallocation, and release are supported. Ordinary allocation/reallocation
uses 16-byte alignment; explicitly aligned allocations must be freed
through the platform function. Native code still uses the isolated
Windows worker's write-then-execute page publication and instruction-cache
flush. No writable/executable page mode was introduced.

Standard streams are granted handles with read or write rights. Transfers
report actual byte counts, EOF, partial results, and cancellation. Terminal
queries report whether the handle is a console and its dimensions when
available. Arguments and exit status still use the hosted C entry point.

The CLI line reader is a common stream consumer. It buffers partial
reads, handles a final line without newline, drains oversized commands,
and pumps graph events while waiting. Command grammar and printing remain
in trusted hosted control. Interactive terminal line editing and complete
Unicode terminal configuration are not implemented by this byte transport.

## Headless adapter

The same common code runs with memory streams, storage, surfaces,
injected events, and captured frame bytes. Scripts can delay transfers,
split reads, return errors, and close a peer without opening sockets.
Headless deadlines and wakeups require no wall-clock sleeps.

The test backend is selected by configuration, not a new OS directory.
It has no ambient filesystem, network, or cryptographic entropy access.

## Input and presentation

Surfaces expose RGBA8 bytes, straight alpha, sRGB channel interpretation,
top-left origin, dimensions, and explicit row stride. Windows copies and
converts these bytes for an opaque host window. The image is responsible
for compositing its final frame; transparent host windows are unsupported.

Acquisition gives a lease with its own generation. Present or abandon
ends it. Resize refuses to invalidate an active lease, and stale lease
copies cannot present a later frame. Captured output is independent of
the writable buffer. Native SDK handles never enter the graph.

Events distinguish physical keys, composed UTF-8 text, pointer motion,
buttons, fractional wheel movement, focus, resize, and close. Coordinates
are physical surface pixels; resize reports display scale separately.
Modifier bits are Shift=1, Control=2, Alt=4, Super=8. Initial physical key
IDs use PC scan positions with an extended bit, not translated character
codes. Later adapters must map their device codes into that vocabulary.
Text is checked for valid UTF-8. Windows supports UTF-16 surrogate-pair
conversion and per-monitor DPI changes. IME preedit, touch, and pen remain
unsupported. Input queue overflow is reported.

The surface host invokes this small image protocol:

```text
configure(widthPixels, heightPixels, scaleTimes1000)
pointer(x, y, modifiers)
button(code, pressed, x, y)
wheel(horizontalTimes120, verticalTimes120, x, y)
key(code, pressed, modifiers)
focus(focused)
text(utf8Text)
```

The native service retains fractional wheel values as doubles. The first
graph bridge uses integer 1/120-detent units because its handler arguments
are int32; finer precision is not yet carried through this bridge.

Graph primitives are `(buffer-fill pixelOffset pixelCount packedRGBA)`
and `(buffer-present)`. Packed color uses R in the high byte and A in
the low byte. The JIT stages commands; the broker validates the full
batch and package grant before changing the buffer. Overflow, bad ranges,
or denied authority discard the send's state writes and buffer commands.
Presentation must be the last command in a batch. Buffer publication,
lifecycle changes, and queued messages currently require separate sends.
Eight staged commands per send remain the limit.

## Storage

The lower boundary is a managed key/opaque-byte store. The image can
represent directories, names, headers, and associations in its graph
without mirroring the host filesystem. Keys are single lowercase ASCII
identifiers of at most 63 bytes, using letters, digits, dot, hyphen, or
underscore. This avoids host-dependent case aliases; displayed filenames
and directory structure belong in the image. A trusted host grants an existing managed directory;
graphs do not submit host paths. Listings enumerate live keys. Reads
provide byte length and a provider revision. Read, replacement, and deletion
have asynchronous request forms. Listing is currently a synchronous host
operation. Both paths share the same replacement protocol.

Windows serializes cooperating writers with exclusive per-key lock
handles. A replacement checks the expected revision while holding that
lock, writes a complete private record to a fresh temporary file, flushes
it, then replaces the previous record. Revision metadata is private
adapter data, not the image's document format. Delete writes a tombstone
with a new revision, preventing stale creation after deletion.

This protocol requires an exclusively managed, trusted host namespace.
It does not protect against arbitrary external writers or hostile
mutation of the directory's ancestor paths. Final reparse entries and
path traversal are rejected. This is not yet a general handle-relative
sandboxed host filesystem. Power-loss durability of directory publication
is not claimed. A read-only grant still requires access to the store's
management lock files; arbitrary read-only host directories are not this
store service.

Before submission, cancellation prevents work. After an external write
starts, cancellation, timeout, or revocation reports uncertain if the
effect cannot be ruled out. The previous or new complete record may
exist; clients must read its revision before deciding what to do next.
The adapter does not retry such writes automatically. Cancellation joins
the worker before releasing buffers; a stalled OS storage operation can
therefore delay teardown. A strict device-I/O termination bound is not
provided by this first implementation.

## Networking

The first network grant is explicitly IPv4 loopback. It supports TCP
listeners, asynchronous accept, nonblocking connections, independent
read/write requests, UDP, backpressure, cancellation, deadlines, and peer
closure. The constructor creates a connection handle; its first I/O
request also observes connection establishment or failure. A zero-length
write can be used to await readiness without application payload.

Datagram truncation has a distinct status. Transfer counts do not imply
that a peer application processed the bytes. Cancellation and graph
rollback cannot undo bytes already sent. There are no raw sockets,
external addresses, DNS, IPv6, TLS, or authenticated peer identities yet.
Loopback is transport reachability, not authentication or permission to
modify the image. Ether exposure and replication remain image policy.

## Budgets and verification

Initial limits are 64 resources, 32 outstanding/unreleased requests,
128 queued input events, 64 KiB per byte transfer or stored value, and
4096 pixels per surface axis. Common managed buffers have a 64 MiB
budget; backend presentation copies, transient staging, and private
metadata are separately bounded allocations, not included in that figure.
The headless store retains 32 keys including tombstones.

Make checks stale and foreign handles, saturation, cancellation races,
deadlines, wakeups, EOF, partial I/O, Unicode conversion, channel conversion,
stale frames, and shutdown. Competing Windows storage requests prove one
conditional writer wins. Interrupted replacement permits only a complete
old or new record. Two independently granted image sessions exchange a
graph value over TCP. The canonical control produces identical frame
bytes on headless and hidden Windows surfaces, with buffer rollback and
revocation checks. The existing language suite also passes.

Linux now implements hosted services, including X11, GLX/software presentation, ALSA audio, and UTF-8 clipboard. Windows provides OpenGL/software, WinMM audio, and clipboard. Standalone x86_64 boots via UEFI with trusted integer JIT execution, framebuffer, timer, PS/2 input, and serial CLI. It lacks worker isolation, binary64, persistent storage, network, USB, and audio drivers. See the build guide. Drag/drop, hotplug, and broader device recovery remain open.
