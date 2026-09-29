#include "neo.h"

#include <stdlib.h>
#include <string.h>

typedef struct neo_edge {
    char *name;
    neo_object_id target;
    unsigned rights;
    struct neo_edge *next;
} neo_edge;

typedef struct neo_object {
    neo_object_id id;
    neo_object_id image;
    neo_object_id parent;
    char *name;
    neo_value value;
    neo_edge *edges;
    bool deleting;
    struct neo_object *next;
} neo_object;

struct neo_capability {
    neo_object_id target;
    unsigned rights;
    struct neo_capability *next;
};

struct neo_vm {
    neo_allocator allocator;
    neo_object_id next_id;
    neo_object *objects;
    neo_capability *capabilities;
};

typedef struct neo_mapping {
    neo_object *source;
    neo_object *copy;
} neo_mapping;

static void *neo_default_allocate(void *context, size_t size) {
    (void)context;
    return malloc(size);
}

static void neo_default_release(void *context, void *memory) {
    (void)context;
    free(memory);
}

static void *neo_alloc(neo_vm *vm, size_t size) {
    void *memory = vm->allocator.allocate(vm->allocator.context, size);
    if (memory != NULL) {
        memset(memory, 0, size);
    }
    return memory;
}

static void neo_free(neo_vm *vm, void *memory) {
    if (memory != NULL) {
        vm->allocator.release(vm->allocator.context, memory);
    }
}

static char *neo_string(neo_vm *vm, const char *source) {
    size_t size = strlen(source);
    if (size == SIZE_MAX) {
        return NULL;
    }
    char *copy = neo_alloc(vm, size + 1);
    if (copy != NULL) {
        memcpy(copy, source, size + 1);
    }
    return copy;
}

static bool neo_valid_value(neo_value value) {
    switch (value.kind) {
        case NEO_OBJECT:
        case NEO_INTEGER:
        case NEO_BOOLEAN:
            return true;
        case NEO_TEXT:
        case NEO_PRIMITIVE:
            return value.text != NULL;
    }
    return false;
}

static neo_status neo_value_copy(neo_vm *vm, neo_value source, neo_value *out) {
    *out = (neo_value){.kind = source.kind};
    switch (source.kind) {
        case NEO_INTEGER:
            out->integer = source.integer;
            break;
        case NEO_BOOLEAN:
            out->boolean = source.boolean;
            break;
        case NEO_TEXT:
        case NEO_PRIMITIVE:
            out->text = neo_string(vm, source.text);
            if (out->text == NULL) {
                return NEO_OUT_OF_MEMORY;
            }
            break;
        case NEO_OBJECT:
            break;
    }
    return NEO_OK;
}

static void neo_value_free(neo_vm *vm, neo_value value) {
    if (value.kind == NEO_TEXT || value.kind == NEO_PRIMITIVE) {
        neo_free(vm, (void *)value.text);
    }
}

static neo_object *neo_lookup(neo_vm *vm, neo_object_id id) {
    for (neo_object *object = vm->objects; object != NULL; object = object->next) {
        if (object->id == id) {
            return object;
        }
    }
    return NULL;
}

static neo_status neo_resolve(neo_vm *vm, const neo_capability *cap,
                               unsigned rights, neo_object **out) {
    if (vm == NULL || cap == NULL) {
        return NEO_INVALID;
    }
    /* Compare before dereferencing caller-supplied capability pointers. */
    const neo_capability *known = vm->capabilities;
    while (known != NULL && known != cap) {
        known = known->next;
    }
    if (known == NULL) {
        return NEO_DENIED;
    }
    if ((rights & known->rights) != rights) {
        return NEO_DENIED;
    }
    *out = neo_lookup(vm, known->target);
    return *out == NULL ? NEO_UNAVAILABLE : NEO_OK;
}

static neo_capability *neo_cap_new(neo_vm *vm, neo_object_id target, unsigned rights) {
    neo_capability *cap = neo_alloc(vm, sizeof(*cap));
    if (cap != NULL) {
        cap->target = target;
        cap->rights = rights;
    }
    return cap;
}

static void neo_cap_publish(neo_vm *vm, neo_capability *cap) {
    cap->next = vm->capabilities;
    vm->capabilities = cap;
}

