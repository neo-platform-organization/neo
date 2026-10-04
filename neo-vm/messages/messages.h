#ifndef NEO_MESSAGE_H
#define NEO_MESSAGE_H

#include "objects/objects.h"

typedef struct neo_context neo_context;
typedef struct neo_message neo_message;

typedef enum neo_message_state {
    NEO_MESSAGE_CREATED,
    NEO_MESSAGE_PENDING,
    NEO_MESSAGE_ACCEPTED,
    NEO_MESSAGE_PROCESSING,
    NEO_MESSAGE_PROCESSED
} neo_message_state;

typedef enum neo_message_right {
    NEO_MESSAGE_READ = 1u << 0,
    NEO_MESSAGE_EDIT = 1u << 1,
    NEO_MESSAGE_INSPECT = 1u << 2,
    NEO_MESSAGE_ALL = (1u << 3) - 1u
} neo_message_right;

typedef struct neo_message_info {
    neo_object_id object;
    neo_object_id sender;
    neo_object_id receiver;
    neo_message_state state;
    uint64_t submission;
    bool receiver_available;
} neo_message_info;

/** Trusted host/bootstrap functions. An evaluator obtains contexts from its
 * activation, never from message-supplied IDs. NEO_ACT is required to establish
 * an acting context. Context/message handles are VM-owned until destruction.
 * ETHER is created explicitly under an image root, one per image. */
neo_status neo_context_create(neo_vm *vm, const neo_capability *actor,
                              const neo_context **out_context);
neo_status neo_ether_create(neo_vm *vm, const neo_capability *image,
                            const neo_capability **out_ether);

/** Make requires an ETHER capability with READ and a receiver capability with
 * SEND. No discovery of the ETHER is implicit. Maker gets READ|EDIT|INSPECT;
 * receiver gets READ|INSPECT. Policy can grant additional actors a subset of
 * these rights before submission. Recipient is fixed. Only the recipient can
 * accept, begin, and finish; only the maker can submit or specialize policy.
 *
 * Proposed v0: accepted payloads are immutable for everyone. No cancellation,
 * automatic retry, automatic failure completion, or concurrent C calls yet. */
neo_status neo_message_create(neo_vm *vm, const neo_context *context,
                              const neo_capability *ether,
                              const neo_capability *receiver, neo_value content,
                              const neo_message **out_message);
neo_status neo_message_grant(neo_vm *vm, const neo_context *context,
                             const neo_message *message,
                             const neo_capability *actor, unsigned rights);
neo_status neo_message_inspect(neo_vm *vm, const neo_context *context,
                               const neo_message *message, neo_message_info *out_info);

/** Paths are relative names separated by '/', with "" denoting the payload
 * root. No empty segments, '.' or '..' traversal. Fields are contained objects.
 * Read text is borrowed until that payload is edited or the image unloaded.
 * Reads do not return a raw capability that could bypass message policy. */
neo_status neo_message_read(neo_vm *vm, const neo_context *context,
                            const neo_message *message, const char *path,
                            neo_value *out_value);
neo_status neo_message_write(neo_vm *vm, const neo_context *context,
                             const neo_message *message, const char *path,
                             neo_value value);
neo_status neo_message_field_create(neo_vm *vm, const neo_context *context,
                                    const neo_message *message, const char *parent_path,
                                    const char *name, neo_value value);
neo_status neo_message_submit(neo_vm *vm, const neo_context *context,
                              const neo_message *message);
neo_status neo_message_accept(neo_vm *vm, const neo_context *context,
                              const neo_message *message);
neo_status neo_message_begin(neo_vm *vm, const neo_context *context,
                             const neo_message **out_message);
neo_status neo_message_finish(neo_vm *vm, const neo_context *context,
                              const neo_message *message);

#endif
