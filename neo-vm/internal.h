#ifndef NEO_INTERNAL_H
#define NEO_INTERNAL_H

#include "objects/objects.h"
#include "resource_limits.h"
#include "messages/messages.h"
#include "platform/platform.h"

/* Collision chains preserve exact identity/parent checks. These indexes are
 * host bookkeeping, maintained only on node publication and destruction. */
#define NEO_INDEX_BUCKETS 8192u
#define NEO_CAP_BUCKETS 4096u

/* Private implementation types; never expose these to image code. */
typedef struct neo_edge {
    char *name;
    neo_object_id target;
    unsigned rights;
    struct neo_edge *next;
} neo_edge;

typedef struct neo_stream_resource neo_stream_resource;

typedef struct neo_display_resource neo_display_resource;

typedef struct neo_object {
    neo_object_id id;
    neo_object_id image;
    neo_object_id parent;
    uint64_t order;
    char *name;
    neo_value value;
    neo_edge *edges;
    neo_display_resource *display;
    neo_stream_resource *stream;
    bool deleting;
    bool ether;
    neo_message *message;
    neo_message *inbox_first;
    neo_message *inbox_last;
    neo_message *active_message;
    struct neo_object *next;
    struct neo_object *id_next;
    struct neo_object *parent_next;
    struct neo_object *name_next;
    bool published;
} neo_object;

struct neo_capability {
    neo_object_id target;
    unsigned rights;
    struct neo_capability *next;
    struct neo_capability *pointer_next;
    struct neo_capability *grant_next;
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

typedef struct neo_registration neo_registration;

struct neo_vm {
    neo_allocator allocator;
    neo_platform_info platform;
    neo_platform_backend platform_backend;
    void *platform_context;
    bool platform_configured;
    neo_object_id next_id;
    neo_object *objects;
    neo_object *id_index[NEO_INDEX_BUCKETS];
    neo_object *parent_index[NEO_INDEX_BUCKETS];
    neo_object *name_index[NEO_INDEX_BUCKETS];
    neo_capability *capabilities;
    neo_capability *cap_pointer_index[NEO_CAP_BUCKETS];
    neo_capability *cap_grant_index[NEO_CAP_BUCKETS];
    neo_context *contexts;
    neo_message *messages;
    uint64_t next_submission;
    size_t active_invocations;
    neo_registration *registrations;
};


void *neo_alloc(neo_vm *vm, size_t size);
void neo_free(neo_vm *vm, void *memory);
void neo_node_publish(neo_vm *vm, neo_object *node);
void neo_node_rename_owned(neo_vm *vm, neo_object *node, char *name);
void neo_node_reparent_staged(neo_vm *vm, neo_object *node, neo_object_id parent, neo_object_id image);
size_t neo_name_bucket(neo_object_id parent, const char *name);
neo_object *neo_lookup(neo_vm *vm, neo_object_id id);
neo_status neo_resolve(neo_vm *vm, const neo_capability *cap, unsigned rights, neo_object **out);
neo_status neo_issue(neo_vm *vm, neo_object_id id, unsigned rights, const neo_capability **out);
const neo_capability *neo_capability_known(neo_vm *vm, const neo_capability *candidate);
neo_status neo_node_new(neo_vm *vm, const char *name, neo_value value, neo_object **out);
bool neo_inside(neo_vm *vm, neo_object *node, neo_object_id ancestor);
void neo_delete_region(neo_vm *vm, neo_object_id root);
bool neo_valid_value(neo_value value);
bool neo_primitive_known(const char *name);
neo_status neo_value_copy(neo_vm *vm, neo_value source, neo_value *out);
void neo_value_free(neo_vm *vm, neo_value value);
void neo_messages_destroy(neo_vm *vm);
bool neo_actor_has_messages(neo_vm *vm, neo_object_id id);

char *neo_string(neo_vm *vm, const char *source);
neo_status neo_stream_context(neo_vm *vm, const neo_capability *cap, unsigned rights,
                              void (*destroy)(void *), void **out);
void neo_stream_release(neo_vm *vm, neo_object *object);
void neo_display_release(neo_vm *vm, neo_object *object);
void neo_object_free(neo_vm *vm, neo_object *object);
neo_object *neo_child_named(neo_vm *vm, neo_object_id parent, const char *name);
neo_object *neo_child_after(neo_vm *vm, neo_object_id parent, uint64_t order);
void neo_scheduler_destroy(neo_vm *vm);

neo_status neo_message_accept_pending(neo_vm *vm, const neo_context *context,
                                      uint64_t cutoff, size_t *out_count);
neo_status neo_message_retry(neo_vm *vm, const neo_context *context);

bool neo_scheduler_any_active(neo_vm *vm);
bool neo_scheduler_contains(neo_vm *vm, neo_object_id actor);

neo_status neo_message_from_reference(neo_vm *vm, const neo_capability *reference, const neo_message **out);
neo_status neo_message_reference(neo_vm *vm, const neo_message *message, const neo_capability **out);
#endif
