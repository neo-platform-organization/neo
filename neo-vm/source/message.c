#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

struct neo_context {
    neo_object_id actor;
    struct neo_context *next;
};

typedef struct neo_message_grant_entry {
    neo_object_id actor;
    unsigned rights;
    struct neo_message_grant_entry *next;
} neo_message_grant_entry;

struct neo_message {
    neo_object_id object;
    neo_object_id sender;
    neo_object_id receiver;
    neo_message_state state;
    uint64_t submission;
    neo_message_grant_entry *grants;
    struct neo_message *queue_next;
    struct neo_message *next;
};

static neo_status neo_context_resolve(neo_vm *vm, const neo_context *context,
                                      neo_object **out_actor) {
    if (vm == NULL || context == NULL) {
        return NEO_INVALID;
    }
    const neo_context *known = vm->contexts;
    while (known != NULL && known != context) {
        known = known->next;
    }
    if (known == NULL) {
        return NEO_DENIED;
    }
    *out_actor = neo_lookup(vm, known->actor);
    return *out_actor == NULL ? NEO_UNAVAILABLE : NEO_OK;
}

static neo_status neo_message_resolve(neo_vm *vm, const neo_message *message,
                                      neo_message **out_message) {
    if (vm == NULL || message == NULL) {
        return NEO_INVALID;
    }
    neo_message *known = vm->messages;
    while (known != NULL && known != message) {
        known = known->next;
    }
    if (known == NULL) {
        return NEO_DENIED;
    }
    if (neo_lookup(vm, known->object) == NULL) {
        return NEO_UNAVAILABLE;
    }
    *out_message = known;
    return NEO_OK;
}

static neo_status neo_message_access(neo_vm *vm, const neo_context *context,
                                     const neo_message *message, unsigned rights,
                                     neo_object **out_actor, neo_message **out_message) {
    neo_status status = neo_context_resolve(vm, context, out_actor);
    if (status != NEO_OK) {
        return status;
    }
    status = neo_message_resolve(vm, message, out_message);
    if (status != NEO_OK) {
        return status;
    }
    if (rights == 0) {
        return NEO_OK;
    }
    unsigned allowed = 0;
    if ((*out_actor)->id == message->sender) {
        allowed |= NEO_MESSAGE_ALL;
    }
    if ((*out_actor)->id == message->receiver) {
        allowed |= NEO_MESSAGE_READ | NEO_MESSAGE_INSPECT;
    }
    for (neo_message_grant_entry *grant = message->grants; grant != NULL; grant = grant->next) {
        if (grant->actor == (*out_actor)->id) {
            allowed |= grant->rights;
        }
    }
    if ((rights & allowed) != rights) {
        return NEO_DENIED;
    }
    if ((rights & NEO_MESSAGE_EDIT) != 0 && message->state >= NEO_MESSAGE_ACCEPTED) {
        return NEO_DENIED;
    }
    return NEO_OK;
}

static bool neo_field_name(const char *name) {
    return name != NULL && name[0] != '\0' && strchr(name, '/') == NULL &&
           strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static neo_status neo_payload_path(neo_vm *vm, const neo_message *message,
                                   const char *path, neo_object **out_object) {
    if (path == NULL) {
        return NEO_INVALID;
    }
    neo_object *node = neo_lookup(vm, message->object);
    if (node == NULL) {
        return NEO_UNAVAILABLE;
    }
    while (*path != '\0') {
        const char *separator = strchr(path, '/');
        size_t length = separator == NULL ? strlen(path) : (size_t)(separator - path);
        if (length == 0 || (length == 1 && path[0] == '.') ||
            (length == 2 && path[0] == '.' && path[1] == '.')) {
            return NEO_INVALID;
        }
        neo_object *found = NULL;
        for (neo_object *child = vm->objects; child != NULL; child = child->next) {
            if (child->parent == node->id && strlen(child->name) == length &&
                memcmp(child->name, path, length) == 0) {
                found = child;
                break;
            }
        }
        if (found == NULL) {
            return NEO_UNAVAILABLE;
        }
        node = found;
        if (separator == NULL) {
            break;
        }
        path = separator + 1;
        if (*path == '\0') {
            return NEO_INVALID;
        }
    }
    *out_object = node;
    return NEO_OK;
}

neo_status neo_context_create(neo_vm *vm, const neo_capability *actor,
                              const neo_context **out_context) {
    if (out_context == NULL) {
        return NEO_INVALID;
    }
    *out_context = NULL;
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, actor, NEO_ACT, &node);
    if (status != NEO_OK) {
        return status;
    }
    for (neo_context *existing = vm->contexts; existing != NULL; existing = existing->next) {
        if (existing->actor == node->id) {
            *out_context = existing;
            return NEO_OK;
        }
    }
    neo_context *context = neo_alloc(vm, sizeof(*context));
    if (context == NULL) {
        return NEO_OUT_OF_MEMORY;
    }
    context->actor = node->id;
    context->next = vm->contexts;
    vm->contexts = context;
    *out_context = context;
    return NEO_OK;
}

