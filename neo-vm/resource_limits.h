#ifndef NEO_RESOURCE_LIMITS_H
#define NEO_RESOURCE_LIMITS_H

/* Trusted host build profile. These are codec/import/package bounds, not image
 * semantics or a global allocator quota. Keep snapshot wire limits separate. */
#define NEO_IMAGE_MAX_OBJECTS 131072u
#define NEO_IMAGE_MAX_SOURCE_BYTES (4u * 1024u * 1024u)
#define NEO_IMAGE_MAX_DEPTH 128u
#define NEO_IMAGE_MAX_TOKEN_BYTES 4096u

#endif
