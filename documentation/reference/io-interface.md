# Streams and platform-independent input

[Documentation index](../README.md) · [Window interface](window-interface.md) · [Design](../design/io-and-display.md)

The image talks to granted stream, window, and buffer objects. It does not name POSIX descriptors, X11 handles, or graphics APIs. Native adapters implement the operations. This is the first polling interface, not yet asynchronous message-based I/O or graph-resident scheduling.

## Terminal example

From the repository root:

```sh
make
./build/neo --cli neo/terminal.neo terminal greet
./build/neo --cli neo/terminal.neo terminal greet > greeting.txt
```

The example writes `hello from neo` followed by a newline. `neo --cli IMAGE ACTOR HANDLER` loads an image and grants the selected actor `stdin` (READ), `stdout` (WRITE), and `stderr` (WRITE) connections. It invokes the handler once, with a one-million-step budget. Application output goes only to the granted streams; host diagnostics go to stderr. The handler's return value is not printed. Loading with the ordinary CLI grants no streams.

The [example image](../../neo/terminal.neo) tracks its byte offset and handles partial writes. It explicitly fails if output would block; a different object can choose a different policy. The standalone launcher has no automatic retry loop.

## Stream primitives

| Operation | Example | Result |
| --- | --- | --- |
| `stream-read` | `(stream-read (target "stdin"))` | Integer byte 0–255, text `"eof"`, or text `"would-block"`. Requires READ. |
| `stream-write` | `(stream-write (target "stdout") (value "hello\n"))` | Integer byte count actually transferred, or text `"would-block"`. Requires WRITE. |
| `stream-write` with byte | `(stream-write (target "stdout") (value 0))` | Transfers one byte, including NUL; value must be an integer from 0 through 255. |
| Offset | `(stream-write (target "stdout") (value "hello") (offset 2))` | Attempts the remaining bytes starting at byte offset 2. Offset defaults to zero. |

Counts and offsets are bytes, not Unicode characters. Text output uses the string's stored bytes; no encoding conversion or newline translation is added by neo. Terminal driver behavior remains in effect. Reading NUL returns integer zero, not EOF. EOF and would-block are normal results; backend errors return execution failures. No transfer is attempted for a zero-length output, which returns zero.

Input consumption and successful writes are observable effects. Retrying an entire failed handler may repeat earlier output. Advance by the reported transfer count; a short write is not completion. The VM allocates possible text results before calling the backend so a result-allocation failure does not consume a byte unseen.

The scalar result convention is provisional until composite result objects and asynchronous activations are implemented. It is intentionally unambiguous: stream data/results are integers; the two exceptional normal states are text.

## Host stream API

[neo_io.h](../../neo-vm/include/neo_io.h) defines the platform-independent API. A backend supplies optional read/write callbacks and a mandatory destroy callback. `neo_stream_create` copies the table and takes ownership of its context only on success. Callbacks borrow buffers only for the duration of the call. They return a transfer count, EOF, or would-block through `neo_io_result`; other failures use `neo_status`.

`neo_stream_read` and `neo_stream_write` enforce capabilities and validate result counts. A backend with no read or write callback reports unsupported for that direction. Zero-length requests still validate authority and endpoint kind. Deletion, image unload, and VM destruction release the backend once; stale capabilities report unavailable. Streams prevent copying/moving/serializing their containing region, just like windows and live pixel buffers.

[neo_io_posix.h](../../neo-vm/include/neo_io_posix.h) is the native adapter, linked only into the terminal runner and its tests. It duplicates explicitly granted descriptors with close-on-exec. It never changes descriptor status flags or terminal modes, and never closes the original descriptor. Duplicates share the underlying stream position and status flags; this is not an independent copy of the underlying input.

The POSIX adapter performs a zero-timeout readiness check, followed by at most one transfer of up to 4096 bytes. Interrupted calls and unavailable data return would-block. Closed output pipes return an I/O failure rather than terminating the process with SIGPIPE; signal masking is scoped to the write. This adapter is for the current single-threaded runtime.

