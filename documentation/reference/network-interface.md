# Networking transport

[VM documentation](../README.md) · [Platform](platform-interface.md)

The first networking layer is an optional hosted transport provider. The Windows
x86-64 backend supplies nonblocking TCP over numeric IPv4 and IPv6 addresses.
Linux currently returns `NEO_UNSUPPORTED` when this provider is requested. This
is the transport foundation; explicit snapshot transfer is now available through the [replication substrate](replication-interface.md).
Integration with local ETHER messaging and continuing updates remains pending.

Trusted bootstrap requests `NEO_PLATFORM_NETWORK` in
`neo_platform_native_services`, installs the resulting platform services, and
calls `neo_platform_network(vm, &backend)`. Provider installation opens no socket.
An unconfigured VM returns `NEO_UNAVAILABLE`; a disabled or absent provider
returns `NEO_UNSUPPORTED`. A descriptor flag alone supplies no implementation.

The interface is in `neo-vm/platform/network.h`. Host bootstrap can:

1. `listen(address, port, &listener)`: explicitly bind a numeric address. Port 0
   selects an ephemeral port, available via `local_port`.
2. `connect(address, port, &connection)`: begin a nonblocking connection.
3. `ready(connection, &ready)`: poll establishment without waiting. A terminal
   establishment failure remains failed on subsequent polls.
4. `accept(listener, &connection)`: attempt one accept; OK with NULL means pending.
5. `attach(connection, vm, parent, name, &stream)`: publish a ready connection as
   an ordinary VM byte stream. Parent INSERT is required. Success transfers handle
   ownership to the VM; failure leaves it with the host. Attaching a pending
   connection returns `NEO_BUSY`.
6. `close(handle)`: release an unattached listener or connection. Never close or
   reuse a handle after successful attach. Close all remaining host handles.

Attached streams require READ/WRITE authority for the corresponding operations.
Connections to image actors still need explicit grants. Deletion or image unload
closes an attached socket. Existing runtime-resource rules reject copying or
serializing a live stream as portable graph state. Reopening after a checkpoint
must be an explicit bootstrap/application action.

Reads/writes perform one nonblocking attempt, report partial byte counts and
would-block, and distinguish read EOF from failure. Callers keep offsets and
retry through scheduling; there is no hidden send-all loop or message boundary.
All operations are sequential. Raw OS socket values stay inside the adapter.
Host socket bookkeeping uses host allocation; attached resource wrappers use the
VM allocator. Failed publication retains host ownership for retry or cleanup.

Numeric endpoints avoid a blocking resolver in the evaluator. DNS, scoped IPv6
addresses, datagrams, half-close, integrated readiness waiting, broader peer policies and connection-broader endpoint policies remain future work.
Transport addresses do not authenticate a neo image. Receiving bytes neither
parses them nor executes behavior, updates a graph, or grants authority.

The intended higher layer publishes selected graph material into ETHER as
snapshots, continuing updates, or both. Applications choose authority, merging,
update acceptance and synchronization direction. Delivery must remain distinct
from granting permission or accepting a remote change. Existing local ETHER
message behavior is unchanged by this provider.

Windows `neo platform` reports `network = true` when the provider is available.
The ordinary live/GUI launch paths do not open listeners or grant network access.
`neo selftest` performs a bounded IPv4 loopback check using an ephemeral port:
connect/accept, empty-read would-block, binary bytes including NUL and 255,
partial-transfer accounting, EOF, denied insertion/write, and allocation-failure
retry followed by VM-owned cleanup. The IPv6 path is implemented but not covered
by that loopback test. Running this test requires local socket permission.

## Image endpoint grants

Host bootstrap can now create an immutable endpoint resource with
`neo_network_endpoint_create`. It stores one address, port and direction in
protected native state; payload edits cannot broaden that scope. Creation opens
no socket. The endpoint is a live resource, rejected by graph copy and persistence
including when closed. Delete it before saving; regrant explicitly after loading.

