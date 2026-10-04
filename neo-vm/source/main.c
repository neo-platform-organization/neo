#include "neo_execution.h"
#include "neo_image.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEO_CLI_SOURCE_LIMIT (1024u * 1024u)
#define NEO_DEFAULT_BUDGET 100000u

static void neo_usage(void) {
    fputs("usage:\n"
          "  neo check IMAGE...\n"
          "  neo format IMAGE\n"
          "  neo clone IMAGE\n"
          "  neo run IMAGE ACTOR HANDLER [STEP_BUDGET]\n"
          "  neo tick IMAGE TICKS [STEP_BUDGET]\n"
          "\nACTOR is an image child name, or '.' for the root.\n"
          "format and clone write graph text to stdout, never overwrite files.\n", stderr);
}

static neo_status neo_read_file(const char *path, char **out_source) {
    *out_source = NULL;
    FILE *file = fopen(path, "rb");
    if (file == NULL) { return NEO_IO_ERROR; }
    char *source = malloc(NEO_CLI_SOURCE_LIMIT + 1u);
    if (source == NULL) { (void)fclose(file); return NEO_OUT_OF_MEMORY; }
    size_t length = fread(source, 1, NEO_CLI_SOURCE_LIMIT, file);
    bool too_long = fgetc(file) != EOF;
    bool failed = ferror(file) != 0;
    if (fclose(file) != 0) { failed = true; }
    if (failed || too_long || memchr(source, '\0', length) != NULL) {
        free(source);
        return failed ? NEO_IO_ERROR : (too_long ? NEO_LIMIT : NEO_PARSE_ERROR);
    }
    source[length] = '\0';
    *out_source = source;
    return NEO_OK;
}

static neo_status neo_load(neo_vm *vm, const char *path, const neo_capability **out_root) {
    char *source = NULL;
    neo_status status = neo_read_file(path, &source);
    if (status != NEO_OK) {
        fprintf(stderr, "%s: %s\n", path, neo_status_name(status));
        return status;
    }
    neo_diagnostic error;
    status = neo_image_parse(vm, source, out_root, &error);
    free(source);
    if (status != NEO_OK) {
        fprintf(stderr, "%s:%zu:%zu: %s\n", path, error.line, error.column, error.message);
    }
    return status;
}

static bool neo_number_arg(const char *text, size_t *out) {
    char *end = NULL;
    errno = 0;
    uintmax_t number = strtoumax(text, &end, 10);
    if (*text == '-' || end == text || *end != '\0' || errno != 0 || number == 0 || number > SIZE_MAX) {
        return false;
    }
    *out = (size_t)number;
    return true;
}

static void neo_print_value(neo_value value) {
    switch (value.kind) {
        case NEO_OBJECT: printf("unit"); break;
        case NEO_INTEGER: printf("%" PRId64, value.integer); break;
        case NEO_BOOLEAN: printf("%s", value.boolean ? "true" : "false"); break;
        case NEO_TEXT: printf("\"%s\"", value.text); break;
        case NEO_INTEGERS:
            printf("[");
            for (size_t i = 0; i < value.count; ++i) { printf("%s%" PRId64, i == 0 ? "" : " ", value.integers[i]); }
            printf("]"); break;
        case NEO_PRIMITIVE: printf("primitive(%s)", value.text); break;
    }
}

static neo_status neo_print_state(neo_vm *vm, const neo_capability *root) {
    size_t count;
    neo_status status = neo_object_child_count(vm, root, &count);
    if (status != NEO_OK) { return status; }
    for (size_t i = 0; i < count; ++i) {
        const neo_capability *actor;
        status = neo_object_child_at(vm, root, i, &actor);
        if (status != NEO_OK) { return status; }
        const char *actor_name;
        status = neo_object_name(vm, actor, &actor_name);
        if (status != NEO_OK) { return status; }
        if (strcmp(actor_name, "ETHER") == 0) { continue; }
        size_t slots;
        status = neo_object_child_count(vm, actor, &slots);
        if (status != NEO_OK) { return status; }
        for (size_t j = 0; j < slots; ++j) {
            const neo_capability *slot;
            status = neo_object_child_at(vm, actor, j, &slot);
            if (status != NEO_OK) { return status; }
            const char *name;
            neo_value value;
            status = neo_object_name(vm, slot, &name);
            if (status != NEO_OK) { return status; }
            status = neo_object_read(vm, slot, &value);
            if (status != NEO_OK) { return status; }
            if (value.kind == NEO_OBJECT || value.kind == NEO_PRIMITIVE) { continue; }
            printf("%s.%s = ", actor_name, name);
            neo_print_value(value);
            putchar('\n');
        }
    }
    return NEO_OK;
}

