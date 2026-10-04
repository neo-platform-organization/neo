#include "internal.h"
#include "io/io.h"

struct neo_stream_resource { neo_stream_backend backend; void *context; };

void neo_stream_release(neo_vm *vm, neo_object *object) {
    if (object->stream == NULL) { return; }
    object->stream->backend.destroy(object->stream->context);
    neo_free(vm, object->stream);
    object->stream = NULL;
}

neo_status neo_stream_create(neo_vm *vm, const neo_capability *parent, const char *name,
                             const neo_stream_backend *backend, void *context,
                             const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (vm == NULL || backend == NULL || backend->destroy == NULL ||
        (backend->read == NULL && backend->write == NULL)) { return NEO_INVALID; }
    neo_stream_resource *resource = neo_alloc(vm, sizeof(*resource));
    if (resource == NULL) { return NEO_OUT_OF_MEMORY; }
    resource->backend = *backend;
    resource->context = context;
    neo_status status = neo_object_create(vm, parent, name, (neo_value){0}, out);
    if (status == NEO_OK) { neo_lookup(vm, (*out)->target)->stream = resource; }
    else { neo_free(vm, resource); }
    return status;
}

static neo_status neo_stream_transfer(neo_vm *vm, const neo_capability *stream,
                                      uint8_t *read_bytes, const uint8_t *write_bytes,
                                      size_t count, bool writing, neo_io_result *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = (neo_io_result){0};
    if (count != 0 && (writing ? write_bytes == NULL : read_bytes == NULL)) { return NEO_INVALID; }
    neo_object *object = NULL;
    neo_status status = neo_resolve(vm, stream, writing ? NEO_WRITE : NEO_READ, &object);
    if (status != NEO_OK) { return status; }
    neo_stream_resource *resource = object->stream;
    if (resource == NULL) { return NEO_WRONG_KIND; }
    if ((writing && resource->backend.write == NULL) ||
        (!writing && resource->backend.read == NULL)) { return NEO_UNSUPPORTED; }
    if (count == 0) { return NEO_OK; }
    neo_io_result result = {0};
    status = writing ? resource->backend.write(resource->context, write_bytes, count, &result) :
        resource->backend.read(resource->context, read_bytes, count, &result);
    if (status != NEO_OK) { return status; }
    if (result.count > count ||
        (result.state == NEO_IO_TRANSFERRED && result.count == 0) ||
        (result.state != NEO_IO_TRANSFERRED && result.count != 0) ||
        (writing && result.state == NEO_IO_EOF) ||
        (result.state != NEO_IO_TRANSFERRED && result.state != NEO_IO_EOF &&
         result.state != NEO_IO_WOULD_BLOCK)) { return NEO_BAD_STATE; }
    *out = result;
    return NEO_OK;
}

neo_status neo_stream_read(neo_vm *vm, const neo_capability *stream, uint8_t *bytes,
                           size_t capacity, neo_io_result *out) {
    return neo_stream_transfer(vm, stream, bytes, NULL, capacity, false, out);
}
neo_status neo_stream_write(neo_vm *vm, const neo_capability *stream, const uint8_t *bytes,
                            size_t count, neo_io_result *out) {
    return neo_stream_transfer(vm, stream, NULL, bytes, count, true, out);
}
