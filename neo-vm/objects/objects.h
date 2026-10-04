#ifndef NEO_H
#define NEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct neo_vm neo_vm;
typedef struct neo_capability neo_capability;
typedef uint64_t neo_object_id;

typedef enum neo_status {
    NEO_OK,
    NEO_INVALID,
    NEO_UNAVAILABLE,
    NEO_DENIED,
    NEO_OUT_OF_MEMORY,
    NEO_CONFLICT,
    NEO_CYCLE,
    NEO_WRONG_IMAGE,
    NEO_WRONG_KIND,
    NEO_LIMIT,
    NEO_BUSY,
    NEO_BAD_STATE,
    NEO_UNSUPPORTED,
    NEO_PARSE_ERROR,
    NEO_OVERFLOW,
    NEO_DIVIDE_BY_ZERO,
    NEO_RAISED,
    NEO_IO_ERROR
} neo_status;

typedef enum neo_kind {
    NEO_OBJECT,
    NEO_INTEGER,
    NEO_BOOLEAN,
    NEO_TEXT,
    NEO_PRIMITIVE,
    NEO_INTEGERS,
    NEO_REFERENCE /* Transient evaluator result; never a stored payload. */
} neo_kind;

typedef enum neo_right {
    NEO_READ = 1u << 0,
    NEO_WRITE = 1u << 1,
    NEO_INSERT = 1u << 2,
    NEO_COPY = 1u << 3,
    NEO_DELETE = 1u << 4,
    NEO_DELEGATE = 1u << 5,
    NEO_ACT = 1u << 6,
    NEO_SEND = 1u << 7,
    NEO_ALL = (1u << 8) - 1u
} neo_right;

/** These are C substrate payloads, not non-object values in the neo language.
 * Text/primitive input strings are copied; output strings are borrowed until
 * the next mutation/deletion of their object or VM destruction. Primitive text
 * is an inert semantic identifier, never an executable native address. */
typedef struct neo_value {
    neo_kind kind;
    int64_t integer;
    bool boolean;
    const char *text;
    const int64_t *integers;
    size_t count;
    const neo_capability *reference; /* Borrowed until VM destruction. */
} neo_value;

typedef void *(*neo_allocate_fn)(void *context, size_t size);
typedef void (*neo_release_fn)(void *context, void *memory);
typedef struct neo_allocator {
    void *context;
    neo_allocate_fn allocate;
    neo_release_fn release;
} neo_allocator;

/** NULL allocator selects malloc/free. Custom allocations need not be zeroed.
 * Allocator context must outlive the VM. Every successful allocation is owned
 * by the VM until released through the paired allocator. */
neo_status neo_vm_create(const neo_allocator *allocator, neo_vm **out_vm);
void neo_vm_destroy(neo_vm *vm);
const char *neo_status_name(neo_status status);

/** Host management API, not exposed directly to image code. Empty construction
 * is not file loading. Unload never deletes a file. Duplicate handles quiescent
 * graphs only: images containing ETHER/message state are explicitly rejected
 * by duplication until lifecycle-preserving duplication is implemented. */
neo_status neo_image_create(neo_vm *vm, const char *name,
                            const neo_capability **out_root);
neo_status neo_image_duplicate(neo_vm *vm, const neo_capability *root,
                               const char *name, const neo_capability **out_root);
neo_status neo_image_unload(neo_vm *vm, const neo_capability *root);

/** Capabilities are opaque, VM-owned, and valid until VM destruction. Deleted
 * targets leave safe stale capabilities. Cross-VM/forged pointers are rejected.
 * This is a trusted host embedding interface; an evaluator must supply held
 * capabilities, never let neo manufacture C pointers. Outputs are cleared on
 * failure. No operation widens a supplied capability's rights.
 *
 * Provisional policy: a containment capability covers its subtree, and child
 * lookup inherits its rights. Copy requires READ|COPY over that subtree;
 * delete requires DELETE. Explicit connections grant only their stored rights.
 * All operations are sequential; no thread safety is claimed. */
neo_status neo_capability_restrict(neo_vm *vm, const neo_capability *source,
                                   unsigned rights, const neo_capability **out_cap);
neo_status neo_object_create(neo_vm *vm, const neo_capability *parent,
                             const char *name, neo_value value,
                             const neo_capability **out_object);
neo_status neo_object_child(neo_vm *vm, const neo_capability *parent,
                            const char *name, const neo_capability **out_child);
neo_status neo_object_read(neo_vm *vm, const neo_capability *object,
                           neo_value *out_value);
neo_status neo_object_write(neo_vm *vm, const neo_capability *object,
                            neo_value value);
neo_status neo_object_identity(neo_vm *vm, const neo_capability *object,
                               neo_object_id *out_id);
neo_status neo_object_child_count(neo_vm *vm, const neo_capability *object,
                                  size_t *out_count);
neo_status neo_object_connect(neo_vm *vm, const neo_capability *object,
                              const char *name, const neo_capability *target,
                              unsigned rights);
neo_status neo_object_connection(neo_vm *vm, const neo_capability *object,
                                 const char *name, const neo_capability **out_target);
neo_status neo_object_copy(neo_vm *vm, const neo_capability *source,
                           const neo_capability *destination, const char *name,
                           const neo_capability **out_copy);
neo_status neo_object_move(neo_vm *vm, const neo_capability *source,
                           const neo_capability *destination, const char *name,
                           const neo_capability **out_copy);
neo_status neo_object_delete(neo_vm *vm, const neo_capability *object);

/** Name is borrowed until deletion. Indexed enumeration follows creation order,
 * preserved by copying and image serialization. */
neo_status neo_object_name(neo_vm *vm, const neo_capability *object, const char **out_name);
neo_status neo_object_child_at(neo_vm *vm, const neo_capability *object,
                               size_t index, const neo_capability **out_child);

/** Packed signed-integer payload. Inputs are copied; read pointers are borrowed.
 * Element operations preserve payload length and enforce the object's rights. */
neo_status neo_array_get(neo_vm *vm, const neo_capability *object, size_t index, int64_t *out);
neo_status neo_array_set(neo_vm *vm, const neo_capability *object, size_t index, int64_t value);
neo_status neo_array_size(neo_vm *vm, const neo_capability *object, size_t *out);

#endif
