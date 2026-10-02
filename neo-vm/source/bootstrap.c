#include "internal.h"
#include "neo_bootstrap.h"
#include <string.h>

/* Host reconstruction of inert template data across image boundaries.
 * Ordinary object-copy deliberately remains restricted to one image. */
static neo_status neo_template_copy(neo_vm *vm, const neo_capability *source,
                                    const neo_capability *parent, const char *name, size_t depth) {
    if (depth > 128) { return NEO_LIMIT; }
    neo_value value;
    neo_status status = neo_object_read(vm, source, &value);
    const neo_capability *copy = NULL;
    if (status == NEO_OK) { status = neo_object_create(vm, parent, name, value, &copy); }
    size_t count = 0;
    if (status == NEO_OK) { status = neo_object_child_count(vm, source, &count); }
    for (size_t i = 0; status == NEO_OK && i < count; ++i) {
        const neo_capability *child;
        const char *child_name;
        status = neo_object_child_at(vm, source, i, &child);
        if (status == NEO_OK) { status = neo_object_name(vm, child, &child_name); }
        if (status == NEO_OK) { status = neo_template_copy(vm, child, copy, child_name, depth + 1); }
    }
    return status;
}

neo_status neo_actor_install(neo_vm *vm, const neo_capability *actor,
                             const neo_capability *prototype) {
    neo_object *destination = NULL, *source = NULL;
    neo_status status = neo_resolve(vm, actor, NEO_READ | NEO_INSERT, &destination);
    if (status == NEO_OK) { status = neo_resolve(vm, prototype, NEO_READ | NEO_COPY, &source); }
    if (status != NEO_OK) { return status; }
    if (source->image == destination->image || neo_scheduler_contains(vm, destination->id)) {
        return NEO_CONFLICT;
    }
    for (neo_object *node = vm->objects; node != NULL; node = node->next) {
        if (neo_inside(vm, node, source->id) && (node->edges != NULL || node->display != NULL ||
            node->stream != NULL || node->ether || node->message != NULL || neo_scheduler_contains(vm, node->id))) { return NEO_UNSUPPORTED; }
    }
    size_t count;
    status = neo_object_child_count(vm, prototype, &count);
    for (size_t i = 0; status == NEO_OK && i < count; ++i) {
        const neo_capability *child;
        const char *name;
        status = neo_object_child_at(vm, prototype, i, &child);
        if (status == NEO_OK) { status = neo_object_name(vm, child, &name); }
        if (status != NEO_OK) { break; }
        const neo_capability *handlers = NULL;
        if (strcmp(name, "handlers") == 0) {
            status = neo_object_child(vm, actor, "handlers", &handlers);
            if (status == NEO_UNAVAILABLE) { status = NEO_OK; }
        }
        if (status != NEO_OK) { break; }
        if (handlers == NULL) { status = neo_template_copy(vm, child, actor, name, 1); }
        else {
            size_t methods;
            status = neo_object_child_count(vm, child, &methods);
            for (size_t j = 0; status == NEO_OK && j < methods; ++j) {
                const neo_capability *method;
                status = neo_object_child_at(vm, child, j, &method);
                if (status == NEO_OK) { status = neo_object_name(vm, method, &name); }
                if (status == NEO_OK) { status = neo_template_copy(vm, method, handlers, name, 1); }
            }
        }
    }
    return status;
}