The launcher accepts one explicit `--connect ADDRESS PORT` or
`--listen ADDRESS PORT` before the image filename. It creates `host-network` at the
root and grants the entry actor a `network` connection with READ and ACT. Port 0
is permitted for listeners. Neither flag is enabled by an image's source. Linux
continues to reject the networking provider as unsupported.

```text
neo --connect 127.0.0.1 9000 neo/fixtures/network-client.neo
```

Image operations use a connection name as `target`:

| Operation | Result |
| --- | --- |
| `(network-open (target "network"))` | Opens the granted endpoint once; already open returns busy. |
| `(network-ready (target "network"))` | Connector establishment status. For a listener, true means listening, not that a peer is waiting. |
| `(network-port (target "network"))` | Bound local port, including an ephemeral port. |
| `(network-stream (target "network") (name "peer"))` | Publishes a stream as the receiver's child; false when pending, true on success. Requires receiver INSERT. |
| `(network-close (target "network"))` | Closes the endpoint; idempotent. Already published streams remain independent. |

All endpoint controls require ACT. An unopened endpoint reports unavailable for
poll/port/publication. Closing and reopening reuses the original immutable grant.
A connector transfers its established socket into one published stream and is
then unopened again. A listener can publish many independent accepted streams.
Explicitly connect to the resulting child with READ/WRITE rights before using
`stream-read` or `stream-write`. Publication checks receiver handler protection
through the same activation guard used for graph mutation. A destination name
conflict is detected before attempting accept. Failed stream allocation retains the accepted/connector socket for retry;
endpoint close discards pending sockets.

The fixture demonstrates connection polling and partial-write offsets. Its loops
are execution-budget bounded; production image services should retry pending
operations on subsequent scheduled turns. The [snapshot layer](replication-interface.md) adds explicit replication above
these byte streams; received material is never automatically executed.

## Live operator grants and scheduled sessions

The Windows live operator supports `net-grant OWNER listen|connect ADDRESS PORT`.
It creates a fixed endpoint grant without opening it; the image still calls
network-open. This is host provisioning, unavailable as an image primitive.
[Session policy](../../neo/packages/session.md) now handles bounded ticks, explicit
or optional automatic retry/backoff, and fresh snapshots after reconnect.

## Authenticated transport

[Pinned mutual TLS](authenticated-networking.md) is now available on Windows via
`net-grant-tls` and `neo_network_endpoint_secure_create`. Secure streams publish
only after authentication; `authenticated-peer` queries verified immediate-peer
identity. Plain TCP remains explicitly selectable. Liveness/acknowledgements are
separate work; Linux stays stubbed.


## Explicit local write shutdown

`(network-finish (target STREAM_REFERENCE))` requires WRITE on a native network
stream and returns Boolean: false means poll again, true means local write
shutdown completed. This takes a stream reference, unlike endpoint control's
connection-name operand. Repeated completed calls succeed. Nonempty writes are
then denied; reads and explicit deletion remain available. TCP uses SD_SEND;
TLS first generates and flushes close-notify. Deletion/endpoint cancellation never
implicitly runs this protocol. A pending TLS application write returns NEO_BUSY
until its original writer completes it; do not destroy or replace its bytes.
No receipt or acceptance acknowledgement is implied. Backends may omit optional
finish support and return unsupported. Linux remains stubbed.

[Session 0.4.0](../../neo/packages/session.md#graceful-local-finish-040) implements
publisher draining and optional poll budgets using this primitive. Candidate and
accepted-state handling stays in the image.


Snapshot receive now records EOF between frames separately from truncation;
`replica-ended` queries that observation. Session 0.5.0 uses it to finish reciprocal
shutdown and stop a subscriber. A pending candidate defers this read until the
application explicitly decides it. TLS EOF requires close-notify; raw TCP FIN
alone is not an authenticated assertion. Publishers do not poll idle input yet.