neo_status neo_ether_create(neo_vm *vm, const neo_capability *image,
                            const neo_capability **out_ether) {
    if (out_ether == NULL) {
        return NEO_INVALID;
    }
    *out_ether = NULL;
    neo_object *root = NULL;
    neo_status status = neo_resolve(vm, image, NEO_INSERT | NEO_READ, &root);
    if (status != NEO_OK) {
        return status;
    }
    if (root->parent != 0) {
        return NEO_WRONG_KIND;
    }
    status = neo_object_create(vm, image, "ETHER", (neo_value){.kind = NEO_OBJECT}, out_ether);
    if (status == NEO_OK) {
        neo_lookup(vm, (*out_ether)->target)->ether = true;
    }
    return status;
}

neo_status neo_message_create(neo_vm *vm, const neo_context *context,
                              const neo_capability *ether,
                              const neo_capability *receiver, neo_value content,
                              const neo_message **out_message) {
    if (out_message == NULL) {
        return NEO_INVALID;
    }
    *out_message = NULL;
    if (!neo_valid_value(content)) {
        return NEO_INVALID;
    }
    neo_object *actor = NULL, *environment = NULL, *recipient = NULL;
    neo_status status = neo_context_resolve(vm, context, &actor);
    if (status != NEO_OK) {
        return status;
    }
    status = neo_resolve(vm, ether, NEO_READ, &environment);
    if (status != NEO_OK) {
        return status;
    }
    if (!environment->ether) {
        return NEO_WRONG_KIND;
    }
    status = neo_resolve(vm, receiver, NEO_SEND, &recipient);
    if (status != NEO_OK) {
        return status;
    }
    if (actor->image != recipient->image || actor->image != environment->image) {
        return NEO_WRONG_IMAGE;
    }
    neo_message *message = neo_alloc(vm, sizeof(*message));
    if (message == NULL) {
        return NEO_OUT_OF_MEMORY;
    }
    char name[32];
    (void)snprintf(name, sizeof(name), "message-%" PRIu64, vm->next_id);
    neo_object *node = NULL;
    status = neo_node_new(vm, name, content, &node);
    if (status != NEO_OK) {
        neo_free(vm, message);
        return status;
    }
    node->parent = environment->id;
    node->image = environment->image;
    node->message = message;
    message->object = node->id;
    message->sender = actor->id;
    message->receiver = recipient->id;
    message->state = NEO_MESSAGE_CREATED;
    node->next = vm->objects;
    vm->objects = node;
    message->next = vm->messages;
    vm->messages = message;
    *out_message = message;
    return NEO_OK;
}

neo_status neo_message_grant(neo_vm *vm, const neo_context *context,
                             const neo_message *message,
                             const neo_capability *actor, unsigned rights) {
    neo_object *maker = NULL, *recipient = NULL;
    neo_message *known = NULL;
    neo_status status = neo_message_access(vm, context, message, 0, &maker, &known);
    if (status != NEO_OK) {
        return status;
    }
    if (maker->id != known->sender || (rights & ~((unsigned)NEO_MESSAGE_ALL)) != 0) {
        return NEO_DENIED;
    }
    if (known->state != NEO_MESSAGE_CREATED) {
        return NEO_BAD_STATE;
    }
    status = neo_resolve(vm, actor, NEO_DELEGATE, &recipient);
    if (status != NEO_OK) {
        return status;
    }
    if (maker->image != recipient->image) {
        return NEO_WRONG_IMAGE;
    }
    for (neo_message_grant_entry *entry = known->grants; entry != NULL; entry = entry->next) {
        if (entry->actor == recipient->id) {
            entry->rights = rights;
            return NEO_OK;
        }
    }
    neo_message_grant_entry *entry = neo_alloc(vm, sizeof(*entry));
    if (entry == NULL) {
        return NEO_OUT_OF_MEMORY;
    }
    entry->actor = recipient->id;
    entry->rights = rights;
    entry->next = known->grants;
    known->grants = entry;
    return NEO_OK;
}