/* An explicit host bootstrap policy, not automatic access in the language:
 * register direct image children with handlers; grant each the new ETHER.
 * Other cross-object SEND connections must be present in the loaded image. */
static neo_status neo_start(neo_vm *vm, const neo_capability *root) {
    const neo_capability *ether;
    neo_status status = neo_ether_create(vm, root, &ether);
    if (status != NEO_OK) { return status; }
    size_t count;
    status = neo_object_child_count(vm, root, &count);
    if (status != NEO_OK) { return status; }
    for (size_t i = 0; i < count; ++i) {
        const neo_capability *actor, *handlers;
        status = neo_object_child_at(vm, root, i, &actor);
        if (status != NEO_OK) { return status; }
        status = neo_object_child(vm, actor, "handlers", &handlers);
        if (status == NEO_UNAVAILABLE) { continue; }
        if (status != NEO_OK) { return status; }
        status = neo_object_connect(vm, actor, "ether", ether, NEO_READ);
        if (status != NEO_OK) { return status; }
        status = neo_scheduler_register(vm, actor);
        if (status != NEO_OK) { return status; }
    }
    return NEO_OK;
}

int main(int argc, char **argv) {
    if (argc < 3) { neo_usage(); return EXIT_FAILURE; }
    const char *command = argv[1];
    bool check = strcmp(command, "check") == 0;
    bool format = strcmp(command, "format") == 0;
    bool clone = strcmp(command, "clone") == 0;
    bool run = strcmp(command, "run") == 0;
    bool tick = strcmp(command, "tick") == 0;
    if ((!check && !format && !clone && !run && !tick) ||
        ((format || clone) && argc != 3) || (run && argc != 5 && argc != 6) ||
        (tick && argc != 4 && argc != 5)) {
        neo_usage(); return EXIT_FAILURE;
    }
    size_t budget = NEO_DEFAULT_BUDGET, ticks = 0;
    if ((run && argc == 6 && !neo_number_arg(argv[5], &budget)) ||
        (tick && (!neo_number_arg(argv[3], &ticks) || ticks > 10000)) ||
        (tick && argc == 5 && !neo_number_arg(argv[4], &budget))) {
        fputs("invalid positive budget or tick count (maximum 10000 ticks)\n", stderr);
        return EXIT_FAILURE;
    }
    neo_vm *vm = NULL;
    neo_status status = neo_vm_create(NULL, &vm);
    if (status != NEO_OK) { fprintf(stderr, "%s\n", neo_status_name(status)); return EXIT_FAILURE; }
    const neo_capability *root = NULL;
    for (int i = 2; i < (check ? argc : 3); ++i) {
        status = neo_load(vm, argv[i], &root);
        if (status != NEO_OK) { neo_vm_destroy(vm); return EXIT_FAILURE; }
        if (check) { printf("%s: valid image (loaded, not executed)\n", argv[i]); }
    }
    if (format || clone) {
        if (clone) { status = neo_image_duplicate(vm, root, "image-copy", &root); }
        char *text = NULL;
        if (status == NEO_OK) { status = neo_image_format(vm, root, &text); }
        if (status == NEO_OK) { fputs(text, stdout); }
        neo_image_text_free(vm, text);
    } else if (run) {
        const neo_capability *actor = root;
        if (strcmp(argv[3], ".") != 0) { status = neo_object_child(vm, root, argv[3], &actor); }
        neo_execution report = {0};
        if (status == NEO_OK) { status = neo_behavior_run(vm, actor, argv[4], NULL, budget, &report); }
        if (status == NEO_OK) {
            printf("result = "); neo_print_value(report.result); putchar('\n');
            status = neo_print_state(vm, root);
        } else {
            fprintf(stderr, "receiver %" PRIu64 ", operation %" PRIu64 ", after %zu steps\n",
                    report.receiver, report.operation, report.steps);
        }
        neo_execution_release(vm, &report);
    } else if (tick) {
        status = neo_start(vm, root);
        for (size_t i = 0; status == NEO_OK && i < ticks; ++i) {
            size_t turns;
            neo_execution failure;
            status = neo_scheduler_tick(vm, budget, &turns, &failure);
            if (status == NEO_OK) { printf("tick %zu: %zu turns\n", i + 1, turns); }
            else {
                fprintf(stderr, "tick %zu failed at receiver %" PRIu64 ", operation %" PRIu64 "\n",
                        i + 1, failure.receiver, failure.operation);
            }
            neo_execution_release(vm, &failure);
        }
        if (status == NEO_OK) { status = neo_print_state(vm, root); }
    }
    if (status != NEO_OK) { fprintf(stderr, "neo: %s\n", neo_status_name(status)); }
    if (fflush(stdout) == EOF) { status = NEO_IO_ERROR; }
    neo_vm_destroy(vm);
    return status == NEO_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