static neo_status neo_issue(neo_vm *vm, neo_object_id id, unsigned rights,
                            const neo_capability **out) {
    neo_capability *cap = neo_cap_new(vm, id, rights);
    if (cap == NULL) {
        return NEO_OUT_OF_MEMORY;
    }
    neo_cap_publish(vm, cap);
    *out = cap;
    return NEO_OK;
}

static void neo_object_free(neo_vm *vm, neo_object *object) {
    neo_edge *edge = object->edges;
    while (edge != NULL) {
        neo_edge *next = edge->next;
        neo_free(vm, edge->name);
        neo_free(vm, edge);
        edge = next;
    }
    neo_value_free(vm, object->value);
    neo_free(vm, object->name);
    neo_free(vm, object);
}

static neo_status neo_node_new(neo_vm *vm, const char *name, neo_value value,
                               neo_object **out) {
    if (vm->next_id == UINT64_MAX) {
        return NEO_LIMIT;
    }
    neo_object *object = neo_alloc(vm, sizeof(*object));
    if (object == NULL) {
        return NEO_OUT_OF_MEMORY;
    }
    object->name = neo_string(vm, name);
    neo_status status = neo_value_copy(vm, value, &object->value);
    if (object->name == NULL || status != NEO_OK) {
        neo_object_free(vm, object);
        return NEO_OUT_OF_MEMORY;
    }
    object->id = vm->next_id++;
    *out = object;
    return NEO_OK;
}

