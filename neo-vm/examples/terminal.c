#define _POSIX_C_SOURCE 200809L
#include "neo_execution.h"
#include "neo_image.h"
#include "neo_io_posix.h"
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Explicit terminal grants. No terminal modes or descriptor flags changed. */
int main(int argc, char **argv) {
    if (argc != 4) {
        fputs("usage: neo-io IMAGE ACTOR HANDLER\n", stderr);
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
    const neo_capability *root = NULL, *actor = NULL;
    neo_diagnostic error = {0};
    if (status == NEO_OK) { status = neo_image_parse(vm, source, &root, &error); }
    free(source);
    if (status != NEO_OK && error.message[0] != '\0') {
        fprintf(stderr, "%s:%zu:%zu: %s\n", argv[1], error.line, error.column, error.message);
    }
    if (status == NEO_OK) { status = neo_object_child(vm, root, argv[2], &actor); }
    const int descriptors[] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
    const char *names[] = {"stdin", "stdout", "stderr"};
    const char *objects[] = {"host-stdin", "host-stdout", "host-stderr"};
    for (size_t i = 0; status == NEO_OK && i < 3; ++i) {
        const neo_capability *stream = NULL;
        status = neo_stream_posix_create(vm, root, objects[i], descriptors[i], i == 0, i != 0, &stream);
        if (status == NEO_OK) {
            status = neo_object_connect(vm, actor, names[i], stream, i == 0 ? NEO_READ : NEO_WRITE);
        }
    }
    if (status == NEO_OK) {
        neo_execution execution = {0};
        status = neo_behavior_run(vm, actor, argv[3], NULL, 1000000, &execution);
        neo_execution_release(vm, &execution);
    }
    if (status != NEO_OK) { fprintf(stderr, "neo-io: %s\n", neo_status_name(status)); }
    neo_vm_destroy(vm);
    return status == NEO_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
