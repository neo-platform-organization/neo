#include "weft.h"
#include "literal.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    const char *cursor;
    char *output;
    size_t length, capacity;
    unsigned depth;
} neo_weft;

static bool neo_word_char(char c) {
    return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
        (c>='0' && c<='9') || c=='_' || c=='-';
}

static void neo_space(neo_weft *weft) {
    for (;;) {
        while (*weft->cursor && strchr(" \t\r\n",*weft->cursor)) { ++weft->cursor; }
        if (weft->cursor[0]!='/' || weft->cursor[1]!='/') { return; }
        while (*weft->cursor && *weft->cursor!='\n') { ++weft->cursor; }
    }
}

static bool neo_take(neo_weft *weft, char token) {
    neo_space(weft);
    if (*weft->cursor!=token) { return false; }
    ++weft->cursor;
    return true;
}

static bool neo_keyword(neo_weft *weft, const char *word) {
    neo_space(weft);
    size_t length=strlen(word);
    if (strncmp(weft->cursor,word,length) || neo_word_char(weft->cursor[length])) {
        return false;
    }
    weft->cursor+=length;
    return true;
}

static bool neo_output(neo_weft *weft, const char *text, size_t length) {
    if (length>=weft->capacity-weft->length) { return false; }
    memcpy(weft->output+weft->length,text,length);
    weft->length+=length;
    weft->output[weft->length]=0;
    return true;
}

static bool neo_out(neo_weft *weft, const char *text) {
    return neo_output(weft,text,strlen(text));
}

static bool neo_word(neo_weft *weft) {
    neo_space(weft);
    const char *start=weft->cursor;
    while (neo_word_char(*weft->cursor)) { ++weft->cursor; }
    size_t length=(size_t)(weft->cursor-start);
    return length && length<64 && neo_output(weft,start,length);
}

static bool neo_expression(neo_weft *weft);

static bool neo_quoted_text(neo_weft *weft) {
    const char *start=weft->cursor;
    size_t bytes;
    return neo_text_literal(&weft->cursor,&bytes) &&
        neo_output(weft,start,(size_t)(weft->cursor-start));
}

static bool neo_f64_token(neo_weft *weft) {
    const char *start=weft->cursor;
    double value;
    size_t count;
    bool valid=start[1]=='[' ? neo_f64_vector_literal(&weft->cursor,&count,NULL) :
        neo_f64_literal(&weft->cursor,&value);
    return valid &&
        neo_output(weft,start,(size_t)(weft->cursor-start));
}
static bool neo_target(neo_weft *weft) {
    if (neo_take(weft,'(')) {
        return neo_expression(weft) && neo_take(weft,')');
    }
    bool reference=neo_take(weft,'@');
    return (!reference || neo_out(weft,"@")) && neo_word(weft);
}

static bool neo_arguments(neo_weft *weft) {
    if (neo_take(weft,')')) { return true; }
    do {
        if (!neo_out(weft," ") || !neo_expression(weft)) { return false; }
    } while (neo_take(weft,','));
    return neo_take(weft,')');
}

