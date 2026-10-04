#define _POSIX_C_SOURCE 200809L
#include "platform/posix/streams.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef struct neo_posix_stream { int fd; } neo_posix_stream;

static neo_status neo_posix_ready(int fd, short events, bool *ready) {
    struct pollfd entry = {.fd = fd, .events = events};
    int result = poll(&entry, 1, 0);
    *ready = false;
    if (result < 0) { return errno == EINTR ? NEO_OK : NEO_IO_ERROR; }
    if ((entry.revents & POLLNVAL) != 0) { return NEO_IO_ERROR; }
    /* HUP/ERR must reach read/write to distinguish EOF from a broken output. */
    *ready = result > 0;
    return NEO_OK;
}

static neo_status neo_posix_result(ssize_t result, int error, bool writing, neo_io_result *out) {
    if (result > 0) {
        *out = (neo_io_result){NEO_IO_TRANSFERRED, (size_t)result};
    } else if (result == 0 && !writing) {
        *out = (neo_io_result){NEO_IO_EOF, 0};
    } else if ((result < 0 && (error == EAGAIN || error == EWOULDBLOCK || error == EINTR)) || result == 0) {
        *out = (neo_io_result){NEO_IO_WOULD_BLOCK, 0};
    } else { return NEO_IO_ERROR; }
    return NEO_OK;
}

static neo_status neo_posix_read(void *context, uint8_t *bytes, size_t count, neo_io_result *out) {
    neo_posix_stream *stream = context;
    *out = (neo_io_result){NEO_IO_WOULD_BLOCK, 0};
    bool ready;
    neo_status status = neo_posix_ready(stream->fd, POLLIN, &ready);
    if (status != NEO_OK || !ready) { return status; }
    if (count > 4096) { count = 4096; }
    ssize_t result = read(stream->fd, bytes, count);
    return neo_posix_result(result, errno, false, out);
}

static neo_status neo_posix_write(void *context, const uint8_t *bytes, size_t count, neo_io_result *out) {
    neo_posix_stream *stream = context;
    *out = (neo_io_result){NEO_IO_WOULD_BLOCK, 0};
    bool ready;
    neo_status status = neo_posix_ready(stream->fd, POLLOUT, &ready);
    if (status != NEO_OK || !ready) { return status; }
    if (count > 4096) { count = 4096; }
    /* A closed pipe is an I/O error, not permission to terminate the host.
     * Block only around this write and consume only a newly generated SIGPIPE. */
    sigset_t blocked, old, pending;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    if (sigprocmask(SIG_BLOCK, &blocked, &old) != 0) { return NEO_IO_ERROR; }
    if (sigpending(&pending) != 0) {
        (void)sigprocmask(SIG_SETMASK, &old, NULL);
        return NEO_IO_ERROR;
    }
    bool already_pending = sigismember(&pending, SIGPIPE) == 1;
    ssize_t result = write(stream->fd, bytes, count);
    int error = errno;
    if (result < 0 && error == EPIPE && !already_pending) {
        const struct timespec timeout = {0};
        while (sigtimedwait(&blocked, NULL, &timeout) < 0 && errno == EINTR) { }
    }
    (void)sigprocmask(SIG_SETMASK, &old, NULL);
    return neo_posix_result(result, error, true, out);
}

static void neo_posix_destroy(void *context) {
    neo_posix_stream *stream = context;
    (void)close(stream->fd);
    free(stream);
}

neo_status neo_stream_posix_create(neo_vm *vm, const neo_capability *parent, const char *name,
                                   int descriptor, bool readable, bool writable,
                                   const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (vm == NULL || descriptor < 0 || (!readable && !writable)) { return NEO_INVALID; }
    int flags = fcntl(descriptor, F_GETFL);
    if (flags < 0 || (readable && (flags & O_ACCMODE) == O_WRONLY) ||
        (writable && (flags & O_ACCMODE) == O_RDONLY)) { return NEO_DENIED; }
    neo_posix_stream *stream = malloc(sizeof(*stream));
    if (stream == NULL) { return NEO_OUT_OF_MEMORY; }
    stream->fd = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    if (stream->fd < 0) { free(stream); return NEO_IO_ERROR; }
    neo_stream_backend backend = {.read = readable ? neo_posix_read : NULL,
        .write = writable ? neo_posix_write : NULL, .destroy = neo_posix_destroy};
    neo_status status = neo_stream_create(vm, parent, name, &backend, stream, out);
    if (status != NEO_OK) { neo_posix_destroy(stream); }
    return status;
}
