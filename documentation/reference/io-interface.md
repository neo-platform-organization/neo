# Streams and normalized input

[VM documentation](../README.md)

CLI launch grants standard streams through connections named `stdin`, `stdout`,
and `stderr`. Input grants READ; output/error grant WRITE. GUI-only launch grants
no streams; combine `--cli --gui` to grant both stream and display resources.

[io.h](../../neo-vm/io/io.h) defines the portable byte-transfer backend. Partial
transfers return their byte count; EOF and would-block are distinct outcomes.
Blocking POSIX descriptors remain blocking. A blocking read during GUI behavior
can therefore stall the frame loop. The kernel does not change terminal modes.

`stream-read` returns a byte integer, or the text status `eof`/`would-block`.
`stream-write` returns a count, or `would-block`. The image must handle partial
writes. `target` is a granted connection name, never a native descriptor.

Normalized window events include pointer positions, buttons, keys, resize, close,
and explicit queue-overflow reports. `window-next-event` updates a snapshot;
`window-event` reads fields. The current snapshot/queue lives in native scaffolding,
not persistent graph objects. Full text input/IME and resumable I/O are pending.

See [the language reference](../../neo/documentation/runtime-format.md) for exact
primitive arguments, event fields, and failures.
