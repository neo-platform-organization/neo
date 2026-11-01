#include "internal.h"
#include "platform/network.h"
#include <string.h>

typedef struct neo_network_endpoint {
    neo_vm *vm;
    const neo_network_backend *backend;
    neo_network_socket *socket;
    neo_network_socket *accepted;
    bool secure;
    uint8_t local_pin[32], peer_pin[32];
    char address[64];
    uint16_t port;
    bool listening;
} neo_network_endpoint;

static void neo_endpoint_destroy(void *context) {
    neo_network_endpoint *endpoint = context;
    endpoint->backend->close(endpoint->accepted);
    endpoint->backend->close(endpoint->socket);
    neo_free(endpoint->vm, endpoint);
}
static neo_status neo_endpoint_read(void *context, uint8_t *bytes, size_t count, neo_io_result *out) {
    (void)context; (void)bytes; (void)count; (void)out;
    return NEO_UNSUPPORTED;
}
static neo_status neo_endpoint_get(neo_vm *vm, const neo_capability *cap, unsigned rights,
                                   neo_network_endpoint **out) {
    void *context = NULL;
    neo_status status = neo_stream_context(vm, cap, rights, neo_endpoint_destroy, &context);
    *out = context;
    return status;
}
static neo_status neo_endpoint_create(neo_vm *vm, const neo_capability *parent,
    const char *name, const char *address, uint16_t port, bool listening,
    const uint8_t *local, const uint8_t *peer, const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (address == NULL || address[0] == '\0' || strlen(address) >= 64 || (!listening && port == 0)) {
        return NEO_INVALID;
    }
    const neo_network_backend *backend = NULL;
    neo_status status = neo_platform_network(vm, &backend);
    if (status != NEO_OK) { return status; }
    if (local != NULL) {
        if (backend->tls_identity == NULL || backend->tls_configure == NULL) { return NEO_UNSUPPORTED; }
        status = backend->tls_identity(local);
        if (status != NEO_OK) { return status; }
    }
    neo_network_endpoint *endpoint = neo_alloc(vm, sizeof(*endpoint));
    if (endpoint == NULL) { return NEO_OUT_OF_MEMORY; }
    *endpoint = (neo_network_endpoint){.vm=vm,.backend=backend,.port=port,.listening=listening};
    memcpy(endpoint->address, address, strlen(address) + 1);
    if (local != NULL) {
        endpoint->secure = true;
        memcpy(endpoint->local_pin, local, 32); memcpy(endpoint->peer_pin, peer, 32);
    }
    const neo_stream_backend resource = {.read=neo_endpoint_read,.destroy=neo_endpoint_destroy};
    status = neo_stream_create(vm, parent, name, &resource, endpoint, out);
    if (status != NEO_OK) { neo_free(vm, endpoint); }
    return status;
}
neo_status neo_network_endpoint_create(neo_vm *vm, const neo_capability *parent,
    const char *name, const char *address, uint16_t port, bool listening, const neo_capability **out) {
    return neo_endpoint_create(vm, parent, name, address, port, listening, NULL, NULL, out);
}
static bool neo_fingerprint(const char *text, uint8_t out[32]) {
    if (text == NULL || strlen(text) != 64) { return false; }
    for (size_t i = 0; i < 32; ++i) {
        unsigned byte = 0;
        for (size_t j = 0; j < 2; ++j) {
            char c = text[i * 2 + j]; unsigned digit;
            if (c >= '0' && c <= '9') { digit = (unsigned)(c - '0'); }
            else if (c >= 'a' && c <= 'f') { digit = (unsigned)(c - 'a') + 10; }
            else if (c >= 'A' && c <= 'F') { digit = (unsigned)(c - 'A') + 10; }
            else { return false; }
            byte = byte * 16 + digit;
        }
        out[i] = (uint8_t)byte;
    }
    return true;
}
neo_status neo_network_endpoint_secure_create(neo_vm *vm, const neo_capability *parent,
    const char *name, const char *address, uint16_t port, bool listening,
    const char *local_sha256, const char *peer_sha256, const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    uint8_t local[32], peer[32];
    if (!neo_fingerprint(local_sha256, local) || !neo_fingerprint(peer_sha256, peer)) { return NEO_INVALID; }
    return neo_endpoint_create(vm, parent, name, address, port, listening, local, peer, out);
}
neo_status neo_network_authenticated_peer(neo_vm *vm, const neo_capability *stream, char out[65]) {
    if (out == NULL) { return NEO_INVALID; }
    out[0] = '\0';
    const neo_network_backend *backend = NULL;
    neo_status status = neo_platform_network(vm, &backend);
    if (status != NEO_OK) { return status; }
    if (backend->authenticated_peer == NULL) { return NEO_UNSUPPORTED; }
    uint8_t pin[32];
    status = backend->authenticated_peer(vm, stream, pin);
    if (status != NEO_OK) { return status; }
    const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) { out[i * 2] = hex[pin[i] >> 4]; out[i * 2 + 1] = hex[pin[i] & 15]; }
    out[64] = '\0'; return NEO_OK;
}
neo_status neo_network_finish(neo_vm *vm, const neo_capability *stream, bool *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = false;
    const neo_network_backend *backend = NULL;
    neo_status status = neo_platform_network(vm, &backend);
    if (status != NEO_OK) { return status; }
    return backend->finish == NULL ? NEO_UNSUPPORTED : backend->finish(vm, stream, out);
}
neo_status neo_network_endpoint_open(neo_vm *vm, const neo_capability *cap) {
    neo_network_endpoint *endpoint = NULL;
    neo_status status = neo_endpoint_get(vm, cap, NEO_ACT, &endpoint);
    if (status != NEO_OK) { return status; }
    if (endpoint->socket != NULL) { return NEO_BUSY; }
    status = endpoint->listening ? endpoint->backend->listen(endpoint->address, endpoint->port, &endpoint->socket) :
        endpoint->backend->connect(endpoint->address, endpoint->port, &endpoint->socket);
    if (status == NEO_OK && endpoint->secure) {
        status = endpoint->backend->tls_configure(endpoint->socket, endpoint->local_pin, endpoint->peer_pin);
        if (status != NEO_OK) { endpoint->backend->close(endpoint->socket); endpoint->socket = NULL; }
    }
    return status;
}
neo_status neo_network_endpoint_ready(neo_vm *vm, const neo_capability *cap, bool *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = false;
    neo_network_endpoint *endpoint = NULL;
    neo_status status = neo_endpoint_get(vm, cap, NEO_ACT, &endpoint);
    if (status != NEO_OK) { return status; }
    if (endpoint->socket == NULL) { return NEO_UNAVAILABLE; }
    if (endpoint->listening) { *out = true; return NEO_OK; }
    return endpoint->backend->ready(endpoint->socket, out);
}
neo_status neo_network_endpoint_port(neo_vm *vm, const neo_capability *cap, uint16_t *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = 0;
    neo_network_endpoint *endpoint = NULL;
    neo_status status = neo_endpoint_get(vm, cap, NEO_ACT, &endpoint);
    if (status != NEO_OK) { return status; }
    if (endpoint->socket == NULL) { return NEO_UNAVAILABLE; }
    return endpoint->backend->local_port(endpoint->socket, out);
}
neo_status neo_network_endpoint_close(neo_vm *vm, const neo_capability *cap) {
    neo_network_endpoint *endpoint = NULL;
    neo_status status = neo_endpoint_get(vm, cap, NEO_ACT, &endpoint);
    if (status != NEO_OK) { return status; }
    endpoint->backend->close(endpoint->accepted); endpoint->accepted = NULL;
    endpoint->backend->close(endpoint->socket);
    endpoint->socket = NULL;
    return NEO_OK;
}
neo_status neo_network_endpoint_stream(neo_vm *vm, const neo_capability *cap,
    const neo_capability *destination, const char *name, const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (name == NULL || name[0] == '\0') { return NEO_INVALID; }
    neo_network_endpoint *endpoint = NULL;
    neo_status status = neo_endpoint_get(vm, cap, NEO_ACT, &endpoint);
    if (status != NEO_OK) { return status; }
    neo_object *container = NULL;
    status = neo_resolve(vm, destination, NEO_INSERT, &container);
    if (status != NEO_OK) { return status; }
    if (neo_child_named(vm, container->id, name) != NULL) { return NEO_CONFLICT; }
    if (endpoint->socket == NULL) { return NEO_UNAVAILABLE; }
    neo_network_socket *socket = endpoint->socket;
    if (endpoint->listening) {
        if (endpoint->accepted == NULL) {
            status = endpoint->backend->accept(endpoint->socket, &endpoint->accepted);
            if (status != NEO_OK || endpoint->accepted == NULL) { return status; }
        }
        socket = endpoint->accepted;
    }
    bool ready = false;
    status = endpoint->backend->ready(socket, &ready);
    if (status != NEO_OK) {
        if (endpoint->listening) { endpoint->backend->close(endpoint->accepted); endpoint->accepted = NULL; }
        return status;
    }
    if (!ready) { return NEO_OK; }
    status = endpoint->backend->attach(socket, vm, destination, name, out);
    if (endpoint->listening && status == NEO_OK) { endpoint->accepted = NULL; }
    if (!endpoint->listening && status == NEO_OK) { endpoint->socket = NULL; }
    return status;
}
