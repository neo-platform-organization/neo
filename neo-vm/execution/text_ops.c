#include "execution/evaluator_internal.h"
#include "display/text.h"
#include <string.h>

static bool neo_text_boundary(const char *text, size_t length, size_t at) {
    return at <= length && (at == length || ((uint8_t)text[at] & 192u) != 128u);
}

bool neo_text_document_valid(const char *text) {
    size_t length=strlen(text),i=0;
    while (i<length) {
        uint8_t byte=(uint8_t)text[i];
        if (byte<128u) { if (byte<32u && byte!=9u && byte!=10u && byte!=13u) { return false; } ++i; continue; }
        size_t count=byte<224u ? 2u : (byte<240u ? 3u : 4u);
        if (count>length-i) { return false; }
        char scalar[5]={0}; memcpy(scalar,text+i,count);
        if (!neo_text_valid(scalar,count+1u)) { return false; }
        i+=count;
    }
    return true;
}

static neo_status neo_text_charge(neo_activation *a, size_t bytes) {
    if (bytes>NEO_IMAGE_MAX_SOURCE_BYTES) { return NEO_LIMIT; }
    size_t work=(bytes+63u)/64u;
    if (work>a->budget-a->report->steps) { return NEO_LIMIT; }
    a->report->steps+=work; return NEO_OK;
}

static neo_status neo_text_piece(neo_activation *a, const char *text, size_t start, size_t count, neo_value *out) {
    neo_status status=neo_text_charge(a,count);
    if (status != NEO_OK) { return status; }
    char *copy=neo_alloc(a->vm,count+1u);
    if (copy == NULL) { return NEO_OUT_OF_MEMORY; }
    memcpy(copy,text+start,count);
    *out=(neo_value){.kind=NEO_TEXT,.text=copy}; return NEO_OK;
}

neo_status neo_text_value_operation(neo_activation *a, const char *op, neo_value *v, neo_value *out) {
    if (strcmp(op,"text-from-bytes") == 0) {
        if (v[0].kind != NEO_INTEGERS) { return NEO_WRONG_KIND; }
        neo_status status=neo_text_charge(a,v[0].count);
        if (status != NEO_OK) { return status; }
        char *copy=neo_alloc(a->vm,v[0].count+1u);
        if (copy == NULL) { return NEO_OUT_OF_MEMORY; }
        for (size_t i=0;i<v[0].count;++i) {
            if (v[0].integers[i]<=0 || v[0].integers[i]>255) { neo_free(a->vm,copy); return NEO_INVALID; }
            copy[i]=(char)(uint8_t)v[0].integers[i];
        }
        if (!neo_text_document_valid(copy)) { neo_free(a->vm,copy); return NEO_INVALID; }
        *out=(neo_value){.kind=NEO_TEXT,.text=copy}; return NEO_OK;
    }
    if (v[0].kind != NEO_TEXT) { return NEO_WRONG_KIND; }
    const char *text=v[0].text; size_t length=strlen(text);
    neo_status status=neo_text_charge(a,length);
    if (status != NEO_OK) { return status; }
    out->kind=NEO_INTEGER;
    if (strcmp(op,"text-size") == 0) { out->integer=(int64_t)length; return NEO_OK; }
    if (strcmp(op,"text-lines") == 0) {
        out->integer=1; for (size_t i=0;i<length;++i) { if (text[i]=='\n') { ++out->integer; } } return NEO_OK;
    }
    if (strcmp(op,"text-index") == 0) {
        if (v[1].kind != NEO_INTEGER || v[2].kind != NEO_INTEGER) { return NEO_WRONG_KIND; }
        if (v[1].integer<0 || v[2].integer<0) { return NEO_INVALID; }
        size_t i=0; int64_t line=0;
        while (i<length && line<v[1].integer) { if (text[i++]=='\n') { ++line; } }
        size_t begin=i;
        while (i<length && text[i]!='\n' && (uint64_t)(i-begin)<(uint64_t)v[2].integer) { ++i; }
        while (!neo_text_boundary(text,length,i)) { --i; }
        out->integer=(int64_t)i; return NEO_OK;
    }
    if (strcmp(op,"text-find") == 0) {
        if (v[1].kind != NEO_TEXT || v[2].kind != NEO_INTEGER) { return NEO_WRONG_KIND; }
        if (v[2].integer<0 || (uint64_t)v[2].integer>length) { return NEO_INVALID; }
        const char *found=strstr(text+(size_t)v[2].integer,v[1].text);
        out->integer=found == NULL ? -1 : (int64_t)(found-text); return NEO_OK;
    }
    if (v[1].kind != NEO_INTEGER || v[1].integer<0) { return NEO_INVALID; }
    size_t at=(size_t)v[1].integer;
    if (strcmp(op,"text-line") == 0) {
        size_t begin=0; int64_t line=0;
        while (begin<length && line<v[1].integer) { if (text[begin++]=='\n') { ++line; } }
        size_t end=begin; while (end<length && text[end]!='\n') { ++end; }
        return neo_text_piece(a,text,begin,end-begin,out);
    }
    if (strcmp(op,"text-offset") == 0 && v[2].kind == NEO_INTEGER && v[2].integer == 0) {
        if (at>length) { at=length; }
        while (!neo_text_boundary(text,length,at)) { --at; }
        out->integer=(int64_t)at; return NEO_OK;
    }
    if (!neo_text_boundary(text,length,at)) { return NEO_INVALID; }
    if (strcmp(op,"text-position") == 0) {
        if (v[2].kind != NEO_TEXT) { return NEO_WRONG_KIND; }
        size_t begin=0; int64_t line=0;
        for (size_t i=0;i<at;++i) { if (text[i]=='\n') { ++line; begin=i+1u; } }
        if (strcmp(v[2].text,"line") == 0) { out->integer=line; }
        else if (strcmp(v[2].text,"column") == 0) { out->integer=(int64_t)(at-begin); }
        else { return NEO_INVALID; } return NEO_OK;
    }
    if (strcmp(op,"text-offset") == 0) {
        if (v[2].kind != NEO_INTEGER) { return NEO_WRONG_KIND; }
        if (v[2].integer<0 && at>0) { do { --at; } while (!neo_text_boundary(text,length,at)); }
        else if (v[2].integer>0 && at<length) { do { ++at; } while (!neo_text_boundary(text,length,at)); }
        out->integer=(int64_t)at; return NEO_OK;
    }
    if (v[2].kind != NEO_INTEGER || v[2].integer<0 || (uint64_t)v[2].integer>length-at) { return NEO_INVALID; }
    size_t count=(size_t)v[2].integer;
    if (!neo_text_boundary(text,length,at+count)) { return NEO_INVALID; }
    if (strcmp(op,"text-part") == 0) { return neo_text_piece(a,text,at,count,out); }
    if (v[3].kind != NEO_TEXT || !neo_text_document_valid(v[3].text)) { return NEO_INVALID; }
    size_t added=strlen(v[3].text);
    if (added>NEO_IMAGE_MAX_SOURCE_BYTES-(length-count)) { return NEO_LIMIT; }
    size_t total=length-count+added;
    status=neo_text_charge(a,total); if (status != NEO_OK) { return status; }
    char *copy=neo_alloc(a->vm,total+1u); if (copy == NULL) { return NEO_OUT_OF_MEMORY; }
    memcpy(copy,text,at); memcpy(copy+at,v[3].text,added); memcpy(copy+at+added,text+at+count,length-at-count);
    *out=(neo_value){.kind=NEO_TEXT,.text=copy}; return NEO_OK;
}
