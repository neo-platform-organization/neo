#ifndef NEO_IO_H
#define NEO_IO_H

#include "objects/objects.h"

typedef enum neo_io_state { NEO_IO_TRANSFERRED, NEO_IO_EOF, NEO_IO_WOULD_BLOCK } neo_io_state;
typedef struct neo_io_result { neo_io_state state; size_t count; } neo_io_result;

/** Backend callbacks perform one attempt, never retry on behalf of the image.
 * Null read/write callbacks mean unsupported direction. Buffers are borrowed
 * only during the call. Context ownership transfers only on create success. */
typedef struct neo_stream_backend {
    neo_status (*read)(void *context, uint8_t *bytes, size_t capacity, neo_io_result *out);
    neo_status (*write)(void *context, const uint8_t *bytes, size_t count, neo_io_result *out);
    void (*destroy)(void *context);
} neo_stream_backend;

neo_status neo_stream_create(neo_vm *vm, const neo_capability *parent, const char *name,
                             const neo_stream_backend *backend, void *context,
                             const neo_capability **out);
neo_status neo_stream_read(neo_vm *vm, const neo_capability *stream, uint8_t *bytes,
                           size_t capacity, neo_io_result *out);
neo_status neo_stream_write(neo_vm *vm, const neo_capability *stream, const uint8_t *bytes,
                            size_t count, neo_io_result *out);
#endif