static bool neo_term(neo_weft *weft) {
    const char *const calls[]={"call-f64-vector","send-f64-vector","call-vector","send-vector","send-bytes","send-text",
                               "send-ref","send-f64","call-f64","post","send","call"};
    for (size_t i=0; i<sizeof(calls)/sizeof(calls[0]); ++i) {
        if (neo_keyword(weft,calls[i])) {
            return neo_out(weft,"(") && neo_out(weft,calls[i]) && neo_out(weft," ") &&
                neo_target(weft) && neo_take(weft,'.') && neo_out(weft," ") &&
                neo_word(weft) && neo_take(weft,'(') && neo_arguments(weft) && neo_out(weft,")");
        }
    }
    bool set=neo_keyword(weft,"set");
    if (set || neo_keyword(weft,"get")) {
        if (!neo_out(weft,set ? "(set " : "(get ") || !neo_target(weft) ||
            !neo_take(weft,'.') || !neo_out(weft," ") || !neo_word(weft)) { return false; }
        return (!set || (neo_take(weft,'=') && neo_out(weft," ") && neo_expression(weft))) &&
            neo_out(weft,")");
    }
    if (neo_keyword(weft,"if")) {
        if (*weft->cursor=='(' && neo_take(weft,'(')) {
            return neo_out(weft,"(if") && neo_arguments(weft) && neo_out(weft,")");
        }
        if (!neo_out(weft,"(if ") || !neo_expression(weft) || !neo_keyword(weft,"then") ||
            !neo_out(weft," ") || !neo_expression(weft)) { return false; }
        return (!neo_keyword(weft,"else") || (neo_out(weft," ") && neo_expression(weft))) &&
            neo_out(weft,")");
    }
    if (neo_keyword(weft,"while")) {
        if (*weft->cursor=='(' && neo_take(weft,'(')) {
            return neo_out(weft,"(while") && neo_arguments(weft) && neo_out(weft,")");
        }
        return neo_out(weft,"(while ") && neo_expression(weft) && neo_keyword(weft,"then") &&
            neo_out(weft," ") && neo_expression(weft) && neo_out(weft,")");
    }
    if (neo_keyword(weft,"return")) {
        return neo_out(weft,"(return ") && neo_expression(weft) && neo_out(weft,")");
    }

    if (neo_take(weft,'(')) {
        return neo_expression(weft) && neo_take(weft,')');
    }
    neo_space(weft);
    char op=*weft->cursor;
    if (op=='~') { return neo_f64_token(weft); }
    if (op=='"') { return neo_quoted_text(weft); }
    if (op=='@') { return neo_target(weft); }
    if (op=='[') {
        const char *start=weft->cursor;
        size_t count;
        return neo_vector_literal(&weft->cursor,&count,NULL) &&
            neo_output(weft,start,(size_t)(weft->cursor-start));
    }
    if (op=='#') {
        const char *start=weft->cursor;
        size_t bytes;
        return neo_bytes_literal(&weft->cursor,&bytes) &&
            !neo_word_char(*weft->cursor) &&
            neo_output(weft,start,(size_t)(weft->cursor-start));
    }
    if (op && strchr("+*=<>",op)) {
        ++weft->cursor;
        return neo_out(weft,"(") && neo_output(weft,&op,1) && neo_take(weft,'(') &&
            neo_arguments(weft) && neo_out(weft,")");
    }
    if (op=='-' && weft->cursor[1]=='(') {
        ++weft->cursor;
        return neo_out(weft,"(-") && neo_take(weft,'(') && neo_arguments(weft) &&
            neo_out(weft,")");
    }
    size_t start=weft->length;
    if (!neo_word(weft)) { return false; }
    if (neo_take(weft,'(')) {
        /* Named primitives such as read-document use ordinary call syntax. */
        if (!neo_out(weft," ")) { return false; }
        memmove(weft->output+start+1,weft->output+start,weft->length-start-1);
        weft->output[start]='(';
        return neo_arguments(weft) && neo_out(weft,")");
    }
    return true;
}

static unsigned neo_precedence(char op) {
    switch (op) {
        case '=': case '<': case '>': return 1;
        case '+': case '-': return 2;
        case '*': return 3;
        default: return 0;
    }
}

static bool neo_infix(neo_weft *weft, unsigned minimum) {
    if (weft->depth>=48) { return false; }
    ++weft->depth;
    size_t start=weft->length;
    bool valid=neo_term(weft);
    unsigned operators=0;
    while (valid) {
        neo_space(weft);
        char op=*weft->cursor;
        unsigned precedence=neo_precedence(op);
        if (precedence<minimum) { break; }
        if (++operators>=48) { valid=false; break; }
        ++weft->cursor;
        size_t left=weft->length-start;
        if (!neo_out(weft,"   ")) { valid=false; break; }
        memmove(weft->output+start+3,weft->output+start,left);
        weft->output[start]='(';
        weft->output[start+1]=op;
        weft->output[start+2]=' ';
        valid=neo_out(weft," ") && neo_infix(weft,precedence+1) && neo_out(weft,")");
    }
    --weft->depth;
    return valid;
}

static bool neo_expression(neo_weft *weft) {
    return neo_infix(weft,1);
}
/* Each binding scopes over the remainder of its block. */
static bool neo_body(neo_weft *weft, bool pure) {
    unsigned closes=0;
    bool sequence=false, expression=false;
    while (!neo_take(weft,'}')) {
        if (neo_keyword(weft,"let")) {
            if ((pure && expression) || closes>=48 || !neo_out(weft,"(let ") || !neo_word(weft) ||
                !neo_take(weft,'=') || !neo_out(weft," ") || !neo_expression(weft) ||
                !neo_take(weft,';') || !neo_out(weft," ")) { return false; }
            ++closes;
            sequence=false;
            expression=false;
            continue;
        }
        if (pure && expression) { return false; }
        if (!pure && !sequence) {
            if (closes>=48 || !neo_out(weft,"(do ")) { return false; }
            ++closes;
            sequence=true;
        }
        if (!neo_expression(weft) || !neo_take(weft,';') || !neo_out(weft," ")) {
            return false;
        }
        expression=true;
    }
    if (!expression) { return false; }
    while (closes) {
        if (!neo_out(weft,")")) { return false; }
        --closes;
    }
    return true;
}

