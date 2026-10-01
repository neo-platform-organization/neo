#ifndef NEO_INTERNAL_H
#define NEO_INTERNAL_H

#include "neo.h"
#include "neo_message.h"

/* Private implementation types; never expose these to image code. */
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
    uint64_t order;
    char *name;
    neo_value value;
    neo_edge *edges;
    bool deleting;
    bool ether;
    neo_message *message;
    neo_message *inbox_first;
    neo_message *inbox_last;
    neo_message *active_message;
    struct neo_object *next;
} neo_object;

struct neo_capability {
    neo_object_id target;
    unsigned rights;
    struct neo_capability *next;
};

typedef struct neo_registration neo_registration;

struct neo_vm {
    neo_allocator allocator;
    neo_object_id next_id;
    neo_object *objects;
    neo_capability *capabilities;
    neo_context *contexts;
    neo_message *messages;
    uint64_t next_submission;
    neo_registration *registrations;
};


void *neo_alloc(neo_vm *vm, size_t size);
void neo_free(neo_vm *vm, void *memory);
neo_object *neo_lookup(neo_vm *vm, neo_object_id id);
neo_status neo_resolve(neo_vm *vm, const neo_capability *cap, unsigned rights, neo_object **out);
neo_status neo_issue(neo_vm *vm, neo_object_id id, unsigned rights, const neo_capability **out);
neo_status neo_node_new(neo_vm *vm, const char *name, neo_value value, neo_object **out);
bool neo_inside(neo_vm *vm, neo_object *node, neo_object_id ancestor);
bool neo_valid_value(neo_value value);
neo_status neo_value_copy(neo_vm *vm, neo_value source, neo_value *out);
void neo_value_free(neo_vm *vm, neo_value value);
void neo_messages_destroy(neo_vm *vm);
bool neo_actor_has_messages(neo_vm *vm, neo_object_id id);

char *neo_string(neo_vm *vm, const char *source);
void neo_object_free(neo_vm *vm, neo_object *object);
neo_object *neo_child_named(neo_vm *vm, neo_object_id parent, const char *name);
neo_object *neo_child_after(neo_vm *vm, neo_object_id parent, uint64_t order);
void neo_scheduler_destroy(neo_vm *vm);

neo_status neo_message_accept_pending(neo_vm *vm, const neo_context *context,
                                      uint64_t cutoff, size_t *out_count);
neo_status neo_message_retry(neo_vm *vm, const neo_context *context);

bool neo_scheduler_contains(neo_vm *vm, neo_object_id actor);

#endif
