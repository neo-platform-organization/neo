#define _POSIX_C_SOURCE 200809L
#include "neo_platform.h"
#include "neo_io_posix.h"
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#ifdef NEO_PLATFORM_WITH_X11
#include "neo_window_x11.h"
#endif

neo_status neo_platform_native(unsigned services, neo_platform_info *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = (neo_platform_info){0};
    if ((services & ~((unsigned)NEO_PLATFORM_SERVICES_ALL)) != 0) { return NEO_INVALID; }
#if defined(__linux__) && defined(__x86_64__)
    *out = (neo_platform_info){
        .environment = NEO_ENVIRONMENT_HOSTED,
        .virtualization = NEO_VIRTUALIZATION_UNKNOWN,
        .backend = "linux-x86_64",
        .architecture = "x86_64",
        .host_os = "linux",
        .services = services
    };
    return NEO_OK;
#else
    return NEO_UNSUPPORTED;
#endif
}

static neo_status neo_linux_read_source(void *context, const char *location, char *bytes,
                                        size_t capacity, size_t *out_count) {
    (void)context;
    *out_count = 0;
    FILE *file = fopen(location, "rb");
    if (file == NULL) { return NEO_IO_ERROR; }
    size_t count = fread(bytes, 1, capacity, file);
    bool excess = fgetc(file) != EOF;
    bool failed = ferror(file) != 0;
    if (fclose(file) != 0) { failed = true; }
    if (failed) { return NEO_IO_ERROR; }
    if (excess) { return NEO_LIMIT; }
    *out_count = count;
    return NEO_OK;
}

static neo_status neo_linux_open_stream(void *context, neo_vm *vm, const neo_capability *parent,
                                        const char *name, neo_standard_stream stream,
                                        const neo_capability **out) {
    (void)context;
    int descriptor = stream == NEO_STANDARD_INPUT ? STDIN_FILENO :
        stream == NEO_STANDARD_OUTPUT ? STDOUT_FILENO : STDERR_FILENO;
    return neo_stream_posix_create(vm, parent, name, descriptor,
                                  stream == NEO_STANDARD_INPUT, stream != NEO_STANDARD_INPUT, out);
}

static neo_status neo_linux_wait(void *context, uint32_t milliseconds) {
    (void)context;
    struct timespec remaining = {.tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L};
    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) { return NEO_IO_ERROR; }
    }
    return NEO_OK;
}

#ifdef NEO_PLATFORM_WITH_X11
static neo_status neo_linux_open_window(void *context, neo_vm *vm, const neo_capability *parent,
                                        const char *name, const char *title,
                                        size_t width, size_t height, const neo_capability **out) {
    (void)context;
    return neo_window_x11_create(vm, parent, name, title, width, height, out);
}
#endif

neo_status neo_platform_native_services(unsigned requested, neo_platform_services *out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = (neo_platform_services){0};
    neo_platform_services selected = {0};
    neo_status status = neo_platform_native(requested, &selected.info);
    if (status != NEO_OK) { return status; }
#ifndef NEO_PLATFORM_WITH_X11
    if ((requested & (NEO_PLATFORM_PIXEL_WINDOWS | NEO_PLATFORM_INPUT_EVENTS)) != 0) {
        return NEO_UNSUPPORTED;
    }
#else
    if ((requested & NEO_PLATFORM_INPUT_EVENTS) != 0 && (requested & NEO_PLATFORM_PIXEL_WINDOWS) == 0) {
        return NEO_INVALID;
    }
    if ((requested & NEO_PLATFORM_PIXEL_WINDOWS) != 0) { selected.backend.open_window = neo_linux_open_window; }
#endif
    if ((requested & NEO_PLATFORM_HOST_FILES) != 0) { selected.backend.read_source = neo_linux_read_source; }
    if ((requested & NEO_PLATFORM_BYTE_STREAMS) != 0) { selected.backend.open_stream = neo_linux_open_stream; }
    if ((requested & NEO_PLATFORM_WAIT) != 0) { selected.backend.wait = neo_linux_wait; }
    *out = selected;
    return NEO_OK;
}