neo_status neo_message_inspect(neo_vm *vm, const neo_context *context,
                               const neo_message *message, neo_message_info *out_info) {
    if (out_info == NULL) {
        return NEO_INVALID;
    }
    *out_info = (neo_message_info){0};
    neo_object *actor = NULL;
    neo_message *known = NULL;
    neo_status status = neo_message_access(vm, context, message, NEO_MESSAGE_INSPECT,
                                          &actor, &known);
    if (status == NEO_OK) {
        *out_info = (neo_message_info){known->object, known->sender, known->receiver,
            known->state, known->submission, neo_lookup(vm, known->receiver) != NULL};
    }
    return status;
}

neo_status neo_message_read(neo_vm *vm, const neo_context *context,
                            const neo_message *message, const char *path,
                            neo_value *out_value) {
    if (out_value == NULL) {
        return NEO_INVALID;
    }
    *out_value = (neo_value){0};
    neo_object *actor = NULL, *node = NULL;
    neo_message *known = NULL;
    neo_status status = neo_message_access(vm, context, message, NEO_MESSAGE_READ,
                                          &actor, &known);
    if (status == NEO_OK) {
        status = neo_payload_path(vm, known, path, &node);
    }
    if (status == NEO_OK) {
        *out_value = node->value;
    }
    return status;
}

