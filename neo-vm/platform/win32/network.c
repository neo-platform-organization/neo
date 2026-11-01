#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "internal.h"
#include "platform/network.h"
#include "platform/win32/tls.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

struct neo_network_socket {
    SOCKET descriptor;
    bool listener;
    bool pending;
    bool failed;
    bool secure;
    bool finished;
    uint8_t local_pin[32], peer_pin[32];
    neo_win32_tls *tls;
};

static neo_status neo_network_start(void) {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? NEO_OK : NEO_IO_ERROR;
}

static bool neo_network_nonblocking(SOCKET descriptor) {
    u_long enabled = 1;
    return ioctlsocket(descriptor, (long)FIONBIO, &enabled) == 0 &&
        SetHandleInformation((HANDLE)descriptor, HANDLE_FLAG_INHERIT, 0) != 0;
}

static void neo_network_close(neo_network_socket *socket) {
    if (socket == NULL) { return; }
    neo_win32_tls_destroy(socket->tls);
    (void)closesocket(socket->descriptor);
    free(socket);
    (void)WSACleanup();
}

static neo_status neo_network_address(const char *text, uint16_t port,
                                       struct sockaddr_storage *out, int *length) {
    if (text == NULL || text[0] == '\0') { return NEO_INVALID; }
    *out = (struct sockaddr_storage){0};
    struct sockaddr_in *v4 = (struct sockaddr_in *)out;
    if (InetPtonA(AF_INET, text, &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET; v4->sin_port = htons(port);
        *length = (int)sizeof(*v4); return NEO_OK;
    }
    struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)out;
    if (InetPtonA(AF_INET6, text, &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6; v6->sin6_port = htons(port);
        *length = (int)sizeof(*v6); return NEO_OK;
    }
    return NEO_INVALID;
}

static neo_status neo_network_open(const char *address, uint16_t port, bool listening,
                                    neo_network_socket **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (!listening && port == 0) { return NEO_INVALID; }
    neo_status status = neo_network_start();
    if (status != NEO_OK) { return status; }
    struct sockaddr_storage endpoint;
    int length = 0;
    status = neo_network_address(address, port, &endpoint, &length);
    if (status != NEO_OK) { (void)WSACleanup(); return status; }
    SOCKET descriptor = socket(endpoint.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (descriptor == INVALID_SOCKET) { (void)WSACleanup(); return NEO_IO_ERROR; }
    neo_network_socket *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
        (void)closesocket(descriptor); (void)WSACleanup(); return NEO_OUT_OF_MEMORY;
    }
    handle->descriptor = descriptor; handle->listener = listening;
    if (!neo_network_nonblocking(descriptor)) { status = NEO_IO_ERROR; }
    if (status == NEO_OK && listening) {
        BOOL exclusive = TRUE;
        if (setsockopt(descriptor, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                       (const char *)&exclusive, (int)sizeof(exclusive)) != 0 ||
            bind(descriptor, (struct sockaddr *)&endpoint, length) != 0 ||
            listen(descriptor, SOMAXCONN) != 0) { status = NEO_IO_ERROR; }
    } else if (status == NEO_OK && connect(descriptor, (struct sockaddr *)&endpoint, length) != 0) {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS) { handle->pending = true; }
        else { status = NEO_IO_ERROR; }
    }
    if (status != NEO_OK) { neo_network_close(handle); return status; }
    *out = handle;
    return NEO_OK;
}

static neo_status neo_network_listen(const char *address, uint16_t port, neo_network_socket **out) {
    return neo_network_open(address, port, true, out);
}
static neo_status neo_network_connect(const char *address, uint16_t port, neo_network_socket **out) {
    return neo_network_open(address, port, false, out);
}

static neo_status neo_network_ready(neo_network_socket *socket, bool *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = false;
    if (socket == NULL || socket->listener) { return NEO_INVALID; }
    if (socket->failed) { return NEO_IO_ERROR; }
    if (!socket->pending) {
        if (socket->tls == NULL) { *out = true; return NEO_OK; }
        return neo_win32_tls_step(socket->tls, out);
    }
    fd_set writable, errors;
    FD_ZERO(&writable); FD_ZERO(&errors);
    FD_SET(socket->descriptor, &writable); FD_SET(socket->descriptor, &errors);
    struct timeval timeout = {0};
    int count = select(0, NULL, &writable, &errors, &timeout);
    if (count == SOCKET_ERROR) { socket->failed = true; return NEO_IO_ERROR; }
    if (count == 0) { return NEO_OK; }
    int error = 0, size = (int)sizeof(error);
    if (getsockopt(socket->descriptor, SOL_SOCKET, SO_ERROR, (char *)&error, &size) != 0 ||
        error != 0 || FD_ISSET(socket->descriptor, &errors)) {
        socket->failed = true; return NEO_IO_ERROR;
    }
    socket->pending = false;
    if (socket->tls == NULL) { *out = true; return NEO_OK; }
    return neo_win32_tls_step(socket->tls, out);
}

static neo_status neo_network_accept(neo_network_socket *listener, neo_network_socket **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (listener == NULL || !listener->listener) { return NEO_INVALID; }
    SOCKET descriptor = accept(listener->descriptor, NULL, NULL);
    if (descriptor == INVALID_SOCKET) {
        return WSAGetLastError() == WSAEWOULDBLOCK ? NEO_OK : NEO_IO_ERROR;
    }
    neo_status status = neo_network_start();
    if (status != NEO_OK) { (void)closesocket(descriptor); return status; }
    neo_network_socket *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
        (void)closesocket(descriptor); (void)WSACleanup(); return NEO_OUT_OF_MEMORY;
    }
    handle->descriptor = descriptor;
    if (!neo_network_nonblocking(descriptor)) { neo_network_close(handle); return NEO_IO_ERROR; }
    if (listener->secure) {
        status = neo_win32_tls_create((uintptr_t)descriptor, true, listener->local_pin, listener->peer_pin, &handle->tls);
        if (status != NEO_OK) { neo_network_close(handle); return status; }
        handle->secure = true;
    }
    *out = handle;
    return NEO_OK;
}

