#ifndef NEO_DISPLAY_H
#define NEO_DISPLAY_H

#include "objects/objects.h"

/** Host-owned external resources, represented by ordinary capability-protected
 * objects. No native pointers or display connections are serialized. */
typedef struct neo_window_state {
    size_t width;
    size_t height;
    bool closed;
    bool redraw;
    bool focused;
    bool captured;
    uint64_t input_epoch;
    uint32_t scale_numerator, scale_denominator; /** Drawable pixels per layout unit; 0/0 means legacy 1/1. */
    bool presentation_lost; /** Recoverable presentation unavailability; not closure. */
    uint64_t surface_epoch; /** Scale/availability generation, bounded by INT64_MAX. */
} neo_window_state;

/** Backend-independent events. Key names are symbolic ("a", "enter", etc.),
 * not platform keycodes. Committed UTF-8 text is separate; IME preedit remains future work. */
typedef enum neo_input_kind {
    NEO_INPUT_NONE, NEO_INPUT_CLOSE, NEO_INPUT_RESIZE, NEO_INPUT_EXPOSE,
    NEO_INPUT_FOCUS, NEO_INPUT_POINTER, NEO_INPUT_BUTTON, NEO_INPUT_KEY,
    NEO_INPUT_OVERFLOW, NEO_INPUT_RESET, NEO_INPUT_CAPTURE, NEO_INPUT_TEXT, NEO_INPUT_SURFACE,
    NEO_INPUT_PINCH /** x/y anchor; positive width/height incremental scale ratio. */
} neo_input_kind;
typedef struct neo_input_event {
    neo_input_kind kind;
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
    int64_t button; /** 1 primary, 2 middle, 3 secondary, 4/5 vertical wheel */
    bool pressed;  /** key/button down; focus gained */
    bool control;
    bool shift;
    uint64_t epoch; /** Input generation; stale queued events are invalidated. */
    uint64_t lost; /** overflow count */
    char key[32];
    char text[64]; /** Valid committed UTF-8; at most63 bytes, no C0/DEL. */
} neo_input_event;
const char *neo_input_kind_name(neo_input_kind kind);
/** Provider helper: invalidate held input; saturation closes the resource. */
void neo_window_input_reset(neo_window_state *state);
/** Provider helper: atomically publish scale/availability, increment generation
 * only on change. Positive ratio required; LIMIT on generation exhaustion. */
neo_status neo_window_surface_change(neo_window_state *state, uint32_t numerator,
                                      uint32_t denominator, bool lost);

typedef struct neo_window_backend {
    /** Callbacks are synchronous, on the creating thread, and must not reenter
     * the VM. present must not retain pixels. poll is nonblocking. */
    neo_status (*present)(void *context, const uint8_t *pixels, size_t width, size_t height);
    neo_status (*poll)(void *context, neo_window_state *out_state);
    void (*destroy)(void *context);
    /** Optional: consume one queued event; NONE means no event. */
    neo_status (*next_event)(void *context, neo_input_event *out);
    /** Optional explicit capture. Return latest state; do not run behavior. */
    neo_status (*capture)(void *context, bool enabled, neo_window_state *out);
} neo_window_backend;

/** Packed RGBA8, top-left origin, rows in increasing y; zero initialized.
 * One buffer is limited to 64 MiB. The host chooses its dimensions. */
neo_status neo_buffer_create(neo_vm *vm, const neo_capability *parent, const char *name,
                             size_t width, size_t height, const neo_capability **out);
neo_status neo_buffer_write(neo_vm *vm, const neo_capability *buffer, size_t offset,
                            const uint8_t *bytes, size_t count);
neo_status neo_buffer_read(neo_vm *vm, const neo_capability *buffer, size_t offset,
                           uint8_t *bytes, size_t count);
/** Byte fill only: a memory operation, not a drawing algorithm. */
neo_status neo_buffer_fill(neo_vm *vm, const neo_capability *buffer, uint8_t value);
neo_status neo_buffer_size(neo_vm *vm, const neo_capability *buffer, size_t *out_size);

/** Copies callback table. Context ownership transfers ONLY on success; destroy
 * runs exactly once when its object/image/VM is deleted. Host grant, not an
 * operation available merely by writing a window-shaped object in an image. */
neo_status neo_window_create(neo_vm *vm, const neo_capability *parent, const char *name,
                             const neo_window_backend *backend, void *context,
                             const neo_capability **out);
/** Poll/present require WRITE on window; present requires READ on buffer.
 * Cached state requires READ; poll refreshes state. No implicit frame pacing. */
neo_status neo_window_poll(neo_vm *vm, const neo_capability *window, neo_window_state *out);
/** WRITE required. Optional backend; enabling requires live focus. Releasing an
 * owned capture cancels input via epoch reset; releasing none is idempotent. */
neo_status neo_window_capture(neo_vm *vm, const neo_capability *window, bool enabled);
neo_status neo_window_get_state(neo_vm *vm, const neo_capability *window, neo_window_state *out);
neo_status neo_window_present(neo_vm *vm, const neo_capability *window,
                              const neo_capability *buffer);

/** Event consumption requires READ|WRITE; snapshot inspection requires READ.
 * Poll first to collect events/state. Epoch changes produce an unqueued RESET
 * before consumption; older queued events are discarded with bounded work.
 * Snapshot is replaced only by successful next. */
neo_status neo_window_next_event(neo_vm *vm, const neo_capability *window, neo_input_event *out);
neo_status neo_window_get_event(neo_vm *vm, const neo_capability *window, neo_input_event *out);
/** Pixel-buffer properties stay separate from drawable/window dimensions. */
neo_status neo_buffer_dimensions(neo_vm *vm, const neo_capability *buffer,
                                 size_t *out_width, size_t *out_height);

/** Image-created private display endpoint. ACT is controller authority; apps
 * receive only READ|WRITE and cannot inject events or change focus themselves.
 * No native window is opened. Resource lifetime follows ordinary object deletion. */
neo_status neo_surface_create(neo_vm *vm, const neo_capability *parent, const char *name,
                              size_t width, size_t height, const neo_capability **out);
neo_status neo_surface_focus(neo_vm *vm, const neo_capability *surface, bool focused);
neo_status neo_surface_route(neo_vm *vm, const neo_capability *surface,
                             const neo_capability *source, int64_t x, int64_t y);
/** Raw rectangular RGBA copy, clipped to destination. Source and destination
 * must be different resources. No scaling, blending, stacking or window policy. */
neo_status neo_buffer_copy_region(neo_vm *vm, const neo_capability *destination,
                                  const neo_capability *source, int64_t x, int64_t y,
                                  size_t width, size_t height);
/** Generic nearest-neighbour buffer transfer; the image supplies both extents.
 * Same rights/lifetime contract as raw copy, with no viewport/window policy. */
neo_status neo_buffer_scale_copy(neo_vm *vm, const neo_capability *destination,
                                 const neo_capability *source, int64_t x, int64_t y,
                                 size_t width, size_t height, size_t output_width, size_t output_height);
neo_status neo_buffer_transfer_work(neo_vm *vm, const neo_capability *destination,
                                    int64_t x, int64_t y, size_t width, size_t height, size_t *out);

#endif