static bool neo_declaration(neo_weft *weft) {
    if (neo_keyword(weft,"package")) {
        return neo_out(weft,"(package ") && neo_word(weft) && neo_take(weft,';') && neo_out(weft,")");
    }
    if (neo_keyword(weft,"export")) {
        return neo_out(weft,"(export ") && neo_word(weft) && neo_take(weft,';') && neo_out(weft,")");
    }
    if (neo_keyword(weft,"import")) {
        return neo_out(weft,"(import ") && neo_word(weft) && neo_take(weft,'=') &&
            neo_out(weft," ") && neo_target(weft) && neo_take(weft,'.') &&
            neo_out(weft," ") && neo_word(weft) && neo_take(weft,';') && neo_out(weft,")");
    }
    if (neo_keyword(weft,"slot")) {
        if (!neo_out(weft,"(slot ") || !neo_word(weft) || !neo_take(weft,'=') ||
            !neo_out(weft," ")) { return false; }
        neo_space(weft);
        bool valid;
        if (*weft->cursor=='~') {
            valid=neo_f64_token(weft);
        } else if (*weft->cursor=='@') {
            valid=neo_target(weft);
        } else if (*weft->cursor=='[') {
            const char *start=weft->cursor;
            size_t count;
            valid=neo_vector_literal(&weft->cursor,&count,NULL) &&
                neo_output(weft,start,(size_t)(weft->cursor-start));
        } else if (*weft->cursor=='#') {
            const char *start=weft->cursor;
            size_t bytes;
            valid=neo_bytes_literal(&weft->cursor,&bytes) &&
                neo_output(weft,start,(size_t)(weft->cursor-start));
        } else {
            valid=*weft->cursor=='"' ? neo_quoted_text(weft) : neo_word(weft);
        }
        return valid && neo_take(weft,';') && neo_out(weft,")");
    }
    if (neo_keyword(weft,"link")) {
        return neo_out(weft,"(link ") && neo_word(weft) && neo_take(weft,'=') &&
            neo_out(weft," @") && neo_take(weft,'@') && neo_word(weft) &&
            neo_take(weft,';') && neo_out(weft,")");
    }
    if (neo_keyword(weft,"delegate")) {
        return neo_out(weft,"(delegate @") && neo_take(weft,'@') && neo_word(weft) &&
            neo_take(weft,';') && neo_out(weft,")");
    }
    bool pure=neo_keyword(weft,"knight");
    if (!pure && !neo_keyword(weft,"handler")) { return false; }
    if (!neo_out(weft,pure ? "(knight " : "(handler ") || !neo_word(weft) ||
        !neo_take(weft,'(')) { return false; }
    if (!neo_take(weft,')')) {
        do {
            if (!neo_out(weft," ") || !neo_word(weft)) { return false; }
        } while (neo_take(weft,','));
        if (!neo_take(weft,')')) { return false; }
    }
    if (!neo_out(weft," ")) { return false; }
    if (neo_take(weft,'=')) {
        return neo_expression(weft) && neo_take(weft,';') && neo_out(weft,")");
    }
    return neo_take(weft,'{') && neo_body(weft,pure) && neo_out(weft,")");
}

const char *neo_weft_lower(const char *source, char *warp, size_t capacity) {
    neo_weft weft={source,warp,0,capacity,0};
    if (!neo_keyword(&weft,"entry") || !neo_out(&weft,"(warp 1 (entry ") ||
        !neo_word(&weft) || !neo_take(&weft,'.') || !neo_out(&weft," ") ||
        !neo_word(&weft) || !neo_take(&weft,';') || !neo_out(&weft,")")) {
        return "invalid Weft entry";
    }
    while (neo_keyword(&weft,"object")) {
        if (!neo_out(&weft,"\n(object ") || !neo_word(&weft) || !neo_take(&weft,'{')) {
            return "invalid Weft object";
        }
        while (!neo_take(&weft,'}')) {
            if (!neo_out(&weft,"\n  ") || !neo_declaration(&weft)) {
                return "invalid Weft declaration or body";
            }
        }
        if (!neo_out(&weft,")")) { return "Weft output exceeds limit"; }
    }
    neo_space(&weft);
    return *weft.cursor || !neo_out(&weft,")") ? "invalid Weft image or output exceeds limit" : NULL;
}
