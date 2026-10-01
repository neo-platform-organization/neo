#include "neo_message.h"

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
    const neo_capability *ether, *sender, *receiver;
    const neo_context *sender_context, *receiver_context;
    const neo_message *message, *active;
    neo_require(neo_ether_create(vm, clone, &ether));
    neo_require(neo_object_child(vm, clone, "cell", &receiver));
    neo_require(neo_object_create(vm, clone, "sender", (neo_value){0}, &sender));
    neo_require(neo_context_create(vm, sender, &sender_context));
    neo_require(neo_context_create(vm, receiver, &receiver_context));
    neo_require(neo_message_create(vm, sender_context, ether, receiver,
        (neo_value){.kind = NEO_TEXT, .text = "open"}, &message));
    neo_require(neo_message_submit(vm, sender_context, message));
    neo_require(neo_message_write(vm, sender_context, message, "",
        (neo_value){.kind = NEO_TEXT, .text = "close"}));
    neo_require(neo_message_accept(vm, receiver_context, message));
    printf("Sender edit after acceptance: %s\n", neo_status_name(
        neo_message_write(vm, sender_context, message, "",
            (neo_value){.kind = NEO_TEXT, .text = "open"})));
    neo_require(neo_message_begin(vm, receiver_context, &active));
    neo_require(neo_message_read(vm, receiver_context, active, "", &value));
    printf("Receiver reads accepted request: %s (embedding demo reads only)\n", value.text);
    neo_require(neo_message_finish(vm, receiver_context, active));
    neo_vm_destroy(vm);
    return EXIT_SUCCESS;
}
