#include "internal.h"
#include "replication/replication.h"
#include "image/image.h"
#include <string.h>

#define NEO_REPLICA_HEADER 12u
#define NEO_REPLICA_CHUNK 4096u
static const uint8_t neo_replica_magic[8] = {'N','E','O','S',0,0,0,1};

typedef struct neo_replica_channel {
    neo_vm *vm;
    const neo_capability *stream;
    uint8_t *send;
    size_t send_size, sent;
    uint8_t header[NEO_REPLICA_HEADER];
    size_t header_size, receive_size, received;
    char *receive;
    bool send_failed, receive_failed, receive_ended;
} neo_replica_channel;

static void neo_replica_destroy(void *context) {
    neo_replica_channel *channel = context;
    neo_free(channel->vm, channel->send);
    neo_free(channel->vm, channel->receive);
    neo_free(channel->vm, channel);
}
static neo_status neo_replica_no_read(void *context, uint8_t *bytes, size_t size, neo_io_result *out) {
    (void)context; (void)bytes; (void)size; (void)out;
    return NEO_UNSUPPORTED;
}
static neo_status neo_replica_get(neo_vm *vm, const neo_capability *cap, neo_replica_channel **out) {
    void *context = NULL;
    neo_status status = neo_stream_context(vm, cap, NEO_ACT, neo_replica_destroy, &context);
    *out = context;
    return status;
}
neo_status neo_replica_channel_create(neo_vm *vm, const neo_capability *stream,
    const neo_capability *parent, const char *name, const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    neo_object *object = NULL;
    neo_status status = neo_resolve(vm, stream, 0, &object);
    if (status != NEO_OK) { return status; }
    if (object->stream == NULL) { return NEO_WRONG_KIND; }
    if ((stream->rights & (NEO_READ | NEO_WRITE)) == 0) { return NEO_DENIED; }
    neo_replica_channel *channel = neo_alloc(vm, sizeof(*channel));
    if (channel == NULL) { return NEO_OUT_OF_MEMORY; }
    *channel = (neo_replica_channel){.vm=vm,.stream=stream};
    const neo_stream_backend backend = {.read=neo_replica_no_read,.destroy=neo_replica_destroy};
    status = neo_stream_create(vm, parent, name, &backend, channel, out);
    if (status != NEO_OK) { neo_free(vm, channel); }
    return status;
}
neo_status neo_replica_prepare(neo_vm *vm, const neo_capability *cap, const neo_capability *source) {
    neo_replica_channel *channel = NULL;
    neo_status status = neo_replica_get(vm, cap, &channel);
    if (status != NEO_OK) { return status; }
    if (channel->send_failed) { return NEO_IO_ERROR; }
    if (channel->send != NULL) { return NEO_BUSY; }
    neo_object *transport = NULL;
    status = neo_resolve(vm, channel->stream, NEO_WRITE, &transport);
    if (status != NEO_OK) { return status; }
    const neo_capability *snapshot = NULL;
    char *text = NULL;
    status = neo_object_extract(vm, source, &snapshot);
    if (status == NEO_OK) { status = neo_image_format(vm, snapshot, &text); }
    if (snapshot != NULL) { (void)neo_image_unload(vm, snapshot); }
    if (status != NEO_OK) { return status; }
    size_t size = strlen(text);
    if (size == 0 || size > NEO_REPLICA_MAX_BYTES) { status = NEO_LIMIT; }
    uint8_t *frame = NULL;
    if (status == NEO_OK) {
        frame = neo_alloc(vm, NEO_REPLICA_HEADER + size);
        if (frame == NULL) { status = NEO_OUT_OF_MEMORY; }
    }
    if (status == NEO_OK) {
        memcpy(frame, neo_replica_magic, sizeof(neo_replica_magic));
        uint32_t length = (uint32_t)size;
        for (size_t i = 0; i < 4; ++i) { frame[8 + i] = (uint8_t)(length >> (24u - (unsigned)i * 8u)); }
        memcpy(frame + NEO_REPLICA_HEADER, text, size);
        channel->send = frame; channel->send_size = NEO_REPLICA_HEADER + size; channel->sent = 0;
    }
    neo_image_text_free(vm, text);
    return status;
}
neo_status neo_replica_send(neo_vm *vm, const neo_capability *cap, bool *complete) {
    if (complete == NULL) { return NEO_INVALID; }
    *complete = false;
    neo_replica_channel *channel = NULL;
    neo_status status = neo_replica_get(vm, cap, &channel);
    if (status != NEO_OK) { return status; }
    if (channel->send_failed) { return NEO_IO_ERROR; }
    if (channel->send == NULL) { return NEO_UNAVAILABLE; }
    size_t size = channel->send_size - channel->sent;
    if (size > NEO_REPLICA_CHUNK) { size = NEO_REPLICA_CHUNK; }
    neo_io_result result = {0};
    status = neo_stream_write(vm, channel->stream, channel->send + channel->sent, size, &result);
    if (status != NEO_OK) { channel->send_failed = true; return status; }
    channel->sent += result.count;
    if (channel->sent == channel->send_size) {
        neo_free(vm, channel->send); channel->send = NULL;
        channel->send_size = 0; channel->sent = 0; *complete = true;
    }
    return NEO_OK;
}
neo_status neo_replica_receive(neo_vm *vm, const neo_capability *cap, bool *complete) {
    if (complete == NULL) { return NEO_INVALID; }
    *complete = false;
    neo_replica_channel *channel = NULL;
    neo_status status = neo_replica_get(vm, cap, &channel);
    if (status != NEO_OK) { return status; }
    if (channel->receive_failed) { return NEO_IO_ERROR; }
    if (channel->receive_ended) { return NEO_OK; }
    if (channel->receive != NULL && channel->received == channel->receive_size) { *complete = true; return NEO_OK; }
    bool header = channel->header_size != NEO_REPLICA_HEADER;
    uint8_t *bytes = channel->header + channel->header_size;
    size_t size = NEO_REPLICA_HEADER - channel->header_size;
    if (!header) {
        if (channel->receive == NULL) {
            channel->receive = neo_alloc(vm, channel->receive_size + 1);
            if (channel->receive == NULL) { return NEO_OUT_OF_MEMORY; }
        }
        bytes = (uint8_t *)channel->receive + channel->received;
        size = channel->receive_size - channel->received;
        if (size > NEO_REPLICA_CHUNK) { size = NEO_REPLICA_CHUNK; }
    }
    neo_io_result result = {0};
    status = neo_stream_read(vm, channel->stream, bytes, size, &result);
    if (status == NEO_OK && result.state == NEO_IO_EOF && channel->header_size == 0) {
        channel->receive_ended = true;
        return NEO_OK;
    }
    if (status != NEO_OK || result.state == NEO_IO_EOF) {
        channel->receive_failed = true;
        return status != NEO_OK ? status : NEO_IO_ERROR;
    }
    if (header) {
        channel->header_size += result.count;
        if (channel->header_size == NEO_REPLICA_HEADER) {
            if (memcmp(channel->header, neo_replica_magic, sizeof(neo_replica_magic)) != 0) {
                channel->receive_failed = true; return NEO_INVALID;
            }
            uint32_t length = 0;
            for (size_t i = 0; i < 4; ++i) { length = (length << 8) | channel->header[8 + i]; }
            if (length == 0 || length > NEO_REPLICA_MAX_BYTES) { channel->receive_failed = true; return NEO_LIMIT; }
            channel->receive_size = length;
        }
    } else {
        channel->received += result.count;
        if (channel->received == channel->receive_size) {
            channel->receive[channel->received] = '\0';
            *complete = true;
        }
    }
    return NEO_OK;
}
neo_status neo_replica_ended(neo_vm *vm, const neo_capability *cap, bool *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = false;
    neo_replica_channel *channel = NULL;
    neo_status status = neo_replica_get(vm, cap, &channel);
    if (status != NEO_OK) { return status; }
    neo_object *stream = NULL;
    status = neo_resolve(vm, channel->stream, NEO_READ, &stream);
    if (status != NEO_OK) { return status; }
    if (channel->receive_failed) { return NEO_IO_ERROR; }
    *out = channel->receive_ended;
    return NEO_OK;
}
neo_status neo_replica_import(neo_vm *vm, const neo_capability *cap,
    const neo_capability *destination, const char *name, const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    neo_replica_channel *channel = NULL;
    neo_status status = neo_replica_get(vm, cap, &channel);
    if (status != NEO_OK) { return status; }
    if (channel->receive_failed) { return NEO_IO_ERROR; }
    if (channel->receive == NULL || channel->received != channel->receive_size) { return NEO_UNAVAILABLE; }
    if (name == NULL || name[0] == '\0') { return NEO_INVALID; }
    neo_object *container = NULL;
    status = neo_resolve(vm, destination, NEO_INSERT, &container);
    if (status != NEO_OK) { return status; }
    if (neo_child_named(vm, container->id, name) != NULL) { return NEO_CONFLICT; }
    if (memchr(channel->receive, '\0', channel->receive_size) != NULL) { return NEO_PARSE_ERROR; }
    const neo_capability *snapshot = NULL;
    neo_diagnostic error = {0};
    status = neo_image_parse(vm, channel->receive, &snapshot, &error);
    if (status == NEO_OK) { status = neo_object_import(vm, snapshot, destination, name, out); }
    if (snapshot != NULL) { (void)neo_image_unload(vm, snapshot); }
    if (status == NEO_OK) {
        neo_free(vm, channel->receive); channel->receive = NULL;
        channel->receive_size = 0; channel->received = 0; channel->header_size = 0;
    }
    return status;
}
