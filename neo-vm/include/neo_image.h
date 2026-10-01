#ifndef NEO_IMAGE_H
#define NEO_IMAGE_H

#include "neo.h"

typedef struct neo_diagnostic {
    size_t line;
    size_t column;
    char message[160];
} neo_diagnostic;

/* Inert host loading: no handler is run. Labels resolve only inside this file;
 * embedded edges may grant image-local rights but never host resources.
 * On failure no image is published. Limits: 1 MiB source, 128 nesting levels,
 * 4096 objects, and 4096 decoded bytes per token/string. */
neo_status neo_image_parse(neo_vm *vm, const char *source,
                           const neo_capability **out_root, neo_diagnostic *out_error);

/* Lossless for ordinary graphs (identity relationships, not numeric IDs).
 * Runtime ETHER/message/queue state is rejected, never silently discarded.
 * Output is VM-allocator-owned; call neo_image_text_free before destroying VM.
 * Does not perform filesystem I/O or overwrite any file. */
neo_status neo_image_format(neo_vm *vm, const neo_capability *root, char **out_text);
void neo_image_text_free(neo_vm *vm, char *text);

#endif
