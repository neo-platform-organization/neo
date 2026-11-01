#ifndef NEO_IMAGE_H
#define NEO_IMAGE_H

#include "objects/objects.h"
#include "resource_limits.h"

typedef struct neo_diagnostic {
    size_t line;
    size_t column;
    char message[160];
} neo_diagnostic;

/** Inert host loading: no handler is run. Labels resolve only inside this file;
 * embedded edges may grant image-local rights but never host resources.
 * On failure no image is published. Bounded by resource_limits.h: 4 MiB source,
 * 128 nesting levels, 131072 objects, 4096 decoded bytes per token/string. */
neo_status neo_image_parse(neo_vm *vm, const char *source,
                           const neo_capability **out_root, neo_diagnostic *out_error);

/** Lossless for ordinary graphs (identity relationships, not numeric IDs).
 * Runtime ETHER/message/queue state is rejected, never silently discarded.
 * Output is VM-allocator-owned; call neo_image_text_free before destroying VM.
 * Does not perform filesystem I/O or overwrite any file. */
neo_status neo_image_format(neo_vm *vm, const neo_capability *root, char **out_text);
/** Version 1 checkpoint: quiescent graph plus message policy/queue state.
 * Processing requests remain blocked until explicit retry/discard; no stack or
 * host resources/legacy registrations are restored. Numeric IDs are remapped.
 * Load inertly with neo_image_parse, which also accepts legacy unversioned input. */
neo_status neo_image_checkpoint(neo_vm *vm, const neo_capability *root, char **out_text);
void neo_image_text_free(neo_vm *vm, char *text);

/** Canonical editable closed subgraph; no native resources/runtime roles.
 * Source output is VM-allocator-owned; release with neo_image_text_free.
 * Apply preserves the target root identity and publishes validated replacement
 * contents only after all fallible work. Child identities are replaced.
 * expected is the previous canonical source; mismatch returns NEO_CONFLICT.
 * Image behavior must also pass the evaluator's active-code mutation guard. */
neo_status neo_image_source(neo_vm *vm, const neo_capability *target, char **out);
neo_status neo_image_apply_source(neo_vm *vm, const neo_capability *target, const char *source,
                                  const char *expected, neo_diagnostic *error);

#endif