static bool neo_name_exists(neo_vm *vm, neo_object_id parent, const char *name) {
    for (neo_object *node = vm->objects; node != NULL; node = node->next) {
        if (node->parent == parent && strcmp(node->name, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool neo_inside(neo_vm *vm, neo_object *node, neo_object_id ancestor) {
    while (node != NULL) {
        if (node->id == ancestor) {
            return true;
        }
        node = neo_lookup(vm, node->parent);
    }
    return false;
}

/* Mark first while all ancestry exists; unlink the complete region from the
 * environment listing before freeing any of its nodes. No callbacks or yields. */
static void neo_delete_region(neo_vm *vm, neo_object_id root) {
    for (neo_object *node = vm->objects; node != NULL; node = node->next) {
        node->deleting = neo_inside(vm, node, root);
    }
    neo_object *garbage = NULL;
    neo_object **cursor = &vm->objects;
    while (*cursor != NULL) {
        neo_object *node = *cursor;
        if (node->deleting) {
            *cursor = node->next;
            node->next = garbage;
            garbage = node;
        } else {
            cursor = &node->next;
        }
    }
    while (garbage != NULL) {
        neo_object *next = garbage->next;
        neo_object_free(vm, garbage);
        garbage = next;
    }
}

neo_status neo_vm_create(const neo_allocator *allocator, neo_vm **out_vm) {
    if (out_vm == NULL) {
        return NEO_INVALID;
    }
    *out_vm = NULL;
    neo_allocator selected = {NULL, neo_default_allocate, neo_default_release};
    if (allocator != NULL) {
        if (allocator->allocate == NULL || allocator->release == NULL) {
            return NEO_INVALID;
        }
        selected = *allocator;
    }
    neo_vm *vm = selected.allocate(selected.context, sizeof(*vm));
    if (vm == NULL) {
        return NEO_OUT_OF_MEMORY;
    }
    *vm = (neo_vm){.allocator = selected, .next_id = 1};
    *out_vm = vm;
    return NEO_OK;
}

void neo_vm_destroy(neo_vm *vm) {
    if (vm == NULL) {
        return;
    }
    while (vm->objects != NULL) {
        neo_object *next = vm->objects->next;
        neo_object_free(vm, vm->objects);
        vm->objects = next;
    }
    while (vm->capabilities != NULL) {
        neo_capability *next = vm->capabilities->next;
        neo_free(vm, vm->capabilities);
        vm->capabilities = next;
    }
    neo_allocator allocator = vm->allocator;
    allocator.release(allocator.context, vm);
}

const char *neo_status_name(neo_status status) {
    switch (status) {
        case NEO_OK: return "ok";
        case NEO_INVALID: return "invalid argument";
        case NEO_UNAVAILABLE: return "unavailable";
        case NEO_DENIED: return "authority denied";
        case NEO_OUT_OF_MEMORY: return "out of memory";
        case NEO_CONFLICT: return "name conflict";
        case NEO_CYCLE: return "destination inside source";
        case NEO_WRONG_IMAGE: return "wrong image";
        case NEO_WRONG_KIND: return "wrong kind";
        case NEO_LIMIT: return "identity limit";
    }
    return "unknown status";
}

neo_status neo_image_create(neo_vm *vm, const char *name,
                            const neo_capability **out_root) {
    if (out_root == NULL) {
        return NEO_INVALID;
    }
    *out_root = NULL;
    if (vm == NULL || name == NULL || name[0] == '\0') {
        return NEO_INVALID;
    }
    neo_object *root = NULL;
    neo_status status = neo_node_new(vm, name, (neo_value){.kind = NEO_OBJECT}, &root);
    if (status != NEO_OK) {
        return status;
    }
    neo_capability *cap = neo_cap_new(vm, root->id, NEO_ALL);
    if (cap == NULL) {
        neo_object_free(vm, root);
        return NEO_OUT_OF_MEMORY;
    }
    root->image = root->id;
    root->next = vm->objects;
    vm->objects = root;
    neo_cap_publish(vm, cap);
    *out_root = cap;
    return NEO_OK;
}

neo_status neo_capability_restrict(neo_vm *vm, const neo_capability *source,
                                   unsigned rights, const neo_capability **out_cap) {
    if (out_cap == NULL) {
        return NEO_INVALID;
    }
    *out_cap = NULL;
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, source, 0, &node);
    if (status != NEO_OK) {
        return status;
    }
    if ((rights & ~source->rights) != 0) {
        return NEO_DENIED;
    }
    return neo_issue(vm, node->id, rights, out_cap);
}

neo_status neo_object_create(neo_vm *vm, const neo_capability *parent,
                             const char *name, neo_value value,
                             const neo_capability **out_object) {
    if (out_object == NULL) {
        return NEO_INVALID;
    }
    *out_object = NULL;
    if (name == NULL || name[0] == '\0' || !neo_valid_value(value)) {
        return NEO_INVALID;
    }
    neo_object *container = NULL;
    neo_status status = neo_resolve(vm, parent, NEO_INSERT, &container);
    if (status != NEO_OK) {
        return status;
    }
    if (neo_name_exists(vm, container->id, name)) {
        return NEO_CONFLICT;
    }
    neo_object *node = NULL;
    status = neo_node_new(vm, name, value, &node);
    if (status != NEO_OK) {
        return status;
    }
    neo_capability *cap = neo_cap_new(vm, node->id, parent->rights);
    if (cap == NULL) {
        neo_object_free(vm, node);
        return NEO_OUT_OF_MEMORY;
    }
    node->parent = container->id;
    node->image = container->image;
    node->next = vm->objects;
    vm->objects = node;
    neo_cap_publish(vm, cap);
    *out_object = cap;
    return NEO_OK;
}

neo_status neo_object_child(neo_vm *vm, const neo_capability *parent,
                            const char *name, const neo_capability **out_child) {
    if (out_child == NULL) {
        return NEO_INVALID;
    }
    *out_child = NULL;
    if (name == NULL) {
        return NEO_INVALID;
    }
    neo_object *container = NULL;
    neo_status status = neo_resolve(vm, parent, NEO_READ, &container);
    if (status != NEO_OK) {
        return status;
    }
    for (neo_object *node = vm->objects; node != NULL; node = node->next) {
        if (node->parent == container->id && strcmp(node->name, name) == 0) {
            return neo_issue(vm, node->id, parent->rights, out_child);
        }
    }
    return NEO_UNAVAILABLE;
}

neo_status neo_object_read(neo_vm *vm, const neo_capability *object,
                           neo_value *out_value) {
    if (out_value == NULL) {
        return NEO_INVALID;
    }
    *out_value = (neo_value){0};
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, object, NEO_READ, &node);
    if (status == NEO_OK) {
        *out_value = node->value;
    }
    return status;
}

neo_status neo_object_write(neo_vm *vm, const neo_capability *object, neo_value value) {
    if (!neo_valid_value(value)) {
        return NEO_INVALID;
    }
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, object, NEO_WRITE, &node);
    if (status != NEO_OK) {
        return status;
    }
    neo_value replacement;
    status = neo_value_copy(vm, value, &replacement);
    if (status == NEO_OK) {
        neo_value_free(vm, node->value);
        node->value = replacement;
    }
    return status;
}

neo_status neo_object_identity(neo_vm *vm, const neo_capability *object,
                               neo_object_id *out_id) {
    if (out_id == NULL) {
        return NEO_INVALID;
    }
    *out_id = 0;
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, object, NEO_READ, &node);
    if (status == NEO_OK) {
        *out_id = node->id;
    }
    return status;
}