static neo_status neo_network_local_port(neo_network_socket *socket, uint16_t *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = 0;
    if (socket == NULL) { return NEO_INVALID; }
    struct sockaddr_storage address;
    int length = (int)sizeof(address);
    if (getsockname(socket->descriptor, (struct sockaddr *)&address, &length) != 0) { return NEO_IO_ERROR; }
    *out = address.ss_family == AF_INET ? ntohs(((struct sockaddr_in *)&address)->sin_port) :
        ntohs(((struct sockaddr_in6 *)&address)->sin6_port);
    return NEO_OK;
}

static neo_status neo_network_read(void *context, uint8_t *bytes, size_t capacity, neo_io_result *out) {
    neo_network_socket *socket = context;
    if (socket->tls != NULL) { return neo_win32_tls_read(socket->tls, bytes, capacity, out); }
    int amount = capacity > INT_MAX ? INT_MAX : (int)capacity;
    int received = recv(socket->descriptor, (char *)bytes, amount, 0);
    if (received > 0) { *out = (neo_io_result){NEO_IO_TRANSFERRED, (size_t)received}; return NEO_OK; }
    if (received == 0) { *out = (neo_io_result){NEO_IO_EOF, 0}; return NEO_OK; }
    if (WSAGetLastError() == WSAEWOULDBLOCK) {
        *out = (neo_io_result){NEO_IO_WOULD_BLOCK, 0}; return NEO_OK;
    }
    return NEO_IO_ERROR;
}
static neo_status neo_network_write(void *context, const uint8_t *bytes, size_t count, neo_io_result *out) {
    neo_network_socket *socket = context;
    if (socket->finished) { return NEO_DENIED; }
    if (socket->tls != NULL) { return neo_win32_tls_write(socket->tls, bytes, count, out); }
    int amount = count > INT_MAX ? INT_MAX : (int)count;
    int sent = send(socket->descriptor, (const char *)bytes, amount, 0);
    if (sent > 0) { *out = (neo_io_result){NEO_IO_TRANSFERRED, (size_t)sent}; return NEO_OK; }
    if (sent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
        *out = (neo_io_result){NEO_IO_WOULD_BLOCK, 0}; return NEO_OK;
    }
    return NEO_IO_ERROR;
}
static void neo_network_destroy(void *context) { neo_network_close(context); }

static neo_status neo_network_attach(neo_network_socket *socket, neo_vm *vm,
                                      const neo_capability *parent, const char *name,
                                      const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    bool ready = false;
    neo_status status = neo_network_ready(socket, &ready);
    if (status != NEO_OK) { return status; }
    if (!ready) { return NEO_BUSY; }
    const neo_stream_backend backend = {neo_network_read, neo_network_write, neo_network_destroy};
    return neo_stream_create(vm, parent, name, &backend, socket, out);
}

static neo_status neo_network_tls_configure(neo_network_socket *socket, const uint8_t local[32], const uint8_t peer[32]) {
    if (socket == NULL || socket->secure) { return NEO_INVALID; }
    neo_status status = neo_win32_tls_identity(local);
    if (status != NEO_OK) { return status; }
    if (!socket->listener) {
        status = neo_win32_tls_create((uintptr_t)socket->descriptor, false, local, peer, &socket->tls);
        if (status != NEO_OK) { return status; }
    }
    socket->secure = true;
    memcpy(socket->local_pin, local, 32); memcpy(socket->peer_pin, peer, 32);
    return NEO_OK;
}
static neo_status neo_network_peer(neo_vm *vm, const neo_capability *stream, uint8_t out[32]) {
    void *context = NULL;
    neo_status status = neo_stream_context(vm, stream, NEO_READ, neo_network_destroy, &context);
    if (status != NEO_OK) { return status; }
    neo_network_socket *socket = context;
    return socket->tls == NULL ? NEO_UNAVAILABLE : neo_win32_tls_peer(socket->tls, out);
}

static neo_status neo_network_finish_stream(neo_vm *vm, const neo_capability *stream, bool *out) {
    *out = false;
    void *context = NULL;
    neo_status status = neo_stream_context(vm, stream, NEO_WRITE, neo_network_destroy, &context);
    if (status != NEO_OK) { return status; }
    neo_network_socket *socket = context;
    if (socket->finished) { *out = true; return NEO_OK; }
    if (socket->tls != NULL) { status = neo_win32_tls_finish(socket->tls, out); }
    else if (shutdown(socket->descriptor, SD_SEND) != 0) { status = NEO_IO_ERROR; }
    else { *out = true; }
    if (status == NEO_OK && *out) { socket->finished = true; }
    return status;
}
const neo_network_backend *neo_win32_network_backend(void) {
    static const neo_network_backend backend = {
        .listen=neo_network_listen,.connect=neo_network_connect,.ready=neo_network_ready,
        .accept=neo_network_accept,.local_port=neo_network_local_port,.attach=neo_network_attach,.close=neo_network_close,
        .tls_identity=neo_win32_tls_identity,.tls_configure=neo_network_tls_configure,.authenticated_peer=neo_network_peer,
        .finish=neo_network_finish_stream
    };
    return &backend;
}
