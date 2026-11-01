#ifndef NEO_WIN32_H
#define NEO_WIN32_H
#include "platform/platform.h"
/** Hidden-window native-message checks; no foreground input injection. */
bool neo_win32_window_selftest(neo_vm *vm);
neo_status neo_win32_open_clipboard(void *context, neo_vm *vm, const neo_capability *parent,
                                    const char *name, const neo_capability **out);
neo_status neo_win32_open_stream(void *context, neo_vm *vm, const neo_capability *parent,
                                const char *name, neo_standard_stream stream,
                                const neo_capability **out);
neo_status neo_win32_open_window(void *context, neo_vm *vm, const neo_capability *parent,
                                const char *name, const char *title, size_t width,
                                size_t height, const neo_capability **out);
#endif
