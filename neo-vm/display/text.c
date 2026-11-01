#include "display/text.h"
#include <string.h>

bool neo_text_valid(const char *text, size_t capacity) {
    if (text == NULL) { return false; }
    const char *end = memchr(text, '\0', capacity);
    if (end == NULL || end == text) { return false; }
    size_t length = (size_t)(end - text), i = 0;
    while (i < length) {
        uint32_t first = (uint8_t)text[i++], scalar = 0, minimum = 0;
        size_t count = 0;
        if (first < 128u) {
            if (first < 32u || first == 127u) { return false; }
            continue;
        }
        if (first >= 194u && first <= 223u) { scalar = first & 31u; count = 1; minimum = 128u; }
        else if (first >= 224u && first <= 239u) { scalar = first & 15u; count = 2; minimum = 2048u; }
        else if (first >= 240u && first <= 244u) { scalar = first & 7u; count = 3; minimum = 65536u; }
        else { return false; }
        if (count > length - i) { return false; }
        for (size_t j = 0; j < count; ++j) {
            uint32_t byte = (uint8_t)text[i++];
            if ((byte & 192u) != 128u) { return false; }
            scalar = (scalar << 6) | (byte & 63u);
        }
        if (scalar < minimum || scalar > 1114111u || (scalar >= 55296u && scalar <= 57343u)) { return false; }
    }
    return true;
}

neo_status neo_text_encode(uint32_t scalar, char out[5]) {
    if (out == NULL) { return NEO_INVALID; }
    memset(out, 0, 5);
    if (scalar == 0 || scalar > 1114111u || (scalar >= 55296u && scalar <= 57343u)) { return NEO_INVALID; }
    if (scalar < 128u) { out[0] = (char)scalar; }
    else if (scalar < 2048u) {
        out[0] = (char)(192u | (scalar >> 6)); out[1] = (char)(128u | (scalar & 63u));
    } else if (scalar < 65536u) {
        out[0] = (char)(224u | (scalar >> 12)); out[1] = (char)(128u | ((scalar >> 6) & 63u)); out[2] = (char)(128u | (scalar & 63u));
    } else {
        out[0] = (char)(240u | (scalar >> 18)); out[1] = (char)(128u | ((scalar >> 12) & 63u));
        out[2] = (char)(128u | ((scalar >> 6) & 63u)); out[3] = (char)(128u | (scalar & 63u));
    }
    return NEO_OK;
}

size_t neo_text_utf16(neo_text_decoder *decoder, uint16_t unit, uint32_t out[2]) {
    size_t count = 0;
    if (decoder->high != 0) {
        uint16_t high = decoder->high; decoder->high = 0;
        if (unit >= 56320u && unit <= 57343u) {
            out[count++] = 65536u + (((uint32_t)high - 55296u) << 10) + (uint32_t)unit - 56320u;
            return count;
        }
        out[count++] = 65533u;
    }
    if (unit >= 55296u && unit <= 56319u) { decoder->high = unit; }
    else if (unit >= 56320u && unit <= 57343u) { out[count++] = 65533u; }
    else { out[count++] = unit; }
    return count;
}