neo_status neo_message_write(neo_vm *vm, const neo_context *context,
                             const neo_message *message, const char *path,
                             neo_value value) {
    if (!neo_valid_value(value)) {
        return NEO_INVALID;
    }
    neo_object *actor = NULL, *node = NULL;
    neo_message *known = NULL;
    neo_status status = neo_message_access(vm, context, message, NEO_MESSAGE_EDIT,
                                          &actor, &known);
    if (status == NEO_OK) {
        status = neo_payload_path(vm, known, path, &node);
    }
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

neo_status neo_message_field_create(neo_vm *vm, const neo_context *context,
                                    const neo_message *message, const char *parent_path,
                                    const char *name, neo_value value) {
    if (!neo_field_name(name) || !neo_valid_value(value)) {
        return NEO_INVALID;
    }
    neo_object *actor = NULL, *parent = NULL;
    neo_message *known = NULL;
    neo_status status = neo_message_access(vm, context, message, NEO_MESSAGE_EDIT,
                                          &actor, &known);
    if (status == NEO_OK) {
        status = neo_payload_path(vm, known, parent_path, &parent);
    }
    if (status != NEO_OK) {
        return status;
    }
    for (neo_object *node = vm->objects; node != NULL; node = node->next) {
        if (node->parent == parent->id && strcmp(node->name, name) == 0) {
            return NEO_CONFLICT;
        }
    }
    neo_object *field = NULL;
    status = neo_node_new(vm, name, value, &field);
    if (status == NEO_OK) {
        field->parent = parent->id;
        field->image = parent->image;
        field->next = vm->objects;
        vm->objects = field;
    }
    return status;
}

neo_status neo_message_submit(neo_vm *vm, const neo_context *context,
                              const neo_message *message) {
    neo_object *actor = NULL;
    neo_message *known = NULL;
    neo_status status = neo_message_access(vm, context, message, 0, &actor, &known);
    if (status != NEO_OK) {
        return status;
    }
    if (actor->id != known->sender) {
        return NEO_DENIED;
    }
    if (known->state != NEO_MESSAGE_CREATED) {
        return NEO_BAD_STATE;
    }
    if (neo_lookup(vm, known->receiver) == NULL) {
        return NEO_UNAVAILABLE;
    }
    if (vm->next_submission == UINT64_MAX) {
        return NEO_LIMIT;
    }
    known->submission = ++vm->next_submission;
    known->state = NEO_MESSAGE_PENDING;
    return NEO_OK;
}

neo_status neo_message_accept(neo_vm *vm, const neo_context *context,
                              const neo_message *message) {
    neo_object *receiver = NULL;
    neo_message *known = NULL;
    neo_status status = neo_message_access(vm, context, message, 0, &receiver, &known);
    if (status != NEO_OK) {
        return status;
    }
    if (receiver->id != known->receiver) {
        return NEO_DENIED;
    }
    if (known->state != NEO_MESSAGE_PENDING) {
        return NEO_BAD_STATE;
    }
    for (neo_message *earlier = vm->messages; earlier != NULL; earlier = earlier->next) {
        if (earlier->sender == known->sender && earlier->receiver == known->receiver &&
            earlier->state == NEO_MESSAGE_PENDING && earlier->submission < known->submission &&
            neo_lookup(vm, earlier->object) != NULL) {
            return NEO_BUSY;
        }
    }
    /* Single-threaded linearization point: no allocation, callback, or yield
     * occurs between closing edits and queue insertion. */
    known->state = NEO_MESSAGE_ACCEPTED;
    if (receiver->inbox_last == NULL) {
        receiver->inbox_first = known;
    } else {
        receiver->inbox_last->queue_next = known;
    }
    receiver->inbox_last = known;
    return NEO_OK;
}

neo_status neo_message_begin(neo_vm *vm, const neo_context *context,
                             const neo_message **out_message) {
    if (out_message == NULL) {
        return NEO_INVALID;
    }
    *out_message = NULL;
    neo_object *receiver = NULL;
    neo_status status = neo_context_resolve(vm, context, &receiver);
    if (status != NEO_OK) {
        return status;
    }
    if (receiver->active_message != NULL) {
        return NEO_BUSY;
    }
    neo_message *message = receiver->inbox_first;
    if (message == NULL) {
        return NEO_UNAVAILABLE;
    }
    receiver->inbox_first = message->queue_next;
    if (receiver->inbox_first == NULL) {
        receiver->inbox_last = NULL;
    }
    message->queue_next = NULL;
    message->state = NEO_MESSAGE_PROCESSING;
    receiver->active_message = message;
    *out_message = message;
    return NEO_OK;
}

neo_status neo_message_finish(neo_vm *vm, const neo_context *context,
                              const neo_message *message) {
    neo_object *receiver = NULL;
    neo_message *known = NULL;
    neo_status status = neo_message_access(vm, context, message, 0, &receiver, &known);
    if (status != NEO_OK) {
        return status;
    }
    if (receiver->id != known->receiver) {
        return NEO_DENIED;
    }
    if (receiver->active_message != known || known->state != NEO_MESSAGE_PROCESSING) {
        return NEO_BAD_STATE;
    }
    known->state = NEO_MESSAGE_PROCESSED;
    receiver->active_message = NULL;
    return NEO_OK;
}

bool neo_actor_has_messages(neo_vm *vm, neo_object_id id) {
    for (neo_message *message = vm->messages; message != NULL; message = message->next) {
        if ((message->sender == id || message->receiver == id) &&
            neo_lookup(vm, message->object) != NULL) {
            return true;
        }
    }
    return false;
}

void neo_messages_destroy(neo_vm *vm) {
    while (vm->messages != NULL) {
        neo_message *next = vm->messages->next;
        neo_message_grant_entry *entry = vm->messages->grants;
        while (entry != NULL) {
            neo_message_grant_entry *following = entry->next;
            neo_free(vm, entry);
            entry = following;
        }
        neo_free(vm, vm->messages);
        vm->messages = next;
    }
    while (vm->contexts != NULL) {
        neo_context *next = vm->contexts->next;
        neo_free(vm, vm->contexts);
        vm->contexts = next;
    }
}

/* Scheduler delivery uses the authenticated receiver context. A boundary's
 * submission cutoff prevents newly emitted messages running in that boundary. */
neo_status neo_message_accept_pending(neo_vm *vm, const neo_context *context,
                                      uint64_t cutoff, size_t *out_count) {
    *out_count = 0;
    neo_object *receiver = NULL;
    neo_status status = neo_context_resolve(vm, context, &receiver);
    if (status != NEO_OK) {
        return status;
    }
    for (;;) {
        neo_message *first = NULL;
        for (neo_message *message = vm->messages; message != NULL; message = message->next) {
            if (message->receiver == receiver->id && message->state == NEO_MESSAGE_PENDING &&
                message->submission <= cutoff && neo_lookup(vm, message->object) != NULL &&
                (first == NULL || message->submission < first->submission)) {
                first = message;
            }
        }
        if (first == NULL) {
            return NEO_OK;
        }
        status = neo_message_accept(vm, context, first);
        if (status != NEO_OK) {
            return status;
        }
        ++*out_count;
    }
}

neo_status neo_message_retry(neo_vm *vm, const neo_context *context) {
    neo_object *receiver = NULL;
    neo_status status = neo_context_resolve(vm, context, &receiver);
    if (status != NEO_OK) {
        return status;
    }
    neo_message *message = receiver->active_message;
    if (message == NULL) {
        return NEO_BAD_STATE;
    }
    message->state = NEO_MESSAGE_ACCEPTED;
    message->queue_next = receiver->inbox_first;
    receiver->inbox_first = message;
    if (receiver->inbox_last == NULL) {
        receiver->inbox_last = message;
    }
    receiver->active_message = NULL;
    return NEO_OK;
}
