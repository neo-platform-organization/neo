#define _POSIX_C_SOURCE 200809L
#include "neo_execution.h"
#include "neo_image.h"
#include "neo_window_x11.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Host bootstrap only: neo owns the frame handler and every pixel written.
 * This example supplies a fixed 640x480 buffer and an independently resizable
 * window. It neither interprets shapes nor draws them. */
int main(int argc, char **argv) {
    if (argc != 3) {
        fputs("usage: neo-window IMAGE ACTOR\n", stderr);
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
