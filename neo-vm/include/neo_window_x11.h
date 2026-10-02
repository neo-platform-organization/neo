#ifndef NEO_WINDOW_X11_H
#define NEO_WINDOW_X11_H

#include "neo_display.h"

/* Optional Xlib adapter. Uses the host's DISPLAY, on the creating thread.
 * TrueColor visuals only. RGB is displayed; alpha is retained in the buffer
 * but ignored by this opaque window. Pixels are copied 1:1, without scaling. */
neo_status neo_window_x11_create(neo_vm *vm, const neo_capability *parent, const char *name,
                                 const char *title, size_t width, size_t height,
                                 const neo_capability **out);

#endif
