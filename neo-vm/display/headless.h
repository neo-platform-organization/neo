#ifndef NEO_HEADLESS_H
#define NEO_HEADLESS_H
#include "display/display.h"

#define NEO_HEADLESS_EVENT_CAPACITY 64u

typedef struct neo_headless_display neo_headless_display;
/** Trusted host/test provider using the ordinary protected window interface.
 * One creating thread; no OS display, event loop, behavior or implicit grants.
 * On success caller owns one controller reference and window owns another.
 * Release the caller reference exactly once; the window can then outlive it.
 * If window dies first, held controller calls report UNAVAILABLE until release.
 * Controller storage is independent of VM lifetime; handles are never serialized. */
neo_status neo_headless_open(neo_vm *vm, const neo_capability *parent, const char *name,
                             size_t width, size_t height, neo_headless_display **out_control,
                             const neo_capability **out_window);
void neo_headless_release(neo_headless_display *display);
/** Inject one existing normalized event. NONE/OVERFLOW are provider-generated.
 * Full queue drops newest event, counts loss and returns LIMIT; latest resize/
 * close state is still applied. Overflow is reported before retained FIFO events.
 * Focus/capture loss, overflow and close invalidate old input epochs. Core
 * delivery prioritizes its synthetic reset and suppresses stale queued events.
 * CAPTURE/pressed=false simulates loss; acquisition uses window capture API.
 * TEXT requires nonempty valid UTF-8 in text[64], excluding C0 controls/DEL.
 * Close is sticky; further injection/presentation is unavailable.
 * Resize can be0x0 and does not resize any presented pixel buffer. */
neo_status neo_headless_push(neo_headless_display *display, const neo_input_event *event);
/** Simulate presentation availability. Only OK and UNAVAILABLE are accepted.
 * Failed presentation retains last successful pixels/dimensions/frame count. */
neo_status neo_headless_present_status(neo_headless_display *display, neo_status status);
/** Set positive pixels/layout-unit ratio; does not resize buffers or drawable.
 * Scale and presentation availability generate a coalesced core surface event. */
neo_status neo_headless_scale(neo_headless_display *display, uint32_t numerator, uint32_t denominator);
/** Independent snapshot copy, never a borrowed pointer. Zero outputs on failure;
 * insufficient capacity returns LIMIT without partial bytes. Before first frame
 * or after window destruction returns UNAVAILABLE. Frame dimensions describe the
 * presented RGBA buffer, separately from drawable state returned by window poll. */
neo_status neo_headless_frame(neo_headless_display *display, uint8_t *bytes, size_t capacity,
                              size_t *out_count, size_t *out_width, size_t *out_height,
                              uint64_t *out_frames);
#endif
