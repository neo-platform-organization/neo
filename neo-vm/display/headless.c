#include "display/headless.h"
#include "display/text.h"
#include <stdlib.h>
#include <string.h>

#define NEO_HEADLESS_PIXEL_LIMIT (64u * 1024u * 1024u)

struct neo_headless_display {
    size_t references;
    bool attached;
    neo_window_state state;
    neo_input_event events[NEO_HEADLESS_EVENT_CAPACITY];
    size_t first, count;
    uint64_t lost, frames;
    uint8_t *pixels;
    size_t size, width, height;
    neo_status present_status;
};

void neo_headless_release(neo_headless_display *display) {
    if (display == NULL) { return; }
    if (--display->references == 0) { free(display->pixels); free(display); }
}

static void neo_headless_destroy(void *context) {
    neo_headless_display *display = context;
    display->attached = false;
    free(display->pixels);
    display->pixels = NULL;
    display->size = 0;
    neo_headless_release(display);
}

static neo_status neo_headless_present(void *context, const uint8_t *pixels, size_t width, size_t height) {
    neo_headless_display *display = context;
    if (!display->attached || display->state.closed) { return NEO_UNAVAILABLE; }
    if (display->present_status != NEO_OK) { return display->present_status; }
    if (display->frames == UINT64_MAX) { return NEO_LIMIT; }
    if (pixels == NULL || width == 0 || height == 0) { return NEO_INVALID; }
    if (width > NEO_HEADLESS_PIXEL_LIMIT / 4u || height > NEO_HEADLESS_PIXEL_LIMIT / (width * 4u)) { return NEO_LIMIT; }
    size_t size = width * height * 4u;
    uint8_t *copy = malloc(size);
    if (copy == NULL) { return NEO_OUT_OF_MEMORY; }
    memcpy(copy, pixels, size);
    free(display->pixels);
    display->pixels = copy;
    display->size = size; display->width = width; display->height = height;
    ++display->frames;
    return NEO_OK;
}

static neo_status neo_headless_poll(void *context, neo_window_state *out) {
    neo_headless_display *display = context;
    if (!display->attached) { return NEO_UNAVAILABLE; }
    *out = display->state;
    display->state.redraw = false;
    return NEO_OK;
}

static neo_status neo_headless_next(void *context, neo_input_event *out) {
    neo_headless_display *display = context;
    if (!display->attached) { return NEO_UNAVAILABLE; }
    *out = (neo_input_event){0};
    if (display->lost != 0) {
        out->kind = NEO_INPUT_OVERFLOW; out->epoch = display->state.input_epoch; out->lost = display->lost; display->lost = 0;
    } else if (display->count != 0) {
        *out = display->events[display->first];
        display->first = (display->first + 1u) % NEO_HEADLESS_EVENT_CAPACITY;
        --display->count;
    }
    return NEO_OK;
}

static neo_status neo_headless_capture(void *context, bool enabled, neo_window_state *out) {
    neo_headless_display *display = context;
    if (!display->attached || display->state.closed || (enabled && !display->state.focused)) { return NEO_UNAVAILABLE; }
    if (!enabled && display->state.captured) { neo_window_input_reset(&display->state); }
    display->state.captured = enabled;
    *out = display->state;
    return NEO_OK;
}

neo_status neo_headless_open(neo_vm *vm, const neo_capability *parent, const char *name,
                             size_t width, size_t height, neo_headless_display **out_control,
                             const neo_capability **out_window) {
    if (out_control != NULL) { *out_control = NULL; }
    if (out_window != NULL) { *out_window = NULL; }
    if (out_control == NULL || out_window == NULL || width == 0 || height == 0 ||
        width > INT64_MAX || height > INT64_MAX) { return NEO_INVALID; }
    neo_headless_display *display = calloc(1, sizeof(*display));
    if (display == NULL) { return NEO_OUT_OF_MEMORY; }
    display->references = 1;
    display->state = (neo_window_state){.width=width,.height=height,.redraw=true,.scale_numerator=1,.scale_denominator=1};
    const neo_window_backend backend = {neo_headless_present,neo_headless_poll,neo_headless_destroy,neo_headless_next,neo_headless_capture};
    neo_status status = neo_window_create(vm, parent, name, &backend, display, out_window);
    if (status != NEO_OK) { neo_headless_release(display); return status; }
    display->attached = true;
    ++display->references;
    *out_control = display;
    return NEO_OK;
}