neo_status neo_object_child_count(neo_vm *vm, const neo_capability *object,
                                  size_t *out_count) {
    if (out_count == NULL) {
        return NEO_INVALID;
    }
    *out_count = 0;
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, object, NEO_READ, &node);
    if (status != NEO_OK) {
        return status;
    }
    for (neo_object *child = vm->objects; child != NULL; child = child->next) {
        if (child->parent == node->id) {
            ++*out_count;
        }
    }
    return NEO_OK;
}

neo_status neo_object_connect(neo_vm *vm, const neo_capability *object,
                              const char *name, const neo_capability *target,
                              unsigned rights) {
    if (name == NULL || name[0] == '\0') {
        return NEO_INVALID;
    }
    neo_object *node = NULL;
    neo_object *other = NULL;
    neo_status status = neo_resolve(vm, object, NEO_WRITE, &node);
    if (status != NEO_OK) {
        return status;
    }
    status = neo_resolve(vm, target, NEO_DELEGATE, &other);
    if (status != NEO_OK) {
        return status;
    }
    if ((rights & ~target->rights) != 0) {
        return NEO_DENIED;
    }
    if (node->image != other->image) {
        return NEO_WRONG_IMAGE;
    }
    for (neo_edge *edge = node->edges; edge != NULL; edge = edge->next) {
        if (strcmp(edge->name, name) == 0) {
            return NEO_CONFLICT;
        }
    }
    neo_edge *edge = neo_alloc(vm, sizeof(*edge));
    if (edge == NULL) {
        return NEO_OUT_OF_MEMORY;
    }
    edge->name = neo_string(vm, name);
    if (edge->name == NULL) {
        neo_free(vm, edge);
        return NEO_OUT_OF_MEMORY;
    }
    edge->target = other->id;
    edge->rights = rights;
    edge->next = node->edges;
    node->edges = edge;
    return NEO_OK;
}

neo_status neo_object_connection(neo_vm *vm, const neo_capability *object,
                                 const char *name, const neo_capability **out_target) {
    if (out_target == NULL) {
        return NEO_INVALID;
    }
    *out_target = NULL;
    if (name == NULL) {
        return NEO_INVALID;
    }
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, object, NEO_READ, &node);
    if (status != NEO_OK) {
        return status;
    }
    for (neo_edge *edge = node->edges; edge != NULL; edge = edge->next) {
        if (strcmp(edge->name, name) == 0) {
            if (neo_lookup(vm, edge->target) == NULL) {
                return NEO_UNAVAILABLE;
            }
            return neo_issue(vm, edge->target, edge->rights, out_target);
        }
    }
    return NEO_UNAVAILABLE;
}

static neo_object_id neo_remap(neo_mapping *map, size_t count, neo_object_id id) {
    for (size_t i = 0; i < count; ++i) {
        if (map[i].source->id == id) {
            return map[i].copy->id;
        }
    }
    return id;
}

