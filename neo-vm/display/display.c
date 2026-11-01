#include "internal.h"
#include "display/display.h"
#include "display/text.h"
#include "display/headless.h"

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
    bool reset_pending;
    bool surface_pending;
    neo_headless_display *surface;
};

void neo_display_release(neo_vm *vm, neo_object *object) {
    neo_display_resource *resource = object->display;
    if (resource == NULL) { return; }
    if (resource->kind == NEO_WINDOW_RESOURCE) {
        resource->backend.destroy(resource->context);
        neo_headless_release(resource->surface);
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
    resource->state.scale_numerator = 1; resource->state.scale_denominator = 1;
    resource->backend = *backend;
    resource->context = context;
    neo_status status = neo_display_publish(vm, parent, name, resource, out);
    if (status != NEO_OK) { neo_free(vm, resource); }
    return status;
}

void neo_window_input_reset(neo_window_state *state) {
    state->captured = false;
    if (state->input_epoch < INT64_MAX) { ++state->input_epoch; }
    else { state->closed = true; }
}

neo_status neo_window_surface_change(neo_window_state *state, uint32_t numerator,
                                      uint32_t denominator, bool lost) {
    if (state == NULL || numerator == 0 || denominator == 0) { return NEO_INVALID; }
    uint32_t divisor = numerator, remainder = denominator;
    while (remainder != 0) { uint32_t next = divisor % remainder; divisor = remainder; remainder = next; }
    numerator /= divisor; denominator /= divisor;
    uint32_t old_numerator = state->scale_numerator, old_denominator = state->scale_denominator;
    if (old_numerator == 0 && old_denominator == 0) { old_numerator = 1; old_denominator = 1; }
    if (old_numerator == numerator && old_denominator == denominator && state->presentation_lost == lost) { return NEO_OK; }
    if (state->surface_epoch >= INT64_MAX) { return NEO_LIMIT; }
    state->scale_numerator = numerator; state->scale_denominator = denominator;
    state->presentation_lost = lost; ++state->surface_epoch;
    state->redraw = true;
    return NEO_OK;
}

static neo_status neo_window_accept_state(neo_display_resource *resource, neo_window_state next) {
    if (next.scale_numerator == 0 && next.scale_denominator == 0) { next.scale_numerator = 1; next.scale_denominator = 1; }
    if (next.scale_numerator == 0 || next.scale_denominator == 0 || next.surface_epoch > INT64_MAX ||
        next.surface_epoch < resource->state.surface_epoch) { return NEO_BAD_STATE; }
    if (next.surface_epoch == resource->state.surface_epoch &&
        ((uint64_t)next.scale_numerator * resource->state.scale_denominator !=
         (uint64_t)resource->state.scale_numerator * next.scale_denominator ||
         next.presentation_lost != resource->state.presentation_lost)) { return NEO_BAD_STATE; }
    if ((resource->state.closed && !next.closed) || next.input_epoch > INT64_MAX || next.input_epoch < resource->state.input_epoch ||
        (next.captured && (!next.focused || next.closed))) { return NEO_BAD_STATE; }
    if (next.input_epoch != resource->state.input_epoch || (next.closed && !resource->state.closed)) { resource->reset_pending = true; }
    if (next.surface_epoch != resource->state.surface_epoch) { resource->surface_pending = true; }
    resource->state = next;
    return NEO_OK;
}

neo_status neo_window_capture(neo_vm *vm, const neo_capability *window, bool enabled) {
    neo_display_resource *resource = NULL;
    neo_status status = neo_display_resolve(vm, window, NEO_WRITE, NEO_WINDOW_RESOURCE, &resource);
    if (status != NEO_OK) { return status; }
    if (resource->backend.capture == NULL) { return NEO_UNSUPPORTED; }
    neo_window_state next = {0};
    status = resource->backend.capture(resource->context, enabled, &next);
    return status == NEO_OK ? neo_window_accept_state(resource, next) : status;
}

neo_status neo_window_poll(neo_vm *vm, const neo_capability *window, neo_window_state *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = (neo_window_state){0};
    neo_display_resource *resource = NULL;
    neo_status status = neo_display_resolve(vm, window, NEO_WRITE, NEO_WINDOW_RESOURCE, &resource);
    if (status == NEO_OK) {
        neo_window_state next = {0};
        status = resource->backend.poll(resource->context, &next);
        if (status == NEO_OK) { status = neo_window_accept_state(resource, next); }
        if (status == NEO_OK) { *out = resource->state; }
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
        "pointer", "button", "key", "overflow", "input-reset", "capture", "text", "surface", "pinch"};
    if (kind < NEO_INPUT_NONE || kind > NEO_INPUT_PINCH) { return "unknown"; }
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
    if (resource->reset_pending) {
        event.kind = NEO_INPUT_RESET; event.epoch = resource->state.input_epoch;
        resource->reset_pending = false;
    } else if (resource->surface_pending) {
        event.kind = NEO_INPUT_SURFACE; event.epoch = resource->state.input_epoch;
        resource->surface_pending = false;
    } else {
        bool found = false;
        for (size_t i = 0; i < 256; ++i) {
            status = resource->backend.next_event(resource->context, &event);
            if (status != NEO_OK) { return status; }
            if (event.kind < NEO_INPUT_NONE || (event.kind > NEO_INPUT_TEXT && event.kind != NEO_INPUT_PINCH) ||
                memchr(event.key, '\0', sizeof(event.key)) == NULL) { return NEO_BAD_STATE; }
            if (event.kind == NEO_INPUT_TEXT && !neo_text_valid(event.text, sizeof(event.text))) { return NEO_BAD_STATE; }
            if (event.kind == NEO_INPUT_PINCH && (event.width <= 0 || event.height <= 0)) { return NEO_BAD_STATE; }
            if (event.kind != NEO_INPUT_TEXT) { event.text[0] = '\0'; }
            if (event.kind == NEO_INPUT_NONE || event.kind == NEO_INPUT_OVERFLOW || event.epoch >= resource->state.input_epoch) { found = true; break; }
        }
        if (!found) { return NEO_LIMIT; }
    }
    if (event.kind == NEO_INPUT_NONE) { event.epoch = resource->state.input_epoch; }
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

neo_status neo_surface_create(neo_vm *vm, const neo_capability *parent, const char *name,
                              size_t width, size_t height, const neo_capability **out) {
    neo_headless_display *control = NULL;
    neo_status status = neo_headless_open(vm, parent, name, width, height, &control, out);
    if (status == NEO_OK) { neo_lookup(vm, (*out)->target)->display->surface = control; }
    return status;
}

neo_status neo_surface_focus(neo_vm *vm, const neo_capability *surface, bool focused) {
    neo_display_resource *resource = NULL;
    neo_status status = neo_display_resolve(vm, surface, NEO_WRITE | NEO_ACT, NEO_WINDOW_RESOURCE, &resource);
    if (status != NEO_OK) { return status; }
    if (resource->surface == NULL) { return NEO_WRONG_KIND; }
    return neo_headless_push(resource->surface, &(neo_input_event){.kind=NEO_INPUT_FOCUS,.pressed=focused});
}

neo_status neo_surface_route(neo_vm *vm, const neo_capability *surface,
                             const neo_capability *source, int64_t x, int64_t y) {
    neo_display_resource *resource = NULL;
    neo_status status = neo_display_resolve(vm, surface, NEO_WRITE | NEO_ACT, NEO_WINDOW_RESOURCE, &resource);
    if (status != NEO_OK) { return status; }
    if (resource->surface == NULL) { return NEO_WRONG_KIND; }
    neo_input_event event = {0};
    status = neo_window_get_event(vm, source, &event);
    if (status != NEO_OK) { return status; }
    if (event.kind != NEO_INPUT_KEY && event.kind != NEO_INPUT_TEXT &&
        event.kind != NEO_INPUT_POINTER && event.kind != NEO_INPUT_BUTTON) { return NEO_INVALID; }
    event.x = x; event.y = y;
    return neo_headless_push(resource->surface, &event);
}

neo_status neo_buffer_copy_region(neo_vm *vm, const neo_capability *destination,
                                  const neo_capability *source, int64_t x, int64_t y,
                                  size_t width, size_t height) {
    neo_display_resource *to = NULL, *from = NULL;
    neo_status status = neo_display_resolve(vm, destination, NEO_WRITE, NEO_BUFFER_RESOURCE, &to);
    if (status == NEO_OK) { status = neo_display_resolve(vm, source, NEO_READ, NEO_BUFFER_RESOURCE, &from); }
    if (status != NEO_OK) { return status; }
    if (to == from) { return NEO_INVALID; }
    if (width > from->width || height > from->height) { return NEO_LIMIT; }
    uint64_t skip_x = x < 0 ? (uint64_t)(-(x + 1)) + 1u : 0;
    uint64_t skip_y = y < 0 ? (uint64_t)(-(y + 1)) + 1u : 0;
    if (skip_x >= width || skip_y >= height ||
        (x >= 0 && (uint64_t)x >= to->width) || (y >= 0 && (uint64_t)y >= to->height)) { return NEO_OK; }
    size_t dx = x < 0 ? 0 : (size_t)x, dy = y < 0 ? 0 : (size_t)y;
    size_t sx = (size_t)skip_x, sy = (size_t)skip_y;
    width -= sx; height -= sy;
    if (width > to->width - dx) { width = to->width - dx; }
    if (height > to->height - dy) { height = to->height - dy; }
    for (size_t row = 0; row < height; ++row) {
        memcpy(to->pixels + ((dy + row) * to->width + dx) * 4u,
               from->pixels + ((sy + row) * from->width + sx) * 4u, width * 4u);
    }
    return NEO_OK;
}

neo_status neo_buffer_transfer_work(neo_vm *vm, const neo_capability *destination,
                                    int64_t x, int64_t y, size_t width, size_t height, size_t *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out=0;
    neo_display_resource *to=NULL;
    neo_status status=neo_display_resolve(vm,destination,NEO_WRITE,NEO_BUFFER_RESOURCE,&to);
    if (status != NEO_OK) { return status; }
    uint64_t sx=x < 0 ? (uint64_t)(-(x+1))+1u : 0,sy=y < 0 ? (uint64_t)(-(y+1))+1u : 0;
    if (sx >= width || sy >= height || (x >= 0 && (uint64_t)x >= to->width) || (y >= 0 && (uint64_t)y >= to->height)) { return NEO_OK; }
    size_t dx=x < 0 ? 0 : (size_t)x,dy=y < 0 ? 0 : (size_t)y;
    width-=(size_t)sx; height-=(size_t)sy;
    if (width > to->width-dx) { width=to->width-dx; }
    if (height > to->height-dy) { height=to->height-dy; }
    *out=(width*height*4u+63u)/64u;
    return NEO_OK;
}

neo_status neo_buffer_scale_copy(neo_vm *vm, const neo_capability *destination,
                                 const neo_capability *source, int64_t x, int64_t y,
                                 size_t width, size_t height, size_t output_width, size_t output_height) {
    neo_display_resource *to=NULL,*from=NULL;
    neo_status status=neo_display_resolve(vm,destination,NEO_WRITE,NEO_BUFFER_RESOURCE,&to);
    if (status == NEO_OK) { status=neo_display_resolve(vm,source,NEO_READ,NEO_BUFFER_RESOURCE,&from); }
    if (status != NEO_OK) { return status; }
    if (to == from) { return NEO_INVALID; }
    if (width > from->width || height > from->height || output_width > NEO_BUFFER_LIMIT/4u || output_height > NEO_BUFFER_LIMIT/4u) { return NEO_LIMIT; }
    if (width == 0 || height == 0 || output_width == 0 || output_height == 0) { return NEO_OK; }
    if (output_width == width && output_height == height) { return neo_buffer_copy_region(vm,destination,source,x,y,width,height); }
    uint64_t skip_x=x < 0 ? (uint64_t)(-(x+1))+1u : 0;
    uint64_t skip_y=y < 0 ? (uint64_t)(-(y+1))+1u : 0;
    if (skip_x >= output_width || skip_y >= output_height || (x >= 0 && (uint64_t)x >= to->width) || (y >= 0 && (uint64_t)y >= to->height)) { return NEO_OK; }
    size_t dx=x < 0 ? 0 : (size_t)x,dy=y < 0 ? 0 : (size_t)y;
    size_t columns=output_width-(size_t)skip_x,rows=output_height-(size_t)skip_y;
    if (columns > to->width-dx) { columns=to->width-dx; }
    if (rows > to->height-dy) { rows=to->height-dy; }
    for (size_t row=0; row<rows; ++row) {
        size_t sy=(size_t)(((uint64_t)row+skip_y)*height/output_height);
        for (size_t column=0; column<columns; ++column) {
            size_t sx=(size_t)(((uint64_t)column+skip_x)*width/output_width);
            memcpy(to->pixels+((dy+row)*to->width+dx+column)*4u,from->pixels+(sy*from->width+sx)*4u,4);
        }
    }
    return NEO_OK;
}
