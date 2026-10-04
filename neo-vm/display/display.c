#include "internal.h"
#include "display/display.h"

#include <string.h>

#define NEO_BUFFER_LIMIT (64u * 1024u * 1024u)

typedef enum neo_display_kind { NEO_BUFFER_RESOURCE, NEO_WINDOW_RESOURCE } neo_display_kind;

struct neo_display_resource {
    neo_display_kind kind;
    uint8_t *pixels;
    size_t width;
    size_t height;
    size_t size;
    neo_window_backend backend;
    void *context;
    neo_window_state state;
    neo_input_event event;
};

void neo_display_release(neo_vm *vm, neo_object *object) {
    neo_display_resource *resource = object->display;
    if (resource == NULL) { return; }
    if (resource->kind == NEO_WINDOW_RESOURCE) {
        resource->backend.destroy(resource->context);
    }
    neo_free(vm, resource->pixels);
    neo_free(vm, resource);
    object->display = NULL;
}

static neo_status neo_display_resolve(neo_vm *vm, const neo_capability *cap, unsigned rights,
                                      neo_display_kind kind, neo_display_resource **out) {
    neo_object *object = NULL;
    neo_status status = neo_resolve(vm, cap, rights, &object);
    if (status != NEO_OK) { return status; }
    if (object->display == NULL || object->display->kind != kind) { return NEO_WRONG_KIND; }
    *out = object->display;
    return NEO_OK;
}

static neo_status neo_display_publish(neo_vm *vm, const neo_capability *parent, const char *name,
                                      neo_display_resource *resource, const neo_capability **out) {
    neo_status status = neo_object_create(vm, parent, name, (neo_value){0}, out);
    if (status == NEO_OK) {
        neo_lookup(vm, (*out)->target)->display = resource;
    }
    return status;
}

neo_status neo_buffer_create(neo_vm *vm, const neo_capability *parent, const char *name,
                             size_t width, size_t height, const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (vm == NULL || width == 0 || height == 0) { return NEO_INVALID; }
    if (width > NEO_BUFFER_LIMIT / 4u || height > NEO_BUFFER_LIMIT / (width * 4u)) {
        return NEO_LIMIT;
    }
    neo_display_resource *resource = neo_alloc(vm, sizeof(*resource));
    if (resource == NULL) { return NEO_OUT_OF_MEMORY; }
    resource->kind = NEO_BUFFER_RESOURCE;
    resource->width = width;
    resource->height = height;
    resource->size = width * height * 4u;
    resource->pixels = neo_alloc(vm, resource->size);
    neo_status status = resource->pixels == NULL ? NEO_OUT_OF_MEMORY :
        neo_display_publish(vm, parent, name, resource, out);
    if (status != NEO_OK) {
        neo_free(vm, resource->pixels);
        neo_free(vm, resource);
    }
    return status;
}

static neo_status neo_buffer_access(neo_vm *vm, const neo_capability *cap, unsigned rights,
                                    size_t offset, size_t count, neo_display_resource **out) {
    neo_status status = neo_display_resolve(vm, cap, rights, NEO_BUFFER_RESOURCE, out);
    if (status != NEO_OK) { return status; }
    if (offset > (*out)->size || count > (*out)->size - offset) { return NEO_LIMIT; }
    return NEO_OK;
}

neo_status neo_buffer_write(neo_vm *vm, const neo_capability *buffer, size_t offset,
                            const uint8_t *bytes, size_t count) {
    if (bytes == NULL && count != 0) { return NEO_INVALID; }
    neo_display_resource *resource = NULL;
    neo_status status = neo_buffer_access(vm, buffer, NEO_WRITE, offset, count, &resource);
    if (status == NEO_OK && count != 0) { memcpy(resource->pixels + offset, bytes, count); }
    return status;
}

neo_status neo_buffer_read(neo_vm *vm, const neo_capability *buffer, size_t offset,
                           uint8_t *bytes, size_t count) {
    if (bytes == NULL && count != 0) { return NEO_INVALID; }
    neo_display_resource *resource = NULL;
    neo_status status = neo_buffer_access(vm, buffer, NEO_READ, offset, count, &resource);
    if (status == NEO_OK && count != 0) { memcpy(bytes, resource->pixels + offset, count); }
    return status;
}

neo_status neo_buffer_fill(neo_vm *vm, const neo_capability *buffer, uint8_t value) {
    neo_display_resource *resource = NULL;
    neo_status status = neo_display_resolve(vm, buffer, NEO_WRITE, NEO_BUFFER_RESOURCE, &resource);
    if (status == NEO_OK) { memset(resource->pixels, value, resource->size); }
    return status;
}

