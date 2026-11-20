#ifndef NEO_LITERAL_H
#define NEO_LITERAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static inline bool neo_f64_literal(const char **cursor, double *value) {
    const char *start=*cursor;
    if (*start!='~') { return false; }
    ++start;
    char *end;
    double number=strtod(start,&end);
    if (end==start || strspn(start,"0123456789+-.eE")!=(size_t)(end-start) ||
        !isfinite(number)) { return false; }
    *value=number;
    *cursor=end;
    return true;
}

enum { NEO_VECTOR_LIMIT=31 };

static inline bool neo_f64_vector_literal(const char **cursor, size_t *count, double *values) {
    const char *p=*cursor;
    if (p[0]!='~' || p[1]!='[') { return false; }
    p+=2;
    *count=0;
    while (*p==' ' || *p=='\t') { ++p; }
    if (*p!=']') {
        for (;;) {
            if (*count==NEO_VECTOR_LIMIT) { return false; }
            const char *start=p;
            char *end;
            double value=strtod(start,&end);
            if (end==start || !isfinite(value) ||
                strspn(start,"0123456789+-.eE")!=(size_t)(end-start)) { return false; }
            p=end;
            if (values) { values[*count]=value; }
            ++*count;
            while (*p==' ' || *p=='\t') { ++p; }
            if (*p!=',') { break; }
            ++p;
            while (*p==' ' || *p=='\t') { ++p; }
        }
    }
    if (*p!=']') { return false; }
    *cursor=p+1;
    return true;
}

static inline bool neo_vector_literal(const char **cursor, size_t *count, int32_t *values) {
    const char *p=*cursor;
    if (*p!='[') { return false; }
    ++p;
    *count=0;
    while (*p==' ' || *p=='\t') { ++p; }
    if (*p!=']') {
        for (;;) {
            bool negative=*p=='-';
            if (negative) { ++p; }
            if (*p<'0' || *p>'9' || *count==NEO_VECTOR_LIMIT) { return false; }
            int64_t value=0;
            while (*p>='0' && *p<='9') {
                value=value*10+(*p++-'0');
                if (value>(int64_t)INT32_MAX+negative) { return false; }
            }
            if (values) { values[*count]=(int32_t)(negative ? -value : value); }
            ++*count;
            while (*p==' ' || *p=='\t') { ++p; }
            if (*p!=',') { break; }
            ++p;
            while (*p==' ' || *p=='\t') { ++p; }
        }
    }
    if (*p!=']') { return false; }
    *cursor=p+1;
    return true;
}

static inline int neo_hex_digit(char digit) {
    if (digit>='0' && digit<='9') { return digit-'0'; }
    if (digit>='a' && digit<='f') { return digit-'a'+10; }
    if (digit>='A' && digit<='F') { return digit-'A'+10; }
    return -1;
}

static inline bool neo_bytes_literal(const char **cursor, size_t *bytes) {
    const char *p=*cursor;
    if (*p!='#') { return false; }
    const char *start=++p;
    while (neo_hex_digit(*p)>=0) { ++p; }
    size_t digits=(size_t)(p-start);
    if (digits%2u || digits>62u) { return false; }
    *cursor=p;
    *bytes=digits/2u;
    return true;
}

/* Scan a quoted token and count its decoded bytes. */
static inline bool neo_text_literal(const char **cursor, size_t *bytes) {
    const char *p=*cursor;
    if (*p++!='"') { return false; }
    size_t count=0;
    while (*p && *p!='"') {
        unsigned char c=(unsigned char)*p++;
        if (c<32 || c==127) { return false; }
        if (c=='\\') {
            if (!*p || !strchr("\\\"nrt",*p)) { return false; }
            ++p;
        }
        ++count;
    }
    if (*p!='"') { return false; }
    *cursor=p+1;
    *bytes=count;
    return true;
}

/* The caller has already validated the literal. */
static inline unsigned char neo_text_byte(const char **cursor) {
    unsigned char byte=(unsigned char)*(*cursor)++;
    if (byte!='\\') { return byte; }
    switch (*(*cursor)++) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case '"': return '"';
        default: return '\\';
    }
}

static inline bool neo_sequence_literal(const char **cursor, size_t *bytes, bool binary) {
    return binary ? neo_bytes_literal(cursor,bytes) : neo_text_literal(cursor,bytes);
}

static inline unsigned char neo_sequence_byte(const char **cursor, bool binary) {
    if (!binary) { return neo_text_byte(cursor); }
    const char *digits=*cursor;
    *cursor+=2;
    return (unsigned char)(neo_hex_digit(digits[0])*16+neo_hex_digit(digits[1]));
}

#endif
