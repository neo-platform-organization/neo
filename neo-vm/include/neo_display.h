#ifndef NEO_DISPLAY_H
#define NEO_DISPLAY_H

#include "neo.h"

/* Host-owned external resources, represented by ordinary capability-protected
 * objects. No native pointers or display connections are serialized. */
typedef struct neo_window_state {
    size_t width;
    size_t height;
    bool closed;
    bool redraw;
} neo_window_state;

/* Backend-independent events. Key names are symbolic ("a", "enter", etc.),
 * not platform keycodes. Text composition is a separate future capability. */
typedef enum neo_input_kind {
    NEO_INPUT_NONE, NEO_INPUT_CLOSE, NEO_INPUT_RESIZE, NEO_INPUT_EXPOSE,
    NEO_INPUT_FOCUS, NEO_INPUT_POINTER, NEO_INPUT_BUTTON, NEO_INPUT_KEY,
    NEO_INPUT_OVERFLOW
} neo_input_kind;
typedef struct neo_input_event {
    neo_input_kind kind;
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
    int64_t button; /* 1 primary, 2 middle, 3 secondary, 4/5 vertical wheel */
    bool pressed;  /* key/button down; focus gained */
    uint64_t lost; /* overflow count */
    char key[32];
} neo_input_event;
const char *neo_input_kind_name(neo_input_kind kind);

typedef struct neo_window_backend {
    /* Callbacks are synchronous, on the creating thread, and must not reenter
     * the VM. present must not retain pixels. poll is nonblocking. */
    neo_status (*present)(void *context, const uint8_t *pixels, size_t width, size_t height);
    neo_status (*poll)(void *context, neo_window_state *out_state);
    void (*destroy)(void *context);
    /* Optional: consume one queued event; NONE means no event. */
    neo_status (*next_event)(void *context, neo_input_event *out);
} neo_window_backend;

/* Packed RGBA8, top-left origin, rows in increasing y; zero initialized.
 * One buffer is limited to 64 MiB. The host chooses its dimensions. */
neo_status neo_buffer_create(neo_vm *vm, const neo_capability *parent, const char *name,
                             size_t width, size_t height, const neo_capability **out);
neo_status neo_buffer_write(neo_vm *vm, const neo_capability *buffer, size_t offset,
                            const uint8_t *bytes, size_t count);
neo_status neo_buffer_read(neo_vm *vm, const neo_capability *buffer, size_t offset,
                           uint8_t *bytes, size_t count);
/* Byte fill only: a memory operation, not a drawing algorithm. */
neo_status neo_buffer_fill(neo_vm *vm, const neo_capability *buffer, uint8_t value);
neo_status neo_buffer_size(neo_vm *vm, const neo_capability *buffer, size_t *out_size);

/* Copies callback table. Context ownership transfers ONLY on success; destroy
 * runs exactly once when its object/image/VM is deleted. Host grant, not an
 * operation available merely by writing a window-shaped object in an image. */
neo_status neo_window_create(neo_vm *vm, const neo_capability *parent, const char *name,
                             const neo_window_backend *backend, void *context,
                             const neo_capability **out);
/* Poll/present require WRITE on window; present requires READ on buffer.
 * Cached state requires READ; poll refreshes state. No implicit frame pacing. */
neo_status neo_window_poll(neo_vm *vm, const neo_capability *window, neo_window_state *out);
neo_status neo_window_get_state(neo_vm *vm, const neo_capability *window, neo_window_state *out);
neo_status neo_window_present(neo_vm *vm, const neo_capability *window,
                              const neo_capability *buffer);

/* Event consumption requires READ|WRITE; snapshot inspection requires READ.
 * Poll first to collect events. Snapshot is replaced only by successful next. */
neo_status neo_window_next_event(neo_vm *vm, const neo_capability *window, neo_input_event *out);
neo_status neo_window_get_event(neo_vm *vm, const neo_capability *window, neo_input_event *out);
/* Pixel-buffer properties stay separate from drawable/window dimensions. */
neo_status neo_buffer_dimensions(neo_vm *vm, const neo_capability *buffer,
                                 size_t *out_width, size_t *out_height);

#endif