neo_status neo_buffer_size(neo_vm *vm, const neo_capability *buffer, size_t *out_size) {
    if (out_size == NULL) { return NEO_INVALID; }
    *out_size = 0;
    neo_display_resource *resource = NULL;
    neo_status status = neo_display_resolve(vm, buffer, NEO_READ, NEO_BUFFER_RESOURCE, &resource);
    if (status == NEO_OK) { *out_size = resource->size; }
    return status;
}

neo_status neo_window_create(neo_vm *vm, const neo_capability *parent, const char *name,
                             const neo_window_backend *backend, void *context,
                             const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (vm == NULL || backend == NULL || backend->present == NULL ||
        backend->poll == NULL || backend->destroy == NULL) { return NEO_INVALID; }
    neo_display_resource *resource = neo_alloc(vm, sizeof(*resource));
    if (resource == NULL) { return NEO_OUT_OF_MEMORY; }
    resource->kind = NEO_WINDOW_RESOURCE;
    resource->backend = *backend;
    resource->context = context;
    neo_status status = neo_display_publish(vm, parent, name, resource, out);
    if (status != NEO_OK) { neo_free(vm, resource); }
    return status;
}

neo_status neo_window_poll(neo_vm *vm, const neo_capability *window, neo_window_state *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = (neo_window_state){0};
    neo_display_resource *resource = NULL;
    neo_status status = neo_display_resolve(vm, window, NEO_WRITE, NEO_WINDOW_RESOURCE, &resource);
    if (status == NEO_OK) {
        neo_window_state next = {0};
        status = resource->backend.poll(resource->context, &next);
        if (status == NEO_OK) { resource->state = next; *out = next; }
    }
    return status;
}

neo_status neo_window_get_state(neo_vm *vm, const neo_capability *window, neo_window_state *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = (neo_window_state){0};
    neo_display_resource *resource = NULL;
    neo_status status = neo_display_resolve(vm, window, NEO_READ, NEO_WINDOW_RESOURCE, &resource);
    if (status == NEO_OK) { *out = resource->state; }
    return status;
}

neo_status neo_window_present(neo_vm *vm, const neo_capability *window,
                              const neo_capability *buffer) {
    neo_display_resource *destination = NULL, *source = NULL;
    neo_status status = neo_display_resolve(vm, window, NEO_WRITE, NEO_WINDOW_RESOURCE, &destination);
    if (status == NEO_OK) {
        status = neo_display_resolve(vm, buffer, NEO_READ, NEO_BUFFER_RESOURCE, &source);
    }
    if (status != NEO_OK) { return status; }
    if (destination->state.closed) { return NEO_UNAVAILABLE; }
    return destination->backend.present(destination->context, source->pixels, source->width, source->height);
}

const char *neo_input_kind_name(neo_input_kind kind) {
    static const char *const names[] = {"none", "close", "resize", "expose", "focus",
        "pointer", "button", "key", "overflow"};
    if (kind < NEO_INPUT_NONE || kind > NEO_INPUT_OVERFLOW) { return "unknown"; }
    return names[(size_t)kind];
}

neo_status neo_window_next_event(neo_vm *vm, const neo_capability *window, neo_input_event *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = (neo_input_event){0};
    neo_display_resource *resource = NULL;
    neo_status status = neo_display_resolve(vm, window, NEO_READ | NEO_WRITE, NEO_WINDOW_RESOURCE, &resource);
    if (status != NEO_OK) { return status; }
    if (resource->backend.next_event == NULL) { return NEO_UNSUPPORTED; }
    neo_input_event event = {0};
    status = resource->backend.next_event(resource->context, &event);
    if (status != NEO_OK) { return status; }
    if (event.kind < NEO_INPUT_NONE || event.kind > NEO_INPUT_OVERFLOW ||
        memchr(event.key, '\0', sizeof(event.key)) == NULL) { return NEO_BAD_STATE; }
    resource->event = event;
    *out = event;
    return NEO_OK;
}

neo_status neo_window_get_event(neo_vm *vm, const neo_capability *window, neo_input_event *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = (neo_input_event){0};
    neo_display_resource *resource = NULL;
    neo_status status = neo_display_resolve(vm, window, NEO_READ, NEO_WINDOW_RESOURCE, &resource);
    if (status == NEO_OK) { *out = resource->event; }
    return status;
}

neo_status neo_buffer_dimensions(neo_vm *vm, const neo_capability *buffer,
                                 size_t *out_width, size_t *out_height) {
    if (out_width == NULL || out_height == NULL) { return NEO_INVALID; }
    *out_width = 0; *out_height = 0;
    neo_display_resource *resource = NULL;
    neo_status status = neo_display_resolve(vm, buffer, NEO_READ, NEO_BUFFER_RESOURCE, &resource);
    if (status == NEO_OK) { *out_width = resource->width; *out_height = resource->height; }
    return status;
}
