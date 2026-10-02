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

typedef struct neo_window_backend {
    /* Callbacks are synchronous, on the creating thread, and must not reenter
     * the VM. present must not retain pixels. poll is nonblocking. */
    neo_status (*present)(void *context, const uint8_t *pixels, size_t width, size_t height);
    neo_status (*poll)(void *context, neo_window_state *out_state);
    void (*destroy)(void *context);
} neo_window_backend;

/* Packed RGBA8, top-left origin, rows in increasing y; zero initialized.
 * One buffer is limited to 64 MiB. The host chooses its dimensions. */
neo_status neo_buffer_create(neo_vm *vm, const neo_capability *parent, const char *name,
                             size_t width, size_t height, const neo_capability **out);
neo_status neo_buffer_write(neo_vm *vm, const neo_capability *buffer, size_t offset,
                            const uint8_t *bytes, size_t count);
neo_status neo_buffer_read(neo_vm *vm, const neo_capability *buffer, size_t offset,
                           uint8_t *bytes, size_t count);
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

#endif