neo_status neo_headless_push(neo_headless_display *display, const neo_input_event *event) {
    if (display == NULL || event == NULL) { return NEO_INVALID; }
    if (!display->attached || display->state.closed) { return NEO_UNAVAILABLE; }
    if (event->kind <= NEO_INPUT_NONE || (event->kind > NEO_INPUT_TEXT && event->kind != NEO_INPUT_PINCH) || event->kind == NEO_INPUT_OVERFLOW || event->kind == NEO_INPUT_RESET ||
        memchr(event->key, '\0', sizeof(event->key)) == NULL) { return NEO_INVALID; }
    if (event->kind == NEO_INPUT_RESIZE && (event->width < 0 || event->height < 0 ||
        (uint64_t)event->width > SIZE_MAX || (uint64_t)event->height > SIZE_MAX)) { return NEO_INVALID; }
    if (event->kind == NEO_INPUT_BUTTON && (event->button < 1 || event->button > 5)) { return NEO_INVALID; }
    if (event->kind == NEO_INPUT_KEY && event->key[0] == '\0') { return NEO_INVALID; }
    if (event->kind == NEO_INPUT_TEXT && !neo_text_valid(event->text, sizeof(event->text))) { return NEO_INVALID; }
    if (event->kind == NEO_INPUT_PINCH && (event->width <= 0 || event->height <= 0)) { return NEO_INVALID; }
    if (event->kind == NEO_INPUT_FOCUS) {
        if (!event->pressed && (display->state.focused || display->state.captured)) { neo_window_input_reset(&display->state); }
        display->state.focused = event->pressed;
    }
    if (event->kind == NEO_INPUT_CAPTURE) {
        if (event->pressed) { return NEO_INVALID; }
        if (display->state.captured) { neo_window_input_reset(&display->state); }
    }
    if (event->kind == NEO_INPUT_CLOSE) {
        neo_window_input_reset(&display->state); display->state.closed = true; display->state.focused = false;
    }
    if (event->kind == NEO_INPUT_EXPOSE || event->kind == NEO_INPUT_RESIZE) { display->state.redraw = true; }
    if (event->kind == NEO_INPUT_RESIZE) {
        display->state.width = (size_t)event->width; display->state.height = (size_t)event->height;
    }
    if (display->count == NEO_HEADLESS_EVENT_CAPACITY) {
        if (display->lost == 0) { neo_window_input_reset(&display->state); }
        if (display->lost != UINT64_MAX) { ++display->lost; }
        return NEO_LIMIT;
    }
    display->events[(display->first + display->count) % NEO_HEADLESS_EVENT_CAPACITY] = *event;
    display->events[(display->first + display->count) % NEO_HEADLESS_EVENT_CAPACITY].epoch = display->state.input_epoch;
    ++display->count;
    return NEO_OK;
}

neo_status neo_headless_present_status(neo_headless_display *display, neo_status status) {
    if (display == NULL || (status != NEO_OK && status != NEO_UNAVAILABLE)) { return NEO_INVALID; }
    if (!display->attached) { return NEO_UNAVAILABLE; }
    if (display->state.closed) { return NEO_UNAVAILABLE; }
    neo_status changed = neo_window_surface_change(&display->state, display->state.scale_numerator,
        display->state.scale_denominator, status == NEO_UNAVAILABLE);
    if (changed != NEO_OK) { return changed; }
    display->present_status = status;
    return NEO_OK;
}

neo_status neo_headless_scale(neo_headless_display *display, uint32_t numerator, uint32_t denominator) {
    if (display == NULL) { return NEO_INVALID; }
    if (!display->attached || display->state.closed) { return NEO_UNAVAILABLE; }
    return neo_window_surface_change(&display->state, numerator, denominator, display->state.presentation_lost);
}

neo_status neo_headless_frame(neo_headless_display *display, uint8_t *bytes, size_t capacity,
                              size_t *out_count, size_t *out_width, size_t *out_height,
                              uint64_t *out_frames) {
    if (out_count != NULL) { *out_count = 0; }
    if (out_width != NULL) { *out_width = 0; }
    if (out_height != NULL) { *out_height = 0; }
    if (out_frames != NULL) { *out_frames = 0; }
    if (display == NULL || out_count == NULL || out_width == NULL || out_height == NULL || out_frames == NULL ||
        (bytes == NULL && capacity != 0)) { return NEO_INVALID; }
    if (!display->attached || display->pixels == NULL) { return NEO_UNAVAILABLE; }
    if (capacity < display->size) { return NEO_LIMIT; }
    memcpy(bytes, display->pixels, display->size);
    *out_count = display->size; *out_width = display->width; *out_height = display->height; *out_frames = display->frames;
    return NEO_OK;
}