**Blocking limitation:** readiness is not a guarantee that a later operation cannot block. Scheduling-sensitive hosts must provide suitable nonblocking endpoints with exclusive I/O ownership; inherited blocking terminal/file endpoints are for the dedicated standalone runner. No general asynchronous disk I/O, terminal raw mode, terminal escape parsing, stream cancellation, or readiness-driven scheduler has been added. The behavior follows the POSIX [readiness](https://pubs.opengroup.org/onlinepubs/9799919799/functions/poll.html) and [write](https://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html) contracts.

## Surface properties and events

The existing RGBA buffer/window callback boundary remains platform-independent. A window is the current presentation surface; changing backends does not change the primitive names. CPU buffer dimensions and window dimensions are separate:

| Operation | Example | Result |
| --- | --- | --- |
| `buffer-width` | `(buffer-width (target "buffer"))` | Fixed buffer width in pixels; READ. |
| `buffer-height` | `(buffer-height (target "buffer"))` | Fixed buffer height in pixels; READ. |
| `window-next-event` | `(window-next-event (target "window"))` | Consumes one collected event and returns its type as text; READ + WRITE. `"none"` means empty. |
| `window-event` | `(window-event (target "window") (field "x"))` | Reads a field of the last consumed event; READ. Does not consume another event. |

Call `window-poll` to collect native events before consuming them. The window launcher already polls before invoking `frame`. The optional backend `next_event` callback translates native events into `neo_input_event`; a backend that lacks this callback reports unsupported, rather than pretending it has an empty queue.

The current snapshot is stored per window, not per caller. A successful next-event call replaces it, including when the result is none; failed calls preserve it. One event consumer per window is recommended until graph event queues are implemented.

| Field | Kind | Meaning |
| --- | --- | --- |
| `type` | Text | `none`, `close`, `resize`, `expose`, `focus`, `pointer`, `button`, `key`, or `overflow`. |
| `x`, `y` | Integer | Pointer/button coordinates relative to the client window. |
| `width`, `height` | Integer | Resize dimensions in pixels. |
| `pressed` | Boolean | Key/button down or focus gained; false denotes release or focus lost. |
| `button` | Integer | 1 primary, 2 middle, 3 secondary; X11 wheel steps currently appear as buttons 4/5 (vertical) or 6/7 (horizontal). Other buttons retain their logical number. |
| `key` | Text | Portable symbolic key name, or `"unknown"` for an unsupported mapping. |
| `lost` | Integer | Count of discarded events reported by overflow. |

Fields irrelevant to an event are zero, false, or empty text. Unknown field names fail. The X11 key subset maps lowercase letters, digits, enter, escape, tab, backspace, delete, space, arrow keys, shift, control, and alt. Key names use the unshifted symbol. They do not represent composed text. Repeated native key events remain repeated events.

## Queue and fallback limits

The X11 adapter holds up to 64 normalized events and polls at most 256 native events per call. On overflow, it preserves already queued events, counts discarded subsequent events, then returns one overflow event after the preserved queue is drained. It resumes enqueueing after that report is consumed. Consumers should resynchronize input state when overflow occurs. Close and size state still update even when the event queue overflows.

These queues and snapshots are native scaffolding, not yet graph-visible message objects. Unicode composition/IME, clipboard, full key mapping, touch, logical/DPI coordinates, and general input-device discovery remain future work. The display backend still presents RGBA pixels only. No GPU renderer, automatic graphics fallback, or platform-specific API is exposed in the image.

## Tests

`make test` includes real pipe EOF/would-block/broken-output behavior, fake short writes, neo invocation, capability denial, stale handles, ownership and allocation failures, buffer dimensions, event snapshots, and terminal output separation. `make test-window` opens a temporary window to verify X11 pixel transfer, resize, pointer/key translation, overflow reporting, and close handling. Sanitizer binaries `build/sanitize_io` and `build/sanitize_display` cover the headless paths; sandbox runs disable leak detection while fault tests check VM allocation balance.
