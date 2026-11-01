#ifndef NEO_NETWORK_H
#define NEO_NETWORK_H

#include "io/io.h"

typedef struct neo_network_socket neo_network_socket;
/** Trusted bootstrap transport interface. Handles are host-owned until attached
 * as a VM stream; close each unattached handle. Calls are sequential. Numeric
 * IPv4/IPv6 addresses only: no hidden DNS, discovery, authentication or framing.
 * Pending operations return OK with ready=false; no call waits for a peer.
 * Opening a connection conveys no image identity or graph authority. */
typedef struct neo_network_backend {
    neo_status (*listen)(const char *address, uint16_t port, neo_network_socket **out);
    neo_status (*connect)(const char *address, uint16_t port, neo_network_socket **out);
    neo_status (*ready)(neo_network_socket *socket, bool *out);
    neo_status (*accept)(neo_network_socket *listener, neo_network_socket **out);
    neo_status (*local_port)(neo_network_socket *socket, uint16_t *out);
    /** Success transfers ownership to the VM; failure leaves handle host-owned.
     * Requires INSERT on parent. Only connected, ready sockets may attach.
     * Use ordinary stream READ/WRITE grants afterwards. */
    neo_status (*attach)(neo_network_socket *socket, neo_vm *vm,
                         const neo_capability *parent, const char *name,
                         const neo_capability **out);
    void (*close)(neo_network_socket *socket);
    /** Optional pinned mutual TLS provider. Configure before polling/open use;
     * no fallback to plaintext on failure. Identity is in CurrentUser/MY on Windows. */
    neo_status (*tls_identity)(const uint8_t local[32]);
    neo_status (*tls_configure)(neo_network_socket *socket, const uint8_t local[32], const uint8_t peer[32]);
    neo_status (*authenticated_peer)(neo_vm *vm, const neo_capability *stream, uint8_t out[32]);
    /** Optional nonblocking write shutdown. WRITE required; true means locally
     * flushed shutdown, never peer receipt or application acceptance. */
    neo_status (*finish)(neo_vm *vm, const neo_capability *stream, bool *out);
} neo_network_backend;

/** Windows host provider. No socket or listener is opened by this factory. */
const neo_network_backend *neo_win32_network_backend(void);
/** Host-only immutable endpoint grant. Does not open a socket. Exact numeric
 * address/port and listen-vs-connect cannot be changed by image payload edits.
 * Grant ACT to control this endpoint; streams retain separate READ/WRITE rights. */
neo_status neo_network_endpoint_create(neo_vm *vm, const neo_capability *parent,
    const char *name, const char *address, uint16_t port, bool listening,
    const neo_capability **out);
neo_status neo_network_endpoint_open(neo_vm *vm, const neo_capability *endpoint);
neo_status neo_network_endpoint_ready(neo_vm *vm, const neo_capability *endpoint, bool *out);
neo_status neo_network_endpoint_port(neo_vm *vm, const neo_capability *endpoint, uint16_t *out);
neo_status neo_network_endpoint_close(neo_vm *vm, const neo_capability *endpoint);
/** Listener: accept one peer. Connector: publish its established connection.
 * OK with NULL means connecting/authenticating. One accepted peer is retained
 * while pending or publication fails, as is a connector socket; retry safely or
 * close the endpoint to discard it. INSERT required on destination. */
neo_status neo_network_endpoint_stream(neo_vm *vm, const neo_capability *endpoint,
    const neo_capability *destination, const char *name, const neo_capability **out);
/** Host-only pinned mutual TLS grant. Fingerprints are exactly 64 hex digits.
 * Local certificate/private key must be provisioned outside the image. */
neo_status neo_network_endpoint_secure_create(neo_vm *vm, const neo_capability *parent,
    const char *name, const char *address, uint16_t port, bool listening,
    const char *local_sha256, const char *peer_sha256, const neo_capability **out);
/** READ-protected verified immediate peer fingerprint; empty on failure.
 * Plain streams return UNAVAILABLE, never an origin-label assertion. */
neo_status neo_network_authenticated_peer(neo_vm *vm, const neo_capability *stream, char out[65]);
neo_status neo_network_finish(neo_vm *vm, const neo_capability *stream, bool *out);
#endif
