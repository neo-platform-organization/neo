#define _POSIX_C_SOURCE 200809L
#include "neo_execution.h"
#include "neo_image.h"
#include "neo_bootstrap.h"
#include "neo_window_x11.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Host bootstrap only: neo owns the frame handler and every pixel written.
 * This example supplies a fixed 640x480 buffer and an independently resizable
 * window. It neither interprets shapes nor draws them. */
static neo_status neo_load_template(neo_vm *vm, const neo_capability *actor, const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) { return NEO_IO_ERROR; }
    size_t limit = 1024u * 1024u;
    char *source = malloc(limit + 1);
    if (source == NULL) { (void)fclose(file); return NEO_OUT_OF_MEMORY; }
    size_t length = fread(source, 1, limit, file);
    bool invalid = fgetc(file) != EOF || ferror(file) != 0 || memchr(source, '\0', length) != NULL;
    if (fclose(file) != 0) { invalid = true; }
    source[length] = '\0';
    const neo_capability *prototype = NULL;
    neo_diagnostic error = {0};
    neo_status status = invalid ? NEO_INVALID : neo_image_parse(vm, source, &prototype, &error);
    free(source);
    if (status == NEO_OK) { status = neo_actor_install(vm, actor, prototype); }
    if (prototype != NULL) { (void)neo_image_unload(vm, prototype); }
    if (status != NEO_OK) { fprintf(stderr, "%s: %s %s\n", path, neo_status_name(status), error.message); }
    return status;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fputs("usage: neo-window IMAGE ACTOR [TEMPLATE...]\n", stderr);
        return EXIT_FAILURE;
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) { perror(argv[1]); return EXIT_FAILURE; }
    size_t limit = 1024u * 1024u;
    char *source = malloc(limit + 1);
    if (source == NULL) { (void)fclose(file); return EXIT_FAILURE; }
    size_t length = fread(source, 1, limit, file);
    bool invalid = fgetc(file) != EOF || ferror(file) != 0 || memchr(source, '\0', length) != NULL;
    if (fclose(file) != 0) { invalid = true; }
    source[length] = '\0';
    neo_vm *vm = NULL;
    neo_status status = invalid ? NEO_INVALID : neo_vm_create(NULL, &vm);
    const neo_capability *root = NULL, *actor = NULL, *buffer = NULL, *window = NULL;
    neo_diagnostic error = {0};
    if (status == NEO_OK) { status = neo_image_parse(vm, source, &root, &error); }
    free(source);
    if (status != NEO_OK && error.message[0] != '\0') {
        fprintf(stderr, "%s:%zu:%zu: %s\n", argv[1], error.line, error.column, error.message);
    }
    if (status == NEO_OK) { status = neo_object_child(vm, root, argv[2], &actor); }
    for (int i = 3; status == NEO_OK && i < argc; ++i) {
        status = neo_load_template(vm, actor, argv[i]);
    }
    if (status == NEO_OK) { status = neo_buffer_create(vm, root, "host-buffer", 640, 480, &buffer); }
    if (status == NEO_OK) {
        status = neo_window_x11_create(vm, root, "host-window", "neo", 640, 480, &window);
    }
    if (status == NEO_OK) {
        status = neo_object_connect(vm, actor, "buffer", buffer, NEO_READ | NEO_WRITE);
    }
    if (status == NEO_OK) {
        status = neo_object_connect(vm, actor, "window", window, NEO_READ | NEO_WRITE);
    }
    while (status == NEO_OK) {
        neo_window_state state;
        status = neo_window_poll(vm, window, &state);
        if (status != NEO_OK || state.closed) { break; }
        neo_execution execution = {0};
        status = neo_behavior_run(vm, actor, "frame", NULL, 1000000, &execution);
        neo_execution_release(vm, &execution);
        /* Example host avoids busy spinning; this is not a VM tick rule. */
        const struct timespec pause = {.tv_nsec = 16000000};
        (void)nanosleep(&pause, NULL);
    }
    if (status != NEO_OK) { fprintf(stderr, "neo-window: %s\n", neo_status_name(status)); }
    neo_vm_destroy(vm);
    return status == NEO_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