/* Build entirely off-list, then publish with no remaining fallible steps. */
static neo_status neo_duplicate(neo_vm *vm, neo_object *source,
                                 neo_object *destination, const char *name,
                                 unsigned rights, const neo_capability **out) {
    size_t count = 0;
    for (neo_object *node = vm->objects; node != NULL; node = node->next) {
        if (neo_inside(vm, node, source->id)) {
            ++count;
        }
    }
    if (count > SIZE_MAX / sizeof(neo_mapping)) {
        return NEO_LIMIT;
    }
    neo_mapping *map = neo_alloc(vm, count * sizeof(*map));
    if (map == NULL) {
        return NEO_OUT_OF_MEMORY;
    }
    size_t built = 0;
    neo_object *root = NULL;
    neo_capability *cap = NULL;
    neo_status status = NEO_OK;
    for (neo_object *node = vm->objects; node != NULL; node = node->next) {
        if (!neo_inside(vm, node, source->id)) {
            continue;
        }
        neo_object *copy = NULL;
        status = neo_node_new(vm, node == source ? name : node->name, node->value, &copy);
        if (status != NEO_OK) {
            goto cleanup;
        }
        map[built++] = (neo_mapping){node, copy};
        if (node == source) {
            root = copy;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        neo_object *copy = map[i].copy;
        neo_object *original = map[i].source;
        copy->image = destination == NULL ? root->id : destination->image;
        copy->parent = original == source
            ? (destination == NULL ? 0 : destination->id)
            : neo_remap(map, count, original->parent);
        for (neo_edge *edge = original->edges; edge != NULL; edge = edge->next) {
            neo_edge *new_edge = neo_alloc(vm, sizeof(*new_edge));
            if (new_edge == NULL) {
                status = NEO_OUT_OF_MEMORY;
                goto cleanup;
            }
            new_edge->next = copy->edges;
            copy->edges = new_edge;
            new_edge->name = neo_string(vm, edge->name);
            if (new_edge->name == NULL) {
                status = NEO_OUT_OF_MEMORY;
                goto cleanup;
            }
            new_edge->target = neo_remap(map, count, edge->target);
            new_edge->rights = edge->rights;
        }
    }
    cap = neo_cap_new(vm, root->id, rights);
    if (cap == NULL) {
        status = NEO_OUT_OF_MEMORY;
        goto cleanup;
    }
    for (size_t i = 0; i < count; ++i) {
        map[i].copy->next = vm->objects;
        vm->objects = map[i].copy;
    }
    neo_cap_publish(vm, cap);
    *out = cap;
    neo_free(vm, map);
    return NEO_OK;

cleanup:
    for (size_t i = 0; i < built; ++i) {
        neo_object_free(vm, map[i].copy);
    }
    neo_free(vm, map);
    return status;
}

static neo_status neo_transfer(neo_vm *vm, const neo_capability *source,
                                const neo_capability *destination, const char *name,
                                bool moving, const neo_capability **out) {
    if (out == NULL) {
        return NEO_INVALID;
    }
    *out = NULL;
    if (name == NULL || name[0] == '\0') {
        return NEO_INVALID;
    }
    neo_object *original = NULL;
    neo_object *container = NULL;
    unsigned required = NEO_READ | NEO_COPY | (moving ? NEO_DELETE : 0u);
    neo_status status = neo_resolve(vm, source, required, &original);
    if (status != NEO_OK) {
        return status;
    }
    status = neo_resolve(vm, destination, NEO_INSERT, &container);
    if (status != NEO_OK) {
        return status;
    }
    if (original->image != container->image) {
        return NEO_WRONG_IMAGE;
    }
    if (neo_inside(vm, container, original->id)) {
        return NEO_CYCLE;
    }
    if (original->parent == 0) {
        return NEO_DENIED;
    }
    if (neo_name_exists(vm, container->id, name)) {
        return NEO_CONFLICT;
    }
    status = neo_duplicate(vm, original, container, name,
                           source->rights & destination->rights, out);
    if (status == NEO_OK && moving) {
        neo_delete_region(vm, original->id);
    }
    return status;
}

neo_status neo_object_copy(neo_vm *vm, const neo_capability *source,
                           const neo_capability *destination, const char *name,
                           const neo_capability **out_copy) {
    return neo_transfer(vm, source, destination, name, false, out_copy);
}

neo_status neo_object_move(neo_vm *vm, const neo_capability *source,
                           const neo_capability *destination, const char *name,
                           const neo_capability **out_copy) {
    return neo_transfer(vm, source, destination, name, true, out_copy);
}

neo_status neo_object_delete(neo_vm *vm, const neo_capability *object) {
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, object, NEO_DELETE, &node);
    if (status != NEO_OK) {
        return status;
    }
    if (node->parent == 0) {
        return NEO_DENIED;
    }
    neo_delete_region(vm, node->id);
    return NEO_OK;
}

neo_status neo_image_duplicate(neo_vm *vm, const neo_capability *root,
                               const char *name, const neo_capability **out_root) {
    if (out_root == NULL) {
        return NEO_INVALID;
    }
    *out_root = NULL;
    if (name == NULL || name[0] == '\0') {
        return NEO_INVALID;
    }
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, root, NEO_READ | NEO_COPY, &node);
    if (status != NEO_OK) {
        return status;
    }
    if (node->parent != 0) {
        return NEO_WRONG_KIND;
    }
    return neo_duplicate(vm, node, NULL, name, root->rights, out_root);
}

neo_status neo_image_unload(neo_vm *vm, const neo_capability *root) {
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, root, NEO_DELETE, &node);
    if (status != NEO_OK) {
        return status;
    }
    if (node->parent != 0) {
        return NEO_WRONG_KIND;
    }
    neo_delete_region(vm, node->id);
    return NEO_OK;
}
