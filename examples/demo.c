#include "neo.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void neo_require(neo_status status) {
    if (status != NEO_OK) {
        fprintf(stderr, "neo: %s\n", neo_status_name(status));
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    neo_vm *vm = NULL;
    const neo_capability *plate, *cell, *food, *meal, *clone;
    neo_require(neo_vm_create(NULL, &vm));
    neo_require(neo_image_create(vm, "plate", &plate));
    neo_require(neo_object_create(vm, plate, "cell", (neo_value){.kind = NEO_OBJECT}, &cell));
    neo_require(neo_object_create(vm, plate, "food",
        (neo_value){.kind = NEO_INTEGER, .integer = 10}, &food));
    neo_object_id before, after;
    neo_require(neo_object_identity(vm, food, &before));
    neo_require(neo_object_move(vm, food, cell, "food", &meal));
    neo_require(neo_object_identity(vm, meal, &after));
    printf("Food moved by duplication: identity %" PRIu64 " -> %" PRIu64 "\n", before, after);
    neo_value value;
    printf("Old food connection: %s\n", neo_status_name(neo_object_read(vm, food, &value)));
    neo_require(neo_image_duplicate(vm, plate, "second-plate", &clone));
    neo_require(neo_image_unload(vm, plate));
    size_t count;
    neo_require(neo_object_child_count(vm, clone, &count));
    printf("Original image unloaded; duplicate still contains %zu cell\n", count);
    neo_vm_destroy(vm);
    return EXIT_SUCCESS;
}
