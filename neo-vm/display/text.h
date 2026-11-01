#ifndef NEO_TEXT_H
#define NEO_TEXT_H
#include "objects/objects.h"

typedef struct neo_text_decoder { uint16_t high; } neo_text_decoder;
/** Bounded committed UTF-8: nonempty, terminated within capacity, no C0/DEL,
 * overlongs, surrogates or values above U+10FFFF. No normalization/grapheme policy. */
bool neo_text_valid(const char *text, size_t capacity);
/** Encode one nonzero Unicode scalar into terminated UTF-8 (at most4 bytes). */
neo_status neo_text_encode(uint32_t scalar, char out[5]);
/** Decode one UTF-16 unit. Malformed sequences produce U+FFFD, possibly followed
 * by the current scalar. A high surrogate can remain pending; clear high on reset. */
size_t neo_text_utf16(neo_text_decoder *decoder, uint16_t unit, uint32_t out[2]);
#endif
