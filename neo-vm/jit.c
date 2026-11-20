#include "platform.h"
#include "jit.h"
#include "weft.h"
#include "literal.h"

#include <limits.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum { NEO_DEPTH=NEO_TEXT_FRAMES, NEO_CELLS=NEO_GRAPH_CELLS, NEO_WRITE_MARK=NEO_CELLS, NEO_ARGUMENT=NEO_CELLS+1, NEO_TRACE_COUNT=NEO_ARGUMENT+NEO_MAX_ARGUMENTS, NEO_TRACE_BASE=NEO_TRACE_COUNT+1, NEO_TEXT_BASE=NEO_TRACE_BASE+2*NEO_TRACE_LIMIT, NEO_DEFERRED_BASE=NEO_NATIVE_SLOTS-NEO_DEFERRED_WORDS, NEO_LIFECYCLE=NEO_DEFERRED_BASE-2, NEO_BYTES=NEO_NATIVE_CODE_LIMIT, NEO_FRAME=NEO_MAX_ARGUMENTS*4, NEO_STACK=NEO_DEPTH*NEO_FRAME };
typedef struct {
    size_t child, next;
    char atom[64];
} neo_cell;
enum { NEO_IMPORT_GRANTS=32 };
typedef struct {
    char receiver[64], alias[64], target[64], selector[64];
    char caller_package[64], target_package[64];
    bool active;
} neo_import_grant;
struct neo_session {
    neo_cell cells[NEO_CELLS];
    const char *cursor;
    size_t count, objects, receiver, length, jumps;
    int32_t slots[NEO_CELLS];
    char text_slots[NEO_CELLS][64];
    size_t text_argument;
    char *text_result;
    char receive_kind;
    uint32_t text_destination;
    uint32_t requested_text;
    unsigned char code[NEO_BYTES];
    size_t overflow[NEO_CELLS];
    size_t returns[NEO_CELLS], return_count;
    bool tracing;
    size_t trace_count;
    neo_trace_event trace[NEO_TRACE_LIMIT];
    size_t root, entry;
    uint64_t revision;
    int32_t *document;
    neo_document_sink *document_sink;
    void *document_context;
    unsigned document_rights, compile_rights;
    size_t scope_object;
    bool checking;
    neo_import_grant imports[NEO_IMPORT_GRANTS];
    char document_package[64];
    neo_deferred_sink *schedule;
    void *schedule_context;
    bool scheduling;
    neo_buffer_sink *buffer;
    void *buffer_context;
    char buffer_package[64];
};
typedef neo_session neo_jit;

static void neo_skip(neo_jit *jit) {
    for (;;) {
        char c=*jit->cursor;
        if (c && strchr(" \r\n\t",c)) { ++jit->cursor; }
        else if (c=='/' && jit->cursor[1]=='/') {
            while (*jit->cursor && *jit->cursor!='\n') { ++jit->cursor; }
        } else { return; }
    }
}

static size_t neo_parse(neo_jit *jit, unsigned depth) {
    neo_skip(jit);
    if (depth>=NEO_DEPTH || jit->count+1>=NEO_CELLS || !*jit->cursor || *jit->cursor==')') {
        return 0;
    }
    size_t id=++jit->count;
    neo_cell *cell=&jit->cells[id];
    if (*jit->cursor=='(') {
        ++jit->cursor;
        neo_skip(jit);
        size_t *link=&cell->child;
        while (*jit->cursor!=')') {
            *link=neo_parse(jit,depth+1);
            if (!*link) { return 0; }
            link=&jit->cells[*link].next;
            neo_skip(jit);
        }
        ++jit->cursor;
        return cell->child ? id : 0;
    }
    if (*jit->cursor=='"' || *jit->cursor=='[' ||
        (jit->cursor[0]=='~' && jit->cursor[1]=='[')) {
        const char *start=jit->cursor;
        size_t bytes;
        bool valid=*start=='~' ? neo_f64_vector_literal(&jit->cursor,&bytes,NULL) :
            *start=='[' ? neo_vector_literal(&jit->cursor,&bytes,NULL) :
            neo_text_literal(&jit->cursor,&bytes);
        if (!valid) { return 0; }
        size_t length=(size_t)(jit->cursor-start);
        if (length>=sizeof(cell->atom)) { return 0; }
        memcpy(cell->atom,start,length);
        return id;
    }
    size_t length=0;
    while (*jit->cursor && !strchr("() \r\n\t",*jit->cursor)) {
        unsigned char c=(unsigned char)*jit->cursor++;
        if (length>=sizeof(cell->atom)-1 || c<33 || c>126 || c=='"') { return 0; }
        cell->atom[length++]=(char)c;
    }
    if (cell->atom[0]=='#') {
        const char *end=cell->atom;
        size_t bytes;
        if (!neo_bytes_literal(&end,&bytes) || *end) { return 0; }
    }
    if (cell->atom[0]=='~') {
        const char *end=cell->atom;
        double value;
        if (!neo_f64_literal(&end,&value) || *end) { return 0; }
    }
    return id;
}

static bool neo_is(const neo_jit *jit, size_t id, const char *atom) {
    return id && jit->cells[id].atom[0] && !strcmp(jit->cells[id].atom,atom);
}

static bool neo_name(const neo_cell *cell) {
    return cell->atom[0] && !strchr("\"#[~",cell->atom[0]);
}

static const char *neo_slot_literal(const neo_jit *jit, size_t slot) {
    size_t tag=jit->cells[slot].child;
    size_t label=jit->cells[tag].next;
    const char *value=jit->cells[jit->cells[label].next].atom;
    return *value && strchr("\"#[@~",*value) ? value : NULL;
}

static size_t neo_object(const neo_jit *jit, const char *name) {
    for (size_t id=jit->objects; id; id=jit->cells[id].next) {
        size_t tag=jit->cells[id].child;
        if (neo_is(jit,tag,"object") && neo_is(jit,jit->cells[tag].next,name)) { return id; }
    }
    return 0;
}

static size_t neo_member(const neo_jit *jit, size_t object, const char *kind, const char *name) {
    size_t tag=jit->cells[object].child;
    size_t identity=jit->cells[tag].next;
    for (size_t id=jit->cells[identity].next; id; id=jit->cells[id].next) {
        tag=jit->cells[id].child;
        size_t label=jit->cells[tag].next;
        if (neo_is(jit,tag,kind) && neo_is(jit,label,name)) { return id; }
    }
    return 0;
}

static const char *neo_package(const neo_jit *jit, size_t object) {
    size_t label=jit->cells[jit->cells[object].child].next;
    for (size_t item=jit->cells[label].next; item; item=jit->cells[item].next) {
        size_t tag=jit->cells[item].child;
        if (neo_is(jit,tag,"package")) { return jit->cells[jit->cells[tag].next].atom; }
    }
    return "root";
}

static bool neo_same_package(const neo_jit *jit, size_t first, size_t second) {
    return !strcmp(neo_package(jit,first),neo_package(jit,second));
}

static size_t neo_parent(const neo_jit *jit, size_t object) {
    size_t tag=jit->cells[object].child, name=jit->cells[tag].next;
    for (size_t item=jit->cells[name].next; item; item=jit->cells[item].next) {
        tag=jit->cells[item].child;
        if (neo_is(jit,tag,"delegate")) {
            const char *target=jit->cells[jit->cells[tag].next].atom;
            return *target=='@' ? neo_object(jit,target+1) : 0;
        }
    }
    return 0;
}

static size_t neo_behavior(const neo_jit *jit, size_t object, const char *kind, const char *name, size_t *owner) {
    for (size_t depth=0; object && depth<NEO_CELLS; ++depth) {
        size_t member=neo_member(jit,object,kind,name);
        if (member) {
            if (owner) { *owner=object; }
            return member;
        }
        object=neo_parent(jit,object);
    }
    return 0;
}

static size_t neo_receiver(const neo_jit *jit, size_t target, size_t self) {
    const char *name=jit->cells[target].atom;
    if (!strcmp(name,"self")) { return self; }
    return *name=='@' ? neo_object(jit,name+1) : 0;
}

static bool neo_import_binding(const neo_jit *jit, size_t object, size_t import,
                                neo_import_grant *binding) {
    size_t tag=jit->cells[import].child, alias=jit->cells[tag].next;
    size_t target=jit->cells[alias].next, selector=jit->cells[target].next;
    size_t receiver=neo_receiver(jit,target,0);
    if (!receiver || !selector) { return false; }
    *binding=(neo_import_grant){0};
    strcpy(binding->receiver,jit->cells[jit->cells[jit->cells[object].child].next].atom);
    strcpy(binding->alias,jit->cells[alias].atom);
    strcpy(binding->target,jit->cells[target].atom+1);
    strcpy(binding->selector,jit->cells[selector].atom);
    strcpy(binding->caller_package,neo_package(jit,object));
    strcpy(binding->target_package,neo_package(jit,receiver));
    binding->active=true;
    return true;
}

static bool neo_import_authorized(const neo_jit *jit, const neo_import_grant *binding) {
    for (size_t i=0; i<NEO_IMPORT_GRANTS; ++i) {
        const neo_import_grant *grant=&jit->imports[i];
        if (grant->active && !strcmp(grant->receiver,binding->receiver) &&
            !strcmp(grant->alias,binding->alias) && !strcmp(grant->target,binding->target) &&
            !strcmp(grant->selector,binding->selector) &&
            !strcmp(grant->caller_package,binding->caller_package) &&
            !strcmp(grant->target_package,binding->target_package)) { return true; }
    }
    return false;
}

/* The final form is the body. All preceding forms are positional names. */
static size_t neo_signature(const neo_jit *jit, size_t first, size_t *count) {
    *count=0;
    size_t id=first;
    for (; jit->cells[id].next; id=jit->cells[id].next) {
        const char *name=jit->cells[id].atom;
        if (!neo_name(&jit->cells[id]) || ++*count>NEO_MAX_ARGUMENTS) { return 0; }
        for (size_t prior=first; prior!=id; prior=jit->cells[prior].next) {
            if (neo_is(jit,prior,name)) { return 0; }
        }
    }
    return id;
}
static bool neo_validate(const neo_jit *jit) {
    if (!jit->objects) { return false; }
    for (size_t id=jit->objects; id; id=jit->cells[id].next) {
        size_t tag=jit->cells[id].child, name=jit->cells[tag].next;
        if (!neo_name(&jit->cells[name]) || neo_object(jit,jit->cells[name].atom)!=id) {
            return false;
        }
        bool delegated=false;
        bool packaged=false;
        for (size_t item=jit->cells[name].next; item; item=jit->cells[item].next) {
            tag=jit->cells[item].child;
            size_t label=jit->cells[tag].next, value=jit->cells[label].next;
            if (neo_is(jit,tag,"package")) {
                if (packaged || !neo_name(&jit->cells[label]) || value) { return false; }
                packaged=true;
                continue;
            }
            if (neo_is(jit,tag,"export")) {
                if (!neo_name(&jit->cells[label]) || value ||
                    neo_member(jit,id,"export",jit->cells[label].atom)!=item ||
                    !neo_behavior(jit,id,"handler",jit->cells[label].atom,NULL)) { return false; }
                continue;
            }
            if (neo_is(jit,tag,"import")) {
                size_t selector=jit->cells[value].next;
                size_t target=neo_receiver(jit,value,0);
                if (!neo_name(&jit->cells[label]) || !target || !neo_name(&jit->cells[selector]) ||
                    jit->cells[selector].next ||
                    neo_member(jit,id,"import",jit->cells[label].atom)!=item ||
                    !neo_member(jit,target,"export",jit->cells[selector].atom)) { return false; }
                continue;
            }
            if (neo_is(jit,tag,"delegate")) {
                size_t target=neo_receiver(jit,label,0);
                if (delegated || value || !target || !neo_same_package(jit,id,target)) { return false; }
                delegated=true;
                continue;
            }
            if (!neo_name(&jit->cells[label]) ||
                neo_member(jit,id,jit->cells[tag].atom,jit->cells[label].atom)!=item || !value) {
                return false;
            }
            if (neo_is(jit,tag,"link")) {
                const char *reference=jit->cells[value].atom;
                size_t target=*reference=='@' ? neo_object(jit,reference+1) : 0;
                if (!target || !neo_same_package(jit,id,target)) { return false; }
            } else if (neo_is(jit,tag,"handler") || neo_is(jit,tag,"knight")) {
                size_t count;
                value=neo_signature(jit,value,&count);
            } else if (!neo_is(jit,tag,"slot")) { return false; }
            if (!value || jit->cells[value].next) { return false; }
        }
        size_t slow=id, fast=id;
        do {
            slow=neo_parent(jit,slow);
            fast=neo_parent(jit,neo_parent(jit,fast));
            if (slow && slow==fast) { return false; }
        } while (slow && fast);
    }
    return true;
}

static bool neo_emit(neo_jit *jit, const unsigned char *bytes, size_t count) {
    if (count>sizeof(jit->code)-jit->length) { return false; }
    memcpy(jit->code+jit->length,bytes,count);
    jit->length+=count;
    return true;
}
#define NEO_EMIT(jit, ...) neo_emit(jit,(const unsigned char[]){__VA_ARGS__}, \
                                       sizeof((const unsigned char[]){__VA_ARGS__}))

static bool neo_u32(neo_jit *jit, uint32_t value) {
    return NEO_EMIT(jit,(unsigned char)value,(unsigned char)(value>>8),
                    (unsigned char)(value>>16),(unsigned char)(value>>24));
}

static void neo_patch_branch(neo_jit *jit, size_t offset, size_t target) {
    uint32_t distance=(uint32_t)(target-offset-4);
    for (unsigned byte=0; byte<4; ++byte) {
        jit->code[offset+byte]=(unsigned char)(distance>>(byte*8));
    }
}

static const char *neo_integer(const char *text, int32_t *result) {
    bool negative=*text=='-';
    if (negative) { ++text; }
    if (!*text) { return "expected a signed 32-bit integer"; }
    int64_t value=0;
    for (; *text; ++text) {
        if (*text<'0' || *text>'9') { return "expected a signed 32-bit integer"; }
        value=value*10+(*text-'0');
        if (value>(int64_t)INT32_MAX+negative) { return "integer overflow"; }
    }
    *result=(int32_t)(negative ? -value : value);
    return NULL;
}

typedef struct neo_binding {
    const char *name;
    uint32_t offset;
    const struct neo_binding *outer;
    size_t literal;
    uint32_t text_offset;
    char record_kind;
} neo_binding;

static const neo_binding *neo_find_binding(const neo_binding *binding, const char *name) {
    for (; binding; binding=binding->outer) {
        if (!strcmp(name,binding->name)) { return binding; }
    }
    return NULL;
}

static size_t neo_bound_literal(const neo_jit *jit, size_t form, const neo_binding *scope) {
    const char *atom=jit->cells[form].atom;
    if (*atom && strchr("\"#[@~",*atom)) { return form; }
    const neo_binding *binding=neo_find_binding(scope,atom);
    return binding ? binding->literal : 0;
}

static size_t neo_bound_receiver(const neo_jit *jit, size_t form,
                                 const neo_binding *scope, size_t self) {
    const char *atom=jit->cells[form].atom;
        if (*atom=='@') {
        size_t target=neo_object(jit,atom+1);
        return target && neo_same_package(jit,jit->scope_object,target) ? target : 0;
    }
    if (!strcmp(atom,"self")) { return self; }
    const neo_binding *binding=neo_find_binding(scope,atom);
    if (!binding) { return 0; }
    const char *literal=jit->cells[binding->literal].atom;
        size_t target=*literal=='@' ? neo_object(jit,literal+1) : 0;
    return target && neo_same_package(jit,jit->scope_object,target) ? target : 0;
}

static size_t neo_argument_literal(const neo_jit *jit, size_t form,
                                   const neo_binding *scope, size_t self) {
    size_t literal=neo_bound_literal(jit,form,scope);
    size_t op=jit->cells[form].child;
    if (!self || !neo_is(jit,op,"link-ref")) { return literal; }
    size_t target=jit->cells[op].next, name=jit->cells[target].next;
    size_t owner=neo_bound_receiver(jit,target,scope,self);
    if (!owner || !neo_name(&jit->cells[name]) || jit->cells[name].next) { return 0; }
    size_t link=neo_member(jit,owner,"link",jit->cells[name].atom);
    size_t label=jit->cells[jit->cells[link].child].next;
    return jit->cells[label].next;
}

enum { NEO_TEXT_TOKEN_OFFSET=8+NEO_VECTOR_LIMIT*8,
       NEO_TEXT_RESULT=NEO_CELLS+NEO_DEPTH*NEO_MAX_ARGUMENTS };
_Static_assert(NEO_TEXT_WORDS*4>=NEO_TEXT_TOKEN_OFFSET+sizeof(((neo_cell *)0)->atom),
               "text staging record must hold the serialized atom");
_Static_assert(NEO_TEXT_WORDS*4%16==0, "text copy uses whole 16-byte chunks");

static uint32_t neo_text_record(size_t slot) {
    return (uint32_t)(NEO_TEXT_BASE+slot*NEO_TEXT_WORDS)*4u;
}

static uint32_t neo_text_location(const neo_jit *jit, size_t form,
                                  const neo_binding *scope, size_t self, char kind) {
    const neo_binding *binding=neo_find_binding(scope,jit->cells[form].atom);
    if (binding && binding->text_offset && binding->record_kind==kind) { return binding->text_offset; }
    size_t op=jit->cells[form].child;
    if (!self || !neo_is(jit,op,"get")) { return 0; }
    size_t target=jit->cells[op].next, label=jit->cells[target].next;
    if (!neo_name(&jit->cells[label]) || jit->cells[label].next) { return 0; }
    size_t object=neo_bound_receiver(jit,target,scope,self);
    size_t slot=neo_member(jit,object,"slot",jit->cells[label].atom);
    const char *literal=neo_slot_literal(jit,slot);
    if (kind=='f' && literal && literal[0]=='~' && literal[1]=='[') { return 0; }
    if (kind=='d') { return literal && literal[0]=='~' && literal[1]=='[' ? neo_text_record(slot) : 0; }
    char slot_kind=kind=='f' ? '~' : kind;
    return literal && *literal==slot_kind ? neo_text_record(slot) : 0;
}

static bool neo_emit_text_copy(neo_jit *jit, uint32_t destination, uint32_t source) {
    /* MOVDQU between staging records using volatile XMM0. */
    for (uint32_t byte=0; byte<NEO_TEXT_WORDS*4u; byte+=16) {
        if (!NEO_EMIT(jit,0xf3,0x41,0x0f,0x6f,0x83) || !neo_u32(jit,source+byte) ||
            !NEO_EMIT(jit,0xf3,0x41,0x0f,0x7f,0x83) || !neo_u32(jit,destination+byte)) {
            return false;
        }
    }
    return true;
}
static void neo_finish_returns(neo_jit *jit, size_t first) {
    while (jit->return_count>first) {
        neo_patch_branch(jit,jit->returns[--jit->return_count],jit->length);
    }
}
static const char *neo_object_name(const neo_jit *jit, size_t object) {
    return object ? jit->cells[jit->cells[jit->cells[object].child].next].atom : NULL;
}

static bool neo_mark_trace(neo_jit *jit, neo_trace_kind kind, size_t sender,
                           size_t receiver, size_t owner, const char *name) {
    if (!jit->tracing) { return true; }
    if (jit->trace_count==NEO_TRACE_LIMIT) { return false; }
    uint32_t site=(uint32_t)jit->trace_count+1u;
    jit->trace[jit->trace_count++]=(neo_trace_event){
        kind,neo_object_name(jit,sender),neo_object_name(jit,receiver),name,
        neo_object_name(jit,owner),0,false
    };
    /* R8D indexes the bounded event stream. Preserve EAX, the expression value. */
    if (!NEO_EMIT(jit,0x45,0x8b,0x83) || !neo_u32(jit,NEO_TRACE_COUNT*4u) ||
        !NEO_EMIT(jit,0x41,0x81,0xf8) || !neo_u32(jit,NEO_TRACE_LIMIT) ||
        !NEO_EMIT(jit,0x0f,0x83,0,0,0,0)) { return false; }
    size_t full=jit->length-4;
    if (!NEO_EMIT(jit,0x43,0xc7,0x84,0xc3) || !neo_u32(jit,NEO_TRACE_BASE*4u) ||
        !neo_u32(jit,site)) { return false; }
    if (kind==NEO_TRACE_WRITE &&
        (!NEO_EMIT(jit,0x43,0x89,0x84,0xc3) || !neo_u32(jit,NEO_TRACE_BASE*4u+4u))) {
        return false;
    }
    if (!NEO_EMIT(jit,0x41,0xff,0xc0,0x45,0x89,0x83) ||
        !neo_u32(jit,NEO_TRACE_COUNT*4u)) { return false; }
    neo_patch_branch(jit,full,jit->length);
    return true;
}
static bool neo_deferred_store(neo_jit *jit, uint32_t word) {
    /* EAX holds the value; ECX selects the current 32-byte request record. */
    return NEO_EMIT(jit,0x41,0x8b,0x8b) && neo_u32(jit,NEO_DEFERRED_BASE*4u) &&
        NEO_EMIT(jit,0xc1,0xe1,0x05,0x41,0x89,0x84,0x0b) &&
        neo_u32(jit,(NEO_DEFERRED_BASE+1u+word)*4u);
}

static const char *neo_compile(neo_jit *jit, size_t form, const neo_binding *parameter,
                               size_t self, unsigned depth);

static unsigned char neo_f64_instruction(const neo_jit *jit, size_t op) {
    if (neo_is(jit,op,"f64-add")) { return 0x58; }
    if (neo_is(jit,op,"f64-sub")) { return 0x5c; }
    if (neo_is(jit,op,"f64-mul")) { return 0x59; }
    if (neo_is(jit,op,"f64-div")) { return 0x5e; }
    return 0;
}

static const char *neo_compile_sequence_source(neo_jit *jit, size_t form, const neo_binding *scope,
                                           size_t self, unsigned depth, char kind, uint32_t *offset);

static const char *neo_compile_f64(neo_jit *jit, size_t form, const neo_binding *scope,
                                   size_t self, unsigned depth) {
#ifdef NEO_INTEGER_PROFILE
    (void)jit; (void)form; (void)scope; (void)self; (void)depth;
    return "binary64 is unavailable in the initial standalone integer profile";
#else
    if (!form || depth>=NEO_DEPTH) { return "invalid binary64 expression"; }
    uint32_t offset;
    const char *failure=neo_compile_sequence_source(jit,form,scope,self,depth,'f',&offset);
    if (failure) { return failure; }
    if (offset) {
        return NEO_EMIT(jit,0xf2,0x41,0x0f,0x10,0x83) && neo_u32(jit,offset+8u) ?
            NULL : "code limit exceeded";
    }
    size_t op=jit->cells[form].child;
    size_t first=jit->cells[op].next, second=jit->cells[first].next;
    const char *atom=jit->cells[neo_bound_literal(jit,form,scope)].atom;
    if (neo_is(jit,op,"f64-vector-at")) {
        uint32_t source=0;
        failure=neo_compile_sequence_source(jit,first,scope,self,depth,'d',&source);
        if (failure) { return failure; }
        if (source) {
            if (!second || jit->cells[second].next) { return "expected vector and integer index"; }
            failure=neo_compile(jit,second,scope,self,depth+1);
            if (failure) { return failure; }
            if (!NEO_EMIT(jit,0x41,0x3b,0x83) || !neo_u32(jit,source+4u) ||
                !NEO_EMIT(jit,0x0f,0x82,0,0,0,0)) { return "code limit exceeded"; }
            size_t valid=jit->length-4;
            if (!NEO_EMIT(jit,0x48,0xb8,6,0,0,0,0,0,0,0x80,0x48,0x81,0xc4) ||
                !neo_u32(jit,NEO_STACK) || !NEO_EMIT(jit,0xc3)) { return "code limit exceeded"; }
            neo_patch_branch(jit,valid,jit->length);
            return NEO_EMIT(jit,0xf2,0x41,0x0f,0x10,0x84,0xc3) && neo_u32(jit,source+8u) ?
                NULL : "code limit exceeded";
        }
        const char *literal=jit->cells[neo_bound_literal(jit,first,scope)].atom;
        double values[NEO_VECTOR_LIMIT];
        size_t count;
        if (!second || jit->cells[second].next ||
            !neo_f64_vector_literal(&literal,&count,values) || *literal) {
            return "f64-vector-at expects a binary64 vector and integer index";
        }
        failure=neo_compile(jit,second,scope,self,depth+1);
        if (failure) { return failure; }
        if (!NEO_EMIT(jit,0x83,0xf8,(unsigned char)count,0x0f,0x83,0,0,0,0)) {
            return "code limit exceeded";
        }
        size_t bounds=jit->length-4;
        if (!NEO_EMIT(jit,0x48,0x8d,0x0d,0,0,0,0)) { return "code limit exceeded"; }
        size_t table=jit->length-4;
        if (!NEO_EMIT(jit,0xf2,0x0f,0x10,0x04,0xc1,0xe9,0,0,0,0)) {
            return "code limit exceeded";
        }
        size_t done=jit->length-4;
        neo_patch_branch(jit,bounds,jit->length);
        if (!NEO_EMIT(jit,0x48,0xb8,6,0,0,0,0,0,0,0x80,0x48,0x81,0xc4) ||
            !neo_u32(jit,NEO_STACK) || !NEO_EMIT(jit,0xc3)) { return "code limit exceeded"; }
        neo_patch_branch(jit,table,jit->length);
        for (size_t i=0; i<count; ++i) {
            uint64_t bits;
            memcpy(&bits,&values[i],sizeof(bits));
            if (!neo_u32(jit,(uint32_t)bits) || !neo_u32(jit,(uint32_t)(bits>>32))) {
                return "code limit exceeded";
            }
        }
        neo_patch_branch(jit,done,jit->length);
        return NULL;
    }
    if (*atom=='~' || neo_is(jit,op,"f64")) {
        double value;
        if (*atom=='~') {
            if (!neo_f64_literal(&atom,&value) || *atom) { return "invalid finite binary64 literal"; }
        } else {
            const char *literal=jit->cells[neo_bound_literal(jit,first,scope)].atom;
            if (*literal!='"' || second) { return "f64 expects a quoted decimal"; }
            const char *start=literal+1;
            char *end;
            value=strtod(start,&end);
            if (end==start || *end!='"' || end[1] ||
                strspn(start,"0123456789+-.eE")!=(size_t)(end-start) || !isfinite(value)) {
                return "invalid finite binary64 literal";
            }
        }
        uint64_t bits;
        _Static_assert(sizeof(bits)==sizeof(value),"binary64 host representation");
        memcpy(&bits,&value,sizeof(bits));
        return NEO_EMIT(jit,0x48,0xb8) && neo_u32(jit,(uint32_t)bits) &&
            neo_u32(jit,(uint32_t)(bits>>32)) && NEO_EMIT(jit,0x66,0x48,0x0f,0x6e,0xc0) ?
            NULL : "code limit exceeded";
    }
    if (neo_is(jit,op,"int-to-f64")) {
        if (!first || second) { return "int-to-f64 expects one integer"; }
        failure=neo_compile(jit,first,scope,0,depth+1);
        if (failure) { return failure; }
        return NEO_EMIT(jit,0xf2,0x0f,0x2a,0xc0) ? NULL : "code limit exceeded";
    }
    unsigned char instruction=neo_f64_instruction(jit,op);
    if (!instruction || !first || !second || jit->cells[second].next) {
        return "expected a binary64 operation with two operands";
    }
    failure=neo_compile_f64(jit,first,scope,self,depth+1);
    if (failure) { return failure; }
    uint32_t saved=depth*NEO_FRAME;
    if (!NEO_EMIT(jit,0xf2,0x0f,0x11,0x84,0x24) || !neo_u32(jit,saved)) {
        return "code limit exceeded";
    }
    failure=neo_compile_f64(jit,second,scope,self,depth+1);
    if (failure) { return failure; }
    return NEO_EMIT(jit,0xf2,0x0f,0x10,0xc8,0xf2,0x0f,0x10,0x84,0x24) &&
        neo_u32(jit,saved) && NEO_EMIT(jit,0xf2,0x0f,instruction,0xc1) ?
        NULL : "code limit exceeded";
#endif
}

static const char *neo_compile_sequence_source(neo_jit *jit, size_t form, const neo_binding *scope,
                                           size_t self, unsigned depth, char kind, uint32_t *offset) {
    *offset=neo_text_location(jit,form,scope,self,kind);
    size_t op=jit->cells[form].child;
    if (kind=='d' && neo_is(jit,op,"f64-vector")) {
        if (depth+1>=NEO_DEPTH) { return "vector construction depth exceeded"; }
        *offset=neo_text_record(NEO_TEXT_RESULT+1u+depth);
        uint32_t count=0;
        for (size_t arg=jit->cells[op].next; arg; arg=jit->cells[arg].next) {
            if (count==NEO_VECTOR_LIMIT) { return "binary64 vector element limit exceeded"; }
            const char *failure=neo_compile_f64(jit,arg,scope,self,depth+1);
            if (failure) { return failure; }
            if (!NEO_EMIT(jit,0xf2,0x41,0x0f,0x11,0x83) ||
                !neo_u32(jit,*offset+8u+count*8u)) { return "code limit exceeded"; }
            ++count;
        }
        return NEO_EMIT(jit,0x41,0xc7,0x83) && neo_u32(jit,*offset+4u) &&
            neo_u32(jit,count) && NEO_EMIT(jit,0x41,0xc6,0x83) &&
            neo_u32(jit,*offset+NEO_TEXT_TOKEN_OFFSET) && NEO_EMIT(jit,0) ?
            NULL : "code limit exceeded";
    }
    const char *send=kind=='d' ? "send-f64-vector" : kind=='f' ? "send-f64" : kind=='@' ? "send-ref" :
        kind=='[' ? "send-vector" : kind=='#' ? "send-bytes" : "send-text";
    if (!neo_is(jit,op,send) && !(kind=='[' && neo_is(jit,op,"call-vector")) &&
        !(kind=='f' && neo_is(jit,op,"call-f64")) &&
        !(kind=='d' && neo_is(jit,op,"call-f64-vector"))) { return NULL; }
    *offset=neo_text_record(NEO_TEXT_RESULT+1u+depth);
    jit->requested_text=*offset;
    const char *failure=neo_compile(jit,form,scope,self,depth+1);
    jit->requested_text=0;
    return failure;
}
static const char *neo_emit_reference(neo_jit *jit, size_t form,
                                      const neo_binding *scope, size_t self, unsigned depth) {
    uint32_t offset;
    const char *failure=neo_compile_sequence_source(jit,form,scope,self,depth,'@',&offset);
    if (failure) { return failure; }
    if (offset) {
        return NEO_EMIT(jit,0x41,0x8b,0x83) && neo_u32(jit,offset+4u) ?
            NULL : "code limit exceeded";
    }
    size_t literal=neo_argument_literal(jit,form,scope,self);
    const char *name=jit->cells[literal].atom;
    size_t object=*name=='@' ? neo_object(jit,name+1) : 0;
    if (!object || !neo_same_package(jit,jit->scope_object,object)) {
        return "expected a reference within the package";
    }
    return NEO_EMIT(jit,0xb8) && neo_u32(jit,(uint32_t)object) ?
        NULL : "code limit exceeded";
}

static const char *neo_bind_value(neo_jit *jit, size_t form, const neo_binding *parameter,
                                   size_t self, unsigned depth, size_t index, neo_binding *binding) {
            uint32_t source;
            const char *source_failure=neo_compile_sequence_source(jit,form,parameter,self,depth,'"',&source);
            if (source_failure) { return source_failure; }
            char kind='"';
            if (!source) {
                source_failure=neo_compile_sequence_source(jit,form,parameter,self,depth,'#',&source);
                if (source_failure) { return source_failure; }
                kind='#';
            }
            if (!source) {
                source_failure=neo_compile_sequence_source(jit,form,parameter,self,depth,'[',&source);
                if (source_failure) { return source_failure; }
                kind='[';
            }
            if (!source && self) {
                source_failure=neo_compile_sequence_source(jit,form,parameter,self,depth,'@',&source);
                if (source_failure) { return source_failure; }
                kind='@';
            }
            size_t argument_op=jit->cells[form].child;
            const neo_binding *bound=neo_find_binding(parameter,jit->cells[form].atom);
            bool floating=!source && ((bound && bound->record_kind=='f') ||
                (jit->cells[binding->literal].atom[0]=='~' && jit->cells[binding->literal].atom[1]!='[') ||
                neo_text_location(jit,form,parameter,self,'f') ||
                neo_is(jit,argument_op,"f64") || neo_is(jit,argument_op,"int-to-f64") ||
                neo_is(jit,argument_op,"call-f64") || neo_is(jit,argument_op,"send-f64") ||
                neo_f64_instruction(jit,argument_op));
            if (!source && !floating) {
                source_failure=neo_compile_sequence_source(jit,form,parameter,self,depth,'d',&source);
                if (source_failure) { return source_failure; }
                kind='d';
            }
            if (source) {
                binding->record_kind=kind;
                uint32_t argument_record=neo_text_record(NEO_CELLS+depth*NEO_MAX_ARGUMENTS+index);
                if (!neo_emit_text_copy(jit,argument_record,source)) { return "code limit exceeded"; }
                binding->text_offset=argument_record;
            } else if (floating) {
                const char *failure=neo_compile_f64(jit,form,parameter,self,depth+1);
                if (failure) { return failure; }
                uint32_t record=neo_text_record(NEO_CELLS+depth*NEO_MAX_ARGUMENTS+index);
                if (!NEO_EMIT(jit,0xf2,0x41,0x0f,0x11,0x83) || !neo_u32(jit,record+8u)) {
                    return "code limit exceeded";
                }
                binding->record_kind='f';
                binding->text_offset=record;
            } else if (!binding->literal) {
                const char *failure=neo_compile(jit,form,parameter,self,depth+1);
                if (failure) { return failure; }
                if (!NEO_EMIT(jit,0x89,0x84,0x24) || !neo_u32(jit,binding->offset)) {
                    return "code limit exceeded";
                }
            }
    return NULL;
}

static const char *neo_compile(neo_jit *jit, size_t form, const neo_binding *parameter,
                               size_t self, unsigned depth) {
    if (depth>=NEO_DEPTH || !form) { return "invalid form or compilation depth exceeded"; }
    const neo_cell *cell=&jit->cells[form];
    if (!cell->child) {
        const char *text=cell->atom;
        const neo_binding *binding=neo_find_binding(parameter,text);
        if (binding) {
            if (binding->literal || binding->text_offset) { return "literal value requires a matching operation"; }
            return NEO_EMIT(jit,0x8b,0x84,0x24) && neo_u32(jit,binding->offset) ?
                NULL : "code limit exceeded";
        }
        int32_t value;
        const char *failure=neo_integer(text,&value);
        if (failure) { return failure; }
        return NEO_EMIT(jit,0xb8) && neo_u32(jit,(uint32_t)value) ?
            NULL : "code limit exceeded";
    }
    size_t op=cell->child, a=jit->cells[op].next, b=jit->cells[a].next, c=jit->cells[b].next;
    bool fill_buffer=neo_is(jit,op,"buffer-fill"), present_buffer=neo_is(jit,op,"buffer-present");
    if (fill_buffer || present_buffer) {
        if (!self || jit->scheduling) { return "buffer operations require a handler"; }
        if (!jit->checking && (!jit->buffer || strcmp(neo_package(jit,self),jit->buffer_package))) {
            return "buffer access denied";
        }
        if (fill_buffer ? !a || !b || !c || jit->cells[c].next : a!=0) { return "invalid buffer operands"; }
        if (!NEO_EMIT(jit,0x41,0x83,0xbb) || !neo_u32(jit,NEO_DEFERRED_BASE*4u) ||
            !NEO_EMIT(jit,NEO_DEFERRED_LIMIT,0x72,0x12,0x48,0xb8,10,0,0,0,0,0,0,0x80,0x48,0x81,0xc4) ||
            !neo_u32(jit,NEO_STACK) || !NEO_EMIT(jit,0xc3)) { return "code limit exceeded"; }
        const uint32_t fields[]={(uint32_t)form,(uint32_t)self,0,fill_buffer ? 3u : 0u};
        for (uint32_t i=0; i<4; ++i) {
            if (!NEO_EMIT(jit,0xb8) || !neo_u32(jit,fields[i]) || !neo_deferred_store(jit,i)) { return "code limit exceeded"; }
        }
        jit->scheduling=true;
        for (uint32_t i=0; i<(fill_buffer ? 3u : 0u); ++i) {
            const char *failure=neo_compile(jit,a,parameter,self,depth+1);
            if (failure) { jit->scheduling=false; return failure; }
            if (!neo_deferred_store(jit,4u+i)) { jit->scheduling=false; return "code limit exceeded"; }
            a=jit->cells[a].next;
        }
        jit->scheduling=false;
        return NEO_EMIT(jit,0x41,0xff,0x83) && neo_u32(jit,NEO_DEFERRED_BASE*4u) &&
            NEO_EMIT(jit,0xb8,1,0,0,0) ? NULL : "code limit exceeded";
    }
    bool delayed=neo_is(jit,op,"after");
    if (delayed || neo_is(jit,op,"post")) {
        if (!self || jit->scheduling) { return "scheduling requires a handler and cannot nest inside an argument"; }
        int32_t delay=0;
        size_t target=delayed ? b : a, selector=jit->cells[target].next;
        size_t arguments=jit->cells[selector].next;
        if (delayed && (neo_integer(jit->cells[a].atom,&delay) || delay<0)) {
            return "after expects a nonnegative literal millisecond delay";
        }
        size_t receiver=neo_receiver(jit,target,self), owner;
        size_t handler=neo_behavior(jit,receiver,"handler",jit->cells[selector].atom,&owner);
        if (!receiver || !neo_same_package(jit,self,receiver)) { return "scheduled message crosses package boundary"; }
        if (!handler) { return "unresolved scheduled receiver or method"; }
        size_t label=jit->cells[jit->cells[handler].child].next, expected;
        neo_signature(jit,jit->cells[label].next,&expected);
        size_t count=0;
        for (size_t arg=arguments; arg; arg=jit->cells[arg].next) { ++count; }
        if (count!=expected || count>NEO_MAX_ARGUMENTS) { return "wrong scheduled argument count"; }
        if (!NEO_EMIT(jit,0x41,0x83,0xbb) || !neo_u32(jit,NEO_DEFERRED_BASE*4u) ||
            !NEO_EMIT(jit,NEO_DEFERRED_LIMIT,0x72,0x12,0x48,0xb8,10,0,0,0,0,0,0,0x80,0x48,0x81,0xc4) ||
            !neo_u32(jit,NEO_STACK) || !NEO_EMIT(jit,0xc3)) { return "code limit exceeded"; }
        const uint32_t fields[]={(uint32_t)form,(uint32_t)receiver,(uint32_t)delay,(uint32_t)count};
        for (uint32_t i=0; i<4; ++i) {
            if (!NEO_EMIT(jit,0xb8) || !neo_u32(jit,fields[i]) || !neo_deferred_store(jit,i)) {
                return "code limit exceeded";
            }
        }
        jit->scheduling=true;
        for (uint32_t i=0; i<(uint32_t)count; ++i) {
            const char *failure=neo_compile(jit,arguments,parameter,self,depth+1);
            if (failure) { jit->scheduling=false; return failure; }
            if (!neo_deferred_store(jit,4u+i)) { jit->scheduling=false; return "code limit exceeded"; }
            arguments=jit->cells[arguments].next;
        }
        jit->scheduling=false;
        return NEO_EMIT(jit,0x41,0xff,0x83) && neo_u32(jit,NEO_DEFERRED_BASE*4u) &&
            NEO_EMIT(jit,0xb8,1,0,0,0) ? NULL : "code limit exceeded";
    }
    bool clone=neo_is(jit,op,"clone");
    if (clone || neo_is(jit,op,"delete")) {
        if (!self || !neo_receiver(jit,a,self) ||
            !neo_same_package(jit,jit->scope_object,neo_receiver(jit,a,self)) ||
            (clone ? !neo_name(&jit->cells[b]) || c : b!=0)) {
            return "invalid object lifecycle operands";
        }
        if (!NEO_EMIT(jit,0x41,0x83,0xbb) || !neo_u32(jit,NEO_LIFECYCLE*4u) ||
            !NEO_EMIT(jit,0,0x74,0x12,0x48,0xb8,9,0,0,0,0,0,0,0x80,0x48,0x81,0xc4) ||
            !neo_u32(jit,NEO_STACK) || !NEO_EMIT(jit,0xc3,0x41,0xc7,0x83) ||
            !neo_u32(jit,NEO_LIFECYCLE*4u) || !neo_u32(jit,(uint32_t)form) ||
            !NEO_EMIT(jit,0x41,0xc7,0x83) || !neo_u32(jit,(NEO_LIFECYCLE+1u)*4u) ||
            !neo_u32(jit,(uint32_t)neo_receiver(jit,a,self)) ||
            !NEO_EMIT(jit,0xb8) || !neo_u32(jit,1)) { return "code limit exceeded"; }
        return NULL;
    }
    if (neo_is(jit,op,"f64-vector-length")) {
        const char *literal=jit->cells[neo_bound_literal(jit,a,parameter)].atom;
        size_t count;
        if (!a || b || !neo_f64_vector_literal(&literal,&count,NULL) || *literal) {
            return "f64-vector-length expects a binary64 vector";
        }
        return NEO_EMIT(jit,0xb8) && neo_u32(jit,(uint32_t)count) ? NULL : "code limit exceeded";
    }
    if (neo_is(jit,op,"f64-to-int")) {
        if (self || !a || b) { return "f64-to-int expects one Knight expression"; }
        const char *failure=neo_compile_f64(jit,a,parameter,self,depth+1);
        if (failure) { return failure; }
        /* Truncate to signed 64 bits, then check that signed 32 bits suffice. */
        if (jit->jumps>=NEO_CELLS ||
            !NEO_EMIT(jit,0xf2,0x48,0x0f,0x2c,0xc0,0x48,0x63,0xc8,0x48,0x39,0xc8,
                      0x0f,0x85,0,0,0,0)) {
            return "code limit exceeded";
        }
        jit->overflow[jit->jumps++]=jit->length-4;
        return NULL;
    }
    bool return_f64=neo_is(jit,op,"return-f64");
    if (return_f64 || neo_is(jit,op,"set-f64")) {
        uint32_t destination;
        size_t operand;
        if (return_f64) {
            destination=jit->text_destination;
            operand=a;
            if (!destination || !a || b) { return "return-f64 expects one expression and a result context"; }
        } else {
            size_t object=neo_bound_receiver(jit,a,parameter,self);
            size_t slot=neo_member(jit,object,"slot",jit->cells[b].atom);
            const char *declared=neo_slot_literal(jit,slot);
            if (!self || !declared || *declared!='~' || declared[1]=='[' || !c || jit->cells[c].next) {
                return "set-f64 requires a binary64 slot and expression";
            }
            destination=neo_text_record(slot);
            operand=c;
        }
        const char *failure=neo_compile_f64(jit,operand,parameter,self,depth+1);
        if (failure) { return failure; }
        if (!NEO_EMIT(jit,0xf2,0x41,0x0f,0x11,0x83) || !neo_u32(jit,destination+8u) ||
            !NEO_EMIT(jit,0x41,0xc7,0x83) || !neo_u32(jit,destination) ||
            !neo_u32(jit,return_f64 ? 5u : 1u) || !NEO_EMIT(jit,0x31,0xc0)) {
            return "code limit exceeded";
        }
        if (return_f64) {
            if (jit->return_count>=NEO_CELLS || !NEO_EMIT(jit,0xe9,0,0,0,0)) {
                return "code limit exceeded";
            }
            jit->returns[jit->return_count++]=jit->length-4;
        }
        return NULL;
    }
    if (neo_is(jit,op,"ref-equal")) {
        if (!self || !b || c) { return "ref-equal expects two object references"; }
        const char *failure=neo_emit_reference(jit,a,parameter,self,depth+1);
        if (failure) { return failure; }
        uint32_t saved=depth*NEO_FRAME;
        if (!NEO_EMIT(jit,0x89,0x84,0x24) || !neo_u32(jit,saved)) { return "code limit exceeded"; }
        failure=neo_emit_reference(jit,b,parameter,self,depth+1);
        if (failure) { return failure; }
        return NEO_EMIT(jit,0x3b,0x84,0x24) && neo_u32(jit,saved) &&
            NEO_EMIT(jit,0x0f,0x94,0xc0,0x0f,0xb6,0xc0) ?
            NULL : "code limit exceeded";
    }
    bool set_ref=neo_is(jit,op,"set-ref");
    bool set_bytes=neo_is(jit,op,"set-bytes");
    bool set_f64_vector=neo_is(jit,op,"set-f64-vector");
    bool set_vector=neo_is(jit,op,"set-vector");
    bool return_bytes=neo_is(jit,op,"return-bytes");
    bool return_ref=neo_is(jit,op,"return-ref");
    bool return_f64_vector=neo_is(jit,op,"return-f64-vector");
    bool return_vector=neo_is(jit,op,"return-vector");
    bool return_text=return_f64_vector || return_ref || return_vector || return_bytes || neo_is(jit,op,"return-text");
    if (return_text || set_bytes || set_vector || set_f64_vector || set_ref || neo_is(jit,op,"set-text")) {
        uint32_t offset=jit->text_destination;
        size_t operand=a;
        char kind=set_f64_vector || return_f64_vector ? 'd' : set_ref || return_ref ? '@' : set_vector || return_vector ? '[' :
            set_bytes || return_bytes ? '#' : '"';
        if (return_text) {
            if ((!self && !return_vector && !return_f64_vector) || !offset || !a || b) {
                return "sequence return requires a result context and one operand";
            }
        } else {
            size_t object=neo_bound_receiver(jit,a,parameter,self);
            size_t slot=object ? neo_member(jit,object,"slot",jit->cells[b].atom) : 0;
            operand=c;
            const char *declared=neo_slot_literal(jit,slot);
            if (!self || !declared || (kind=='d' ? declared[0]!='~' || declared[1]!='[' : *declared!=kind ||
                    (kind=='~' && declared[1]=='[')) ||
                !c || jit->cells[c].next) {
                return "write requires a slot of the matching value kind";
            }
            offset=neo_text_record(slot);
        }
        bool binary=set_bytes || return_bytes;
        uint32_t source;
        const char *source_failure=neo_compile_sequence_source(jit,operand,parameter,self,depth,kind,&source);
        if (source_failure) { return source_failure; }
        if (source) {
            if (!neo_emit_text_copy(jit,offset,source)) { return "code limit exceeded"; }
        } else {
            size_t text=neo_argument_literal(jit,operand,parameter,self);
            const char *literal=jit->cells[text].atom;
            size_t length;
            int32_t values[NEO_VECTOR_LIMIT];
            double doubles[NEO_VECTOR_LIMIT];
            size_t reference=kind=='@' && *literal=='@' ? neo_object(jit,literal+1) : 0;
            bool valid;
            if (kind=='@') {
                valid=reference!=0 && neo_same_package(jit,jit->scope_object,reference);
                length=reference;
                literal+=strlen(literal);
            } else {
                valid=kind=='d' ? neo_f64_vector_literal(&literal,&length,doubles) :
                    kind=='[' ? neo_vector_literal(&literal,&length,values) :
                    neo_sequence_literal(&literal,&length,binary);
            }
            if (!valid || *literal) {
                if (kind=='@') { return "expected a graph-local object reference"; }
                if (kind=='d') { return "expected a binary64 vector"; }
                if (kind=='[') { return "expected an integer vector"; }
                return binary ? "expected a byte value" : "expected a text value";
            }
            const char *decoded=jit->cells[text].atom+1;
            if (!NEO_EMIT(jit,0x41,0xc7,0x83) || !neo_u32(jit,offset+4u) ||
                !neo_u32(jit,(uint32_t)length)) { return "code limit exceeded"; }
            for (uint32_t i=0; kind!='@' && i<length; ++i) {
                if (kind=='d') {
                    uint64_t bits;
                    memcpy(&bits,&doubles[i],sizeof(bits));
                    if (!NEO_EMIT(jit,0x41,0xc7,0x83) || !neo_u32(jit,offset+8u+i*8u) ||
                        !neo_u32(jit,(uint32_t)bits) || !NEO_EMIT(jit,0x41,0xc7,0x83) ||
                        !neo_u32(jit,offset+12u+i*8u) || !neo_u32(jit,(uint32_t)(bits>>32))) {
                        return "code limit exceeded";
                    }
                } else if (kind=='[') {
                    if (!NEO_EMIT(jit,0x41,0xc7,0x83) || !neo_u32(jit,offset+8u+i*4u) ||
                        !neo_u32(jit,(uint32_t)values[i])) { return "code limit exceeded"; }
                } else if (!NEO_EMIT(jit,0x41,0xc6,0x83) || !neo_u32(jit,offset+8u+i) ||
                           !NEO_EMIT(jit,neo_sequence_byte(&decoded,binary))) {
                    return "code limit exceeded";
                }
            }
            for (uint32_t i=0; i<=strlen(jit->cells[text].atom); ++i) {
                if (!NEO_EMIT(jit,0x41,0xc6,0x83) ||
                    !neo_u32(jit,offset+NEO_TEXT_TOKEN_OFFSET+i) ||
                    !NEO_EMIT(jit,(unsigned char)jit->cells[text].atom[i])) {
                    return "code limit exceeded";
                }
            }
        }
        if (!NEO_EMIT(jit,0x41,0xc7,0x83) || !neo_u32(jit,offset) || !neo_u32(jit,return_f64_vector ? 7u : return_ref ? 4u : return_vector ? 3u : return_bytes ? 2u : 1u) ||
            !NEO_EMIT(jit,0x41,0x8b,0x83) || !neo_u32(jit,offset+4u)) {
            return "code limit exceeded";
        }
        if (set_ref && (!NEO_EMIT(jit,0xb8) || !neo_u32(jit,1))) {
            return "code limit exceeded";
        }
        if (return_text) {
            if (jit->return_count>=NEO_CELLS || !NEO_EMIT(jit,0xe9,0,0,0,0)) {
                return "code limit exceeded";
            }
            jit->returns[jit->return_count++]=jit->length-4;
        }
        return NULL;
    }
    bool binary_at=neo_is(jit,op,"bytes-at");
    bool binary=binary_at || neo_is(jit,op,"bytes-length");
    bool vector_at=neo_is(jit,op,"vector-at");
    bool vector=vector_at || neo_is(jit,op,"vector-length");
    bool indexed=binary_at || vector_at || neo_is(jit,op,"text-byte");
    if (binary || vector || indexed || neo_is(jit,op,"text-length")) {
        size_t text=neo_bound_literal(jit,a,parameter);
        const char *literal=jit->cells[text].atom;
        if (!a || (indexed ? !b || c : b!=0)) {
            return "invalid sequence operation operands";
        }
                uint32_t offset;
        const char *source_failure=neo_compile_sequence_source(jit,a,parameter,self,depth,
            vector ? '[' : binary ? '#' : '"',&offset);
        if (source_failure) { return source_failure; }
        if (offset) {
            if (!indexed) {
                return NEO_EMIT(jit,0x41,0x8b,0x83) && neo_u32(jit,offset+4u) ?
                    NULL : "code limit exceeded";
            }
            const char *failure=neo_compile(jit,b,parameter,self,depth+1);
            if (failure) { return failure; }
            return NEO_EMIT(jit,0x41,0x3b,0x83) && neo_u32(jit,offset+4u) &&
                NEO_EMIT(jit,0x73,0x0b) &&
                (vector ? NEO_EMIT(jit,0x90,0x41,0x8b,0x84,0x83) :
                          NEO_EMIT(jit,0x41,0x0f,0xb6,0x84,0x03)) &&
                neo_u32(jit,offset+8u) && NEO_EMIT(jit,0xeb,0x05,0xb8,0xff,0xff,0xff,0xff) ?
                NULL : "code limit exceeded";
        }
        size_t bytes;
        int32_t values[NEO_VECTOR_LIMIT];
        const char *decoded=literal+1;
        bool valid=vector ? neo_vector_literal(&literal,&bytes,values) :
            neo_sequence_literal(&literal,&bytes,binary);
        if (!valid || *literal) {
            if (vector) { return "expected an integer vector"; }
            return binary ? "expected a hexadecimal byte literal" : "expected a text value";
        }
        if (!indexed) {
            return NEO_EMIT(jit,0xb8) && neo_u32(jit,(uint32_t)bytes) ?
                NULL : "code limit exceeded";
        }

        const char *failure=neo_compile(jit,b,parameter,self,depth+1);
        if (failure) { return failure; }
        /* An unsigned bounds check also rejects negative indices. The jump
           skips the literal bytes, which share the execute/read code page. */
        if (!NEO_EMIT(jit,0x83,0xf8,(unsigned char)bytes,0x73,0x0d,
                     0x48,0x8d,0x0d,0,0,0,0) ||
            !(vector ? NEO_EMIT(jit,0x8b,0x04,0x81,0x90) : NEO_EMIT(jit,0x0f,0xb6,0x04,0x01)) ||
            !NEO_EMIT(jit,
                     0xeb,0x05,0xb8,0xff,0xff,0xff,0xff,0xe9,0,0,0,0)) {
            return "code limit exceeded";
        }
        size_t skip=jit->length-4;
        neo_patch_branch(jit,skip-16,jit->length);
        for (size_t i=0; i<bytes; ++i) {
            if (vector) {
                if (!neo_u32(jit,(uint32_t)values[i])) { return "code limit exceeded"; }
            } else {
                unsigned char byte=neo_sequence_byte(&decoded,binary);
                if (!neo_emit(jit,&byte,1)) { return "code limit exceeded"; }
            }
        }
        neo_patch_branch(jit,skip,jit->length);
        return NULL;
    }
    if (neo_is(jit,op,"return")) {
        if (!self || !a || b) { return "return expects one value in a handler"; }
        const char *failure=neo_compile(jit,a,parameter,self,depth+1);
        if (failure) { return failure; }
        if (jit->return_count>=NEO_CELLS || !NEO_EMIT(jit,0xe9,0,0,0,0)) {
            return "code limit exceeded";
        }
        jit->returns[jit->return_count++]=jit->length-4;
        return NULL;
    }
    if (neo_is(jit,op,"let")) {
        if (!neo_name(&jit->cells[a]) || !b || !c || jit->cells[c].next) {
            return "let expects a name, value and body";
        }
        size_t literal=neo_argument_literal(jit,b,parameter,self);
        const char *atom=jit->cells[literal].atom;
        if (*atom=='@' && (!self || !neo_object(jit,atom+1) ||
            !neo_same_package(jit,jit->scope_object,neo_object(jit,atom+1)))) {
            return "unresolved or unavailable object reference";
        }
        neo_binding binding={jit->cells[a].atom,depth*NEO_FRAME,parameter,literal,0,0};
        const char *failure=neo_bind_value(jit,b,parameter,self,depth,0,&binding);
        if (failure) { return failure; }
        return neo_compile(jit,c,&binding,self,depth+1);
    }
    if (neo_is(jit,op,"if")) {
        if (!a || !b || jit->cells[c].next) { return "if expects two or three forms"; }
        const char *failure=neo_compile(jit,a,parameter,self,depth+1);
        if (failure) { return failure; }
        if (!NEO_EMIT(jit,0x85,0xc0,0x0f,0x84,0,0,0,0)) { return "code limit exceeded"; }
        size_t otherwise=jit->length-4;
        failure=neo_compile(jit,b,parameter,self,depth+1);
        if (failure) { return failure; }
        if (!NEO_EMIT(jit,0xe9,0,0,0,0)) { return "code limit exceeded"; }
        size_t end=jit->length-4;
        neo_patch_branch(jit,otherwise,jit->length);
        failure=c ? neo_compile(jit,c,parameter,self,depth+1) :
            (NEO_EMIT(jit,0x31,0xc0) ? NULL : "code limit exceeded");
        if (failure) { return failure; }
        neo_patch_branch(jit,end,jit->length);
        return NULL;
    }
    if (neo_is(jit,op,"while")) {
        if (!self || !a || !b || c) { return "while expects a condition and body in a handler"; }
        size_t start=jit->length;
        const char *failure=neo_compile(jit,a,parameter,self,depth+1);
        if (failure) { return failure; }
        if (!NEO_EMIT(jit,0x85,0xc0,0x0f,0x84,0,0,0,0)) { return "code limit exceeded"; }
        size_t done=jit->length-4;
        failure=neo_compile(jit,b,parameter,self,depth+1);
        if (failure) { return failure; }
        if (!NEO_EMIT(jit,0xe9,0,0,0,0)) { return "code limit exceeded"; }
        neo_patch_branch(jit,jit->length-4,start);
        neo_patch_branch(jit,done,jit->length);
        return NEO_EMIT(jit,0x31,0xc0) ? NULL : "code limit exceeded";
    }
    if (self && neo_is(jit,op,"do")) {
        if (!a) { return "empty handler body"; }
        for (size_t step=a; step; step=jit->cells[step].next) {
            const char *failure=neo_compile(jit,step,parameter,self,depth+1);
            if (failure) { return failure; }
        }
        return NULL;
    }
    bool read_document=neo_is(jit,op,"read-document");
    bool write_document=neo_is(jit,op,"write-document");
    if (read_document || write_document) {
        if (!self || (read_document ? a!=0 : !a || b)) {
            return "invalid document operation";
        }
        unsigned right=read_document ? NEO_DOCUMENT_READ : NEO_DOCUMENT_WRITE;
        if (!jit->checking && (!(jit->compile_rights & right) ||
            strcmp(neo_package(jit,self),jit->document_package[0] ? jit->document_package : "root"))) { return "document access denied"; }
        if (write_document) {
            const char *failure=neo_compile(jit,a,parameter,self,depth+1);
            if (failure) { return failure; }
            if (!NEO_EMIT(jit,0x41,0xc7,0x83) || !neo_u32(jit,NEO_WRITE_MARK*4u) ||
                !neo_u32(jit,1)) { return "code limit exceeded"; }
        }
        /* Slot zero is reserved for the broker's per-send staging value. */
        return NEO_EMIT(jit,0x41,read_document ? 0x8b : 0x89,0x03) ?
            NULL : "code limit exceeded";
    }
    bool get=neo_is(jit,op,"get"), set=neo_is(jit,op,"set");
    if (get || set) {
        size_t object=neo_bound_receiver(jit,a,parameter,self);
        if (!self || !object || !jit->cells[b].atom[0] ||
            (get ? c!=0 : !c || jit->cells[c].next)) { return "invalid slot operation"; }
        size_t slot=neo_member(jit,object,"slot",jit->cells[b].atom);
        if (!slot) { return "unresolved object or slot"; }
        if (neo_slot_literal(jit,slot)) { return "sequence slot requires a matching operation"; }
        if (set) {
            const char *failure=neo_compile(jit,c,parameter,self,depth+1);
            if (failure) { return failure; }
        }
        if (set && !neo_mark_trace(jit,NEO_TRACE_WRITE,self,object,object,jit->cells[b].atom)) {
            return "trace limit exceeded";
        }
        /* RCX carries the session's slot array. Keep it in R11 for the whole native call. */
        return NEO_EMIT(jit,0x41,get ? 0x8b : 0x89,0x83) &&
            neo_u32(jit,(uint32_t)slot*4u) ? NULL : "code limit exceeded";
    }
    bool send_bytes=neo_is(jit,op,"send-bytes");
    bool import=neo_is(jit,op,"send-import");
    bool send_ref=neo_is(jit,op,"send-ref");
    bool send_f64=neo_is(jit,op,"send-f64"), call_f64=neo_is(jit,op,"call-f64");
    bool send_f64_vector=neo_is(jit,op,"send-f64-vector");
    bool call_f64_vector=neo_is(jit,op,"call-f64-vector");
    bool send_vector=neo_is(jit,op,"send-vector");
    bool call_vector=neo_is(jit,op,"call-vector");
    bool send_text=send_f64_vector || send_f64 || send_ref || send_vector || send_bytes || neo_is(jit,op,"send-text");
    unsigned expected_result=send_f64_vector || call_f64_vector ? 7u : send_f64 || call_f64 ? 5u : send_ref ? 4u :
        send_vector || call_vector ? 3u : send_bytes ? 2u : send_text ? 1u : 0u;
    bool send=import || send_text || neo_is(jit,op,"send"), call=call_f64_vector || call_f64 || call_vector || neo_is(jit,op,"call");
    if (send || call) {
        uint32_t destination=expected_result ? jit->requested_text : 0;
        jit->requested_text=0;
        if (expected_result && !destination) { return "sequence send requires a matching operand context"; }
        size_t object=neo_bound_receiver(jit,a,parameter,self);
        if (import) {
            size_t member=neo_member(jit,self,"import",jit->cells[a].atom);
            neo_import_grant binding;
            if (!self || !member || !neo_import_binding(jit,self,member,&binding)) {
                return "unresolved package import";
            }
            if (!jit->checking && !neo_import_authorized(jit,&binding)) {
                return "package import access denied";
            }
            object=neo_object(jit,binding.target);
            c=b;
            size_t alias=jit->cells[jit->cells[member].child].next;
            b=jit->cells[jit->cells[alias].next].next;
            if (!neo_member(jit,object,"export",jit->cells[b].atom)) {
                return "package selector is not exported";
            }
        }
        if (!object && send && self && neo_name(&jit->cells[b])) {
            uint32_t reference=neo_text_location(jit,a,parameter,self,'@');
            if (reference) {
                size_t *ends=neo_platform_malloc(NEO_CELLS*sizeof(*ends));
                if (!ends) { return "out of memory"; }
                size_t branches=0;
                neo_cell saved=jit->cells[a];
                const char *failure=NULL;
                for (size_t target=jit->objects; target; target=jit->cells[target].next) {
                    if (!neo_same_package(jit,jit->scope_object,target) ||
                        !neo_behavior(jit,target,"handler",jit->cells[b].atom,NULL)) { continue; }
                    if (!NEO_EMIT(jit,0x41,0x81,0xbb) || !neo_u32(jit,reference+4u) ||
                        !neo_u32(jit,(uint32_t)target) || !NEO_EMIT(jit,0x0f,0x85,0,0,0,0)) {
                        failure="code limit exceeded";
                        break;
                    }
                    size_t mismatch=jit->length-4;
                    char reference_name[64];
                    int written=snprintf(reference_name,sizeof(reference_name),
                                         "@%s",neo_object_name(jit,target));
                    if (written<0 || (size_t)written>=sizeof(reference_name)) {
                        failure="receiver name exceeds reference limit";
                    } else {
                        jit->cells[a].child=0;
                        strcpy(jit->cells[a].atom,reference_name);
                        jit->requested_text=destination;
                        failure=neo_compile(jit,form,parameter,self,depth+1);
                    }
                    jit->cells[a]=saved;
                    if (failure) { break; }
                    if (!NEO_EMIT(jit,0xe9,0,0,0,0)) { failure="code limit exceeded"; break; }
                    ends[branches++]=jit->length-4;
                    neo_patch_branch(jit,mismatch,jit->length);
                }
                jit->cells[a]=saved;
                if (!failure &&
                    (!NEO_EMIT(jit,0x48,0xb8,8,0,0,0,0,0,0,0x80,0x48,0x81,0xc4) ||
                     !neo_u32(jit,NEO_STACK) || !NEO_EMIT(jit,0xc3))) {
                    failure="code limit exceeded";
                }
                for (size_t i=0; i<branches; ++i) { neo_patch_branch(jit,ends[i],jit->length); }
                neo_platform_free(ends);
                return failure;
            }
        }
        if ((!self && send) || !object || !jit->cells[b].atom[0]) {
            return "invalid call or message";
        }
        size_t owner;
        size_t member=neo_behavior(jit,object,send ? "handler" : "knight",jit->cells[b].atom,&owner);
        if (!member) { return "unresolved receiver or method"; }
        size_t tag=jit->cells[member].child;
        size_t label=jit->cells[tag].next, name=jit->cells[label].next, count;
        size_t body=neo_signature(jit,name,&count);
        neo_binding bindings[NEO_MAX_ARGUMENTS];
        for (size_t i=0; i<count; ++i) {
            if (!c) { return "wrong argument count"; }
            size_t text=neo_argument_literal(jit,c,parameter,self);
            const char *atom=jit->cells[text].atom;
            if (*atom=='@' && (!send || !neo_same_package(jit,jit->scope_object,neo_object(jit,atom+1)))) {
                return "unresolved or unavailable object reference";
            }
            bindings[i]=(neo_binding){jit->cells[name].atom,depth*NEO_FRAME+(uint32_t)i*4u,
                                      i ? &bindings[i-1] : NULL,text,0,0};
            if (import) {
                const char *failure=neo_compile(jit,c,parameter,self,depth+1);
                if (failure) { return failure; }
                if (!NEO_EMIT(jit,0x89,0x84,0x24) || !neo_u32(jit,bindings[i].offset)) {
                    return "code limit exceeded";
                }
                name=jit->cells[name].next;
                c=jit->cells[c].next;
                continue;
            }
            const char *failure=neo_bind_value(jit,c,parameter,self,depth,i,&bindings[i]);
            if (failure) { return failure; }
            name=jit->cells[name].next;
            c=jit->cells[c].next;
        }
        if (c) { return "wrong argument count"; }
        if (!neo_mark_trace(jit,send ? NEO_TRACE_SEND : NEO_TRACE_KNIGHT,self,object,owner,
                            jit->cells[b].atom)) { return "trace limit exceeded"; }
        size_t first_return=jit->return_count;
        uint32_t previous_destination=jit->text_destination;
        size_t previous_scope=jit->scope_object;
        jit->scope_object=object;
        jit->text_destination=destination;
        if (destination &&
            (!NEO_EMIT(jit,0x41,0xc7,0x83) || !neo_u32(jit,destination) || !neo_u32(jit,0))) {
            return "code limit exceeded";
        }
        const char *failure=neo_compile(jit,body,count ? &bindings[count-1] : NULL,
                                        send ? object : 0,depth+1);
        jit->text_destination=previous_destination;
        jit->scope_object=previous_scope;
        neo_finish_returns(jit,first_return);
        if (failure || !destination) { return failure; }
        /* Missing text aborts the entire send through the native failure result. */
        if (!NEO_EMIT(jit,0x41,0x83,0xbb) || !neo_u32(jit,destination) ||
            !NEO_EMIT(jit,(unsigned char)expected_result,0x74,0x12,0x48,0xb8,(unsigned char)expected_result,0,0,0,0,0,0,0x80,0x48,0x81,0xc4) ||
            !neo_u32(jit,NEO_STACK) || !NEO_EMIT(jit,0xc3)) { return "code limit exceeded"; }
        return NULL;
    }
    bool add=neo_is(jit,op,"+"), subtract=neo_is(jit,op,"-");
    bool equal=neo_is(jit,op,"="), less=neo_is(jit,op,"<"), greater=neo_is(jit,op,">");
    bool comparison=equal || less || greater;
    if (comparison && (!a || !b || c)) { return "comparison expects two forms"; }
    if (!comparison && (self || !a || !b || c ||
                        (!add && !subtract && !neo_is(jit,op,"*")))) {
        return "invalid Knight expression";
    }
    const char *failure=neo_compile(jit,a,parameter,self,depth+1);
    if (failure) { return failure; }
    uint32_t slot=depth*NEO_FRAME;
    if (!NEO_EMIT(jit,0x89,0x84,0x24) || !neo_u32(jit,slot)) { return "code limit exceeded"; }
    failure=neo_compile(jit,b,parameter,self,depth+1);
    if (failure) { return failure; }
    if (!NEO_EMIT(jit,0x89,0xc1,0x8b,0x84,0x24) || !neo_u32(jit,slot)) {
        return "code limit exceeded";
    }
    if (comparison) {
        /* Signed CMP, SETcc, then zero-extend the Boolean result into EAX. */
        unsigned char predicate=equal ? 0x94 : less ? 0x9c : 0x9f;
        return NEO_EMIT(jit,0x39,0xc8,0x0f,predicate,0xc0,0x0f,0xb6,0xc0) ?
            NULL : "code limit exceeded";
    }
    bool emitted=add ? NEO_EMIT(jit,0x01,0xc8) :
        subtract ? NEO_EMIT(jit,0x29,0xc8) : NEO_EMIT(jit,0x0f,0xaf,0xc1);
    if (!emitted || jit->jumps>=NEO_CELLS || !NEO_EMIT(jit,0x0f,0x80,0,0,0,0)) {
        return "code limit exceeded";
    }
    jit->overflow[jit->jumps++]=jit->length-4;
    return NULL;
}

static const char *neo_prepare(neo_jit *jit, size_t body, size_t receiver, unsigned rights, size_t parameters, size_t count) {
    jit->scope_object=receiver ? receiver : jit->receiver;
    jit->length=0;
    jit->jumps=0;
    jit->return_count=0;
    jit->requested_text=0;
    jit->text_destination=neo_text_record(NEO_TEXT_RESULT);
    jit->trace_count=0;
    jit->scheduling=false;
    jit->compile_rights=rights;
    if (!NEO_EMIT(jit,0x49,0x89,0xcb,0x48,0x81,0xec) ||
        !neo_u32(jit,NEO_STACK)) { return "code limit exceeded"; }
    neo_binding bindings[NEO_MAX_ARGUMENTS];
    for (size_t i=0; i<count; ++i) {
        bindings[i]=(neo_binding){jit->cells[parameters].atom,(uint32_t)i*4u,
                                  i ? &bindings[i-1] : NULL,0,0,0};
        if (i==0 && jit->text_argument) {
            bindings[i].literal=jit->text_argument;
        } else if (!NEO_EMIT(jit,0x41,0x8b,0x83) || !neo_u32(jit,(NEO_ARGUMENT+(uint32_t)i)*4u) ||
            !NEO_EMIT(jit,0x89,0x84,0x24) || !neo_u32(jit,bindings[i].offset)) {
            return "code limit exceeded";
        }
        parameters=jit->cells[parameters].next;
    }
    const char *failure=neo_compile(jit,body,count ? &bindings[count-1] : NULL,
                                    receiver,count ? 1u : 0u);
    if (failure) { return failure; }
    neo_finish_returns(jit,0);
    if (!NEO_EMIT(jit,0x48,0x98,0x48,0x81,0xc4) || !neo_u32(jit,NEO_STACK) ||
        !NEO_EMIT(jit,0xc3)) { return "code limit exceeded"; }
    size_t overflow=jit->length;
    if (!NEO_EMIT(jit,0x48,0xb8,0,0,0,0,0,0,0,0x80,0x48,0x81,0xc4) ||
        !neo_u32(jit,NEO_STACK) || !NEO_EMIT(jit,0xc3)) { return "code limit exceeded"; }
    for (size_t i=0; i<jit->jumps; ++i) {
        neo_patch_branch(jit,jit->overflow[i],overflow);
    }
    return NULL;
}

typedef struct {
    neo_trace_sink *sink;
    void *context;
    neo_trace_event root;
} neo_trace_request;
static bool neo_format_f64_vector(const int32_t *record, char output[64]) {
    if (record[1]<0 || record[1]>NEO_VECTOR_LIMIT) { return false; }
    size_t length=2;
    memcpy(output,"~[",3);
    for (int32_t i=0; i<record[1]; ++i) {
        double value;
        memcpy(&value,record+2+(size_t)i*2,sizeof(value));
        if (!isfinite(value)) { return false; }
        int written=snprintf(output+length,64-length,"%s%.17g",i ? ", " : "",value);
        if (written<0 || (size_t)written>=64-length) { return false; }
        length+=(size_t)written;
    }
    if (length+1>=64) { return false; }
    output[length++]=']';
    output[length]=0;
    return true;
}

static bool neo_valid_text_record(const neo_jit *jit, const int32_t *record, char kind, int32_t marker) {
    const char *literal=(const char *)record+NEO_TEXT_TOKEN_OFFSET;
    if (kind=='d') {
        char formatted[64];
        return record[0]==marker && neo_format_f64_vector(record,formatted);
    }
    if (kind=='~' && literal[1]!='[') {
        double value;
        memcpy(&value,record+2,sizeof(value));
        return record[0]==marker && isfinite(value);
    }
    if (kind=='@') {
        return record[0]==marker && *literal=='@' && memchr(literal,0,64) &&
            record[1]>0 && (size_t)record[1]==neo_object(jit,literal+1);
    }
    size_t bytes;
    return record[0]==marker && memchr(literal,0,NEO_TEXT_RESULT_CAPACITY) &&
        (kind=='d' || kind=='~' ? neo_f64_vector_literal(&literal,&bytes,NULL) :
         kind=='[' ? neo_vector_literal(&literal,&bytes,NULL) :
         neo_sequence_literal(&literal,&bytes,kind=='#')) && !*literal &&
        (int32_t)bytes==record[1];
}
static void neo_commit_values(neo_jit *jit, const int32_t *pending) {
    for (size_t slot=1; slot<=jit->count; ++slot) {
        if (!jit->text_slots[slot][0]) { continue; }
        const int32_t *record=pending+NEO_TEXT_BASE+slot*NEO_TEXT_WORDS;
        if (!record[0]) { continue; }
        if (jit->text_slots[slot][0]=='~' && jit->text_slots[slot][1]=='[') {
            neo_format_f64_vector(record,jit->text_slots[slot]);
        } else if (jit->text_slots[slot][0]=='~') {
            double number;
            memcpy(&number,record+2,sizeof(number));
            snprintf(jit->text_slots[slot],sizeof(jit->text_slots[slot]),"~%.17g",number);
        } else {
            strcpy(jit->text_slots[slot],(const char *)record+NEO_TEXT_TOKEN_OFFSET);
        }
    }
    memcpy(jit->slots,pending,sizeof(jit->slots));
    jit->slots[0]=0;
}
static const char *neo_execute(neo_jit *jit, size_t body, size_t receiver,
                               const neo_code_platform *platform, int32_t *result,
                               size_t parameters, const int32_t *arguments, size_t count,
                               const neo_trace_request *trace) {
    jit->tracing=trace!=NULL;
    const char *failure=neo_prepare(jit,body,receiver,jit->document_rights,parameters,count);
    jit->tracing=false;
    if (failure) { return failure; }
    int32_t pending[NEO_NATIVE_SLOTS]={0};
    memcpy(pending,jit->slots,sizeof(jit->slots));
    for (size_t slot=1; slot<=jit->count; ++slot) {
        if (!jit->text_slots[slot][0]) { continue; }
        int32_t *record=pending+NEO_TEXT_BASE+slot*NEO_TEXT_WORDS;
        const char *literal=jit->text_slots[slot];
        const char *decoded=literal+1;
        size_t length;
        bool binary=*literal=='#';
        if (literal[0]=='~' && literal[1]=='[') {
            double values[NEO_VECTOR_LIMIT];
            if (!neo_f64_vector_literal(&literal,&length,values) || *literal) {
                return "invalid live binary64 vector";
            }
            memcpy(record+2,values,length*sizeof(*values));
        } else if (*literal=='~') {
            double number;
            if (!neo_f64_literal(&literal,&number) || *literal) { return "invalid live binary64"; }
            memcpy(record+2,&number,sizeof(number));
            length=0;
        } else if (*literal=='@') {
            length=neo_object(jit,literal+1);
            if (!length) { return "invalid live reference"; }
        } else if (*literal=='[') {
            if (!neo_vector_literal(&literal,&length,record+2)) { return "invalid live vector"; }
        } else {
            if (!neo_sequence_literal(&literal,&length,binary)) { return "invalid live sequence"; }
            unsigned char *bytes=(unsigned char *)(record+2);
            for (size_t i=0; i<length; ++i) { bytes[i]=neo_sequence_byte(&decoded,binary); }
        }
        record[1]=(int32_t)length;
        strcpy((char *)record+NEO_TEXT_TOKEN_OFFSET,jit->text_slots[slot]);
    }
    pending[0]=jit->document && (jit->document_rights & NEO_DOCUMENT_READ) ? *jit->document : 0;
    pending[NEO_WRITE_MARK]=0;
    if (count) { memcpy(pending+NEO_ARGUMENT,arguments,count*sizeof(*arguments)); }
    int64_t value;
    failure=platform->execute(jit->code,jit->length,pending,&value);
    if (failure) { return failure; }
    if (value==INT64_MIN+1) { return "nested handler did not return text"; }
    if (value==INT64_MIN+2) { return "nested handler did not return bytes"; }
    if (value==INT64_MIN+3) { return "nested call did not return a vector"; }
    if (value==INT64_MIN+4) { return "nested call did not return a reference"; }
    if (value==INT64_MIN+5) { return "nested call did not return binary64"; }
    if (value==INT64_MIN+6) { return "binary64 vector index out of bounds"; }
    if (value==INT64_MIN+7) { return "nested call did not return a binary64 vector"; }
    if (value==INT64_MIN+8) { return "runtime receiver does not implement the message"; }
    if (value==INT64_MIN+9) { return "one lifecycle operation is supported per send"; }
    if (value==INT64_MIN+10) { return "scheduled message batch limit exceeded"; }
    bool overflow=value==INT64_MIN;
    if (!overflow && (value<INT32_MIN || value>INT32_MAX ||
        (pending[NEO_WRITE_MARK]!=0 && pending[NEO_WRITE_MARK]!=1))) {
        return "invalid native result";
    }
    const int32_t *text_result=pending+NEO_TEXT_BASE+NEO_TEXT_RESULT*NEO_TEXT_WORDS;
    double floating_result=0;
    if (!overflow) {
        int32_t marker=text_result[0];
        char kind=marker==7 ? 'd' : marker==5 ? 'f' : marker==4 ? '@' : marker==3 ? '[' :
            marker==2 ? '#' : marker==1 ? '"' : 0;
        if (kind=='f') {
            memcpy(&floating_result,text_result+2,sizeof(floating_result));
        } else if (marker && (!kind || !neo_valid_text_record(jit,text_result,kind,marker))) {
            return "invalid native sequence result";
        }
        if (kind && !jit->text_result) {
            if (kind=='d') { return "handler returned a binary64 vector; use receive-f64-vector"; }
            if (kind=='f') { return "handler returned binary64; use receive-f64"; }
            if (kind=='[') { return "handler returned a vector; use receive-vector"; }
            if (kind=='@') { return "handler returned a reference; use receive-ref"; }
            return kind=='#' ? "handler returned bytes; use receive-bytes" :
                "handler returned text; use receive-text";
        }
        if (jit->text_result && kind!=jit->receive_kind) {
            if (jit->receive_kind=='d') { return "handler did not return a binary64 vector"; }
            if (jit->receive_kind=='f') { return "handler did not return binary64"; }
            if (jit->receive_kind=='[') { return "handler did not return a vector"; }
            if (jit->receive_kind=='@') { return "handler did not return a reference"; }
            return jit->receive_kind=='#' ? "handler did not return bytes" : "handler did not return text";
        }
        for (size_t slot=1; slot<=jit->count; ++slot) {
            if (!jit->text_slots[slot][0]) { continue; }
            const int32_t *record=pending+NEO_TEXT_BASE+slot*NEO_TEXT_WORDS;
            if (!record[0]) { continue; }
            if (jit->text_slots[slot][0]=='@' && record[0]) {
                const char *reference=(const char *)record+NEO_TEXT_TOKEN_OFFSET;
                size_t owner=0;
                for (size_t object=jit->objects; object; object=jit->cells[object].next) {
                    size_t label=jit->cells[jit->cells[object].child].next;
                    for (size_t item=jit->cells[label].next; item; item=jit->cells[item].next) {
                        if (item==slot) { owner=object; break; }
                    }
                    if (owner) { break; }
                }
                if (!memchr(reference,0,64) || !neo_same_package(jit,owner,neo_object(jit,reference+1))) {
                    return "reference write crosses package boundary";
                }
            }
            if (!neo_valid_text_record(jit,record,
                jit->text_slots[slot][0]=='~' && jit->text_slots[slot][1]=='[' ?
                'd' : jit->text_slots[slot][0],1)) {
                return "invalid native sequence write";
            }
        }
    }
    neo_jit *replacement=NULL;
    int32_t request=pending[NEO_LIFECYCLE];
    if (!overflow && request) {
        if (jit->text_argument) { return "lifecycle operations require an integer-argument send"; }
        if (request<1 || (size_t)request>jit->count) { return "invalid lifecycle request"; }
        size_t op=jit->cells[request].child, target=jit->cells[op].next;
        size_t name=jit->cells[target].next;
        bool clone=neo_is(jit,op,"clone");
        if (!clone && !neo_is(jit,op,"delete")) { return "invalid lifecycle request"; }
        int32_t target_id=pending[NEO_LIFECYCLE+1];
        if (target_id<1 || (size_t)target_id>jit->count) { return "invalid lifecycle target"; }
        const char *target_name=neo_object_name(jit,(size_t)target_id);
        if (neo_object(jit,target_name)!=(size_t)target_id) { return "invalid lifecycle target"; }
        replacement=neo_platform_malloc(sizeof(*replacement));
        if (!replacement) { return "out of memory"; }
        *replacement=*jit;
        neo_commit_values(replacement,pending);
        failure=clone ? neo_session_clone(&replacement,jit->revision,target_name,jit->cells[name].atom) :
            neo_session_delete(&replacement,jit->revision,target_name);
        if (failure) { neo_platform_free(replacement); return failure; }
    }
    int32_t deferred_count=pending[NEO_DEFERRED_BASE];
    if (deferred_count<0 || deferred_count>NEO_DEFERRED_LIMIT) { neo_platform_free(replacement); return "invalid scheduled batch"; }
    bool durable_write=!overflow && jit->document && jit->document_sink && pending[NEO_WRITE_MARK];
    if (durable_write && (deferred_count || replacement)) {
        neo_platform_free(replacement);
        return "durable writes and other external effects require separate sends";
    }
    if (durable_write) {
        failure=jit->document_sink(jit->document_context,pending[0]);
        if (failure) { return failure; }
    }
    if (trace) {
        int32_t count_events=pending[NEO_TRACE_COUNT];
        if (count_events<0 || count_events>NEO_TRACE_LIMIT) { neo_platform_free(replacement); return "invalid native trace"; }
        for (int32_t i=0; i<count_events; ++i) {
            int32_t site=pending[NEO_TRACE_BASE+2u*(size_t)i];
            if (site<1 || (size_t)site>jit->trace_count) { neo_platform_free(replacement); return "invalid native trace"; }
        }
    }
    if (!overflow && deferred_count) {
        if (replacement) { neo_platform_free(replacement); return "scheduling and lifecycle changes require separate sends"; }
        neo_deferred requests[NEO_DEFERRED_LIMIT];
        neo_buffer_command buffer[NEO_DEFERRED_LIMIT];
        size_t messages=0, commands=0;
        for (int32_t i=0; i<deferred_count; ++i) {
            const int32_t *record=pending+NEO_DEFERRED_BASE+1u+(size_t)i*8u;
            if (record[0]<1 || (size_t)record[0]>jit->count || record[1]<1 ||
                (size_t)record[1]>jit->count || record[2]<0 || record[3]<0 || record[3]>NEO_MAX_ARGUMENTS) {
                return "invalid scheduled message";
            }
            size_t operation=jit->cells[record[0]].child;
            bool fill=neo_is(jit,operation,"buffer-fill"), present=neo_is(jit,operation,"buffer-present");
            if (fill || present) {
                if (!jit->buffer || strcmp(neo_package(jit,(size_t)record[1]),jit->buffer_package)) { return "buffer access denied"; }
                if (record[3]!=(fill ? 3 : 0)) { return "invalid buffer command"; }
                buffer[commands++]=(neo_buffer_command){present,record[4],record[5],record[6]};
                continue;
            }
            bool after=neo_is(jit,operation,"after");
            if (!after && !neo_is(jit,operation,"post")) { return "invalid scheduled operation"; }
            size_t target=jit->cells[operation].next;
            if (after) { target=jit->cells[target].next; }
            size_t selector=jit->cells[target].next;
            requests[messages]=(neo_deferred){neo_object_name(jit,(size_t)record[1]),jit->cells[selector].atom,
                                      (uint32_t)record[2],{0},(size_t)record[3]};
            if (neo_object(jit,requests[messages].object)!=(size_t)record[1]) { return "invalid scheduled target"; }
            memcpy(requests[messages].arguments,record+4,requests[messages].count*sizeof(int32_t));
            ++messages;
        }
        if (messages && commands) { return "buffer publication and message scheduling require separate sends"; }
        if (messages && !jit->schedule) { return "scheduling service unavailable"; }
        failure=commands ? jit->buffer(jit->buffer_context,buffer,commands) :
            jit->schedule(jit->schedule_context,jit->revision,requests,messages);
        if (failure) { return failure; }
    }
    if (trace) {
        trace->sink(&trace->root,trace->context);
        int32_t count_events=pending[NEO_TRACE_COUNT];
        if (count_events<0 || count_events>NEO_TRACE_LIMIT) { neo_platform_free(replacement); return "invalid native trace"; }
        for (int32_t i=0; i<count_events; ++i) {
            size_t offset=NEO_TRACE_BASE+2u*(size_t)i;
            int32_t site=pending[offset];
            if (site<1 || (size_t)site>jit->trace_count) { neo_platform_free(replacement); return "invalid native trace"; }
        }
        for (int32_t i=0; i<count_events; ++i) {
            size_t offset=NEO_TRACE_BASE+2u*(size_t)i;
            neo_trace_event event=jit->trace[(size_t)pending[offset]-1u];
            event.value=pending[offset+1];
            event.committed=!overflow;
            trace->sink(&event,trace->context);
        }
        if (count_events==NEO_TRACE_LIMIT) {
            neo_trace_event limit={.kind=NEO_TRACE_LIMIT_REACHED};
            trace->sink(&limit,trace->context);
        }
    }
    if (overflow) { return "integer overflow"; }
    neo_commit_values(jit,pending);
    if (jit->document && (jit->document_rights & NEO_DOCUMENT_WRITE) && pending[NEO_WRITE_MARK]) {
        *jit->document=pending[0];
    }
    pending[0]=0;
    memcpy(jit->slots,pending,sizeof(jit->slots));
    if (jit->text_result) {
        if (jit->receive_kind=='f') {
            snprintf(jit->text_result,NEO_TEXT_RESULT_CAPACITY,"%.17g",floating_result);
        } else if (jit->receive_kind=='d') {
            neo_format_f64_vector(text_result,jit->text_result);
        } else {
            strcpy(jit->text_result,(const char *)text_result+NEO_TEXT_TOKEN_OFFSET);
        }
    }
    if (replacement) {
        replacement->text_result=jit->text_result;
        replacement->text_argument=jit->text_argument;
        replacement->receive_kind=jit->receive_kind;
        *jit=*replacement;
        neo_platform_free(replacement);
    }
    *result=(int32_t)value;
    return NULL;
}

static const char *neo_load_graph(neo_jit *jit, const char *source, size_t *root_id, size_t *body) {
    jit->cursor=source;
    size_t root=neo_parse(jit,0);
    neo_skip(jit);
    size_t tag=jit->cells[root].child, version=jit->cells[tag].next;
    size_t entry=jit->cells[version].next, kind=jit->cells[entry].child;
    size_t receiver=jit->cells[kind].next, selector=jit->cells[receiver].next;
    jit->objects=jit->cells[entry].next;
    if (!root || *jit->cursor || !neo_is(jit,tag,"warp") || !neo_is(jit,version,"1") ||
        !neo_is(jit,kind,"entry") || !receiver || !selector || jit->cells[selector].next ||
        !neo_validate(jit)) { return "invalid Warp image"; }
    size_t object=neo_object(jit,jit->cells[receiver].atom);
    jit->receiver=object;
    size_t handler=neo_behavior(jit,object,"handler",jit->cells[selector].atom,NULL);
    if (!handler) { return "unresolved entry"; }
    size_t label=jit->cells[jit->cells[handler].child].next;
    for (size_t id=jit->objects; id; id=jit->cells[id].next) {
        size_t name=jit->cells[jit->cells[id].child].next;
        for (size_t slot=jit->cells[name].next; slot; slot=jit->cells[slot].next) {
            size_t slot_tag=jit->cells[slot].child;
            if (!neo_is(jit,slot_tag,"slot")) { continue; }
            size_t value=jit->cells[jit->cells[slot_tag].next].next;
            const char *literal=neo_slot_literal(jit,slot);
            if (literal) {
                if (*literal=='@' && (!neo_object(jit,literal+1) ||
                    !neo_same_package(jit,id,neo_object(jit,literal+1)))) {
                    return "unresolved reference slot initializer";
                }
                strcpy(jit->text_slots[slot],literal);
                continue;
            }
            const char *failure=neo_integer(jit->cells[value].atom,&jit->slots[slot]);
            if (failure) { return failure; }
        }
    }
    jit->cursor=NULL;
    if (jit->cells[jit->cells[label].next].next) { return "entry must take no arguments"; }
    *root_id=root;
    *body=jit->cells[label].next;
    return NULL;
}

static bool neo_serialize_cell(const neo_jit *jit, size_t id, char *text, size_t capacity,
                               size_t *length, int live_depth) {
    const neo_cell *cell=&jit->cells[id];
    if (!cell->child) {
        size_t bytes=strlen(cell->atom);
        if (bytes>=capacity-*length) { return false; }
        memcpy(text+*length,cell->atom,bytes);
        *length+=bytes;
        return true;
    }
    if (*length+1>=capacity) { return false; }
    text[(*length)++]='(';
    size_t tag=cell->child;
    size_t slot_value=live_depth==0 && neo_is(jit,tag,"slot") ?
        jit->cells[jit->cells[tag].next].next : 0;
    for (size_t child=cell->child; child; child=jit->cells[child].next) {
        if (child!=cell->child) {
            if (*length+1>=capacity) { return false; }
            text[(*length)++]=' ';
        }
        if (child==slot_value) {
            int written=jit->text_slots[id][0] ?
                snprintf(text+*length,capacity-*length,"%s",jit->text_slots[id]) :
                snprintf(text+*length,capacity-*length,"%" PRId32,jit->slots[id]);
            if (written<0 || (size_t)written>=capacity-*length) { return false; }
            *length+=(size_t)written;
        } else if (!neo_serialize_cell(jit,child,text,capacity,length,live_depth-1)) { return false; }
    }
    if (*length+1>=capacity) { return false; }
    text[(*length)++]=')';
    return true;
}

static bool neo_same_graph(const neo_jit *first, const neo_jit *second) {
    if (first->count!=second->count || first->objects!=second->objects) { return false; }
    for (size_t i=1; i<=first->count; ++i) {
        const neo_cell *a=&first->cells[i], *b=&second->cells[i];
        if (a->child!=b->child || a->next!=b->next || strcmp(a->atom,b->atom)) { return false; }
    }
    return true;
}

const char *neo_run(const char *source, const neo_code_platform *platform, int32_t *result) {
    neo_jit *jit=neo_platform_calloc(1,sizeof(*jit));
    if (!jit) { return "out of memory"; }
    size_t root,body;
    const char *failure=neo_load_graph(jit,source,&root,&body);
    if (!failure) { failure=neo_execute(jit,body,jit->receiver,platform,result,0,NULL,0,NULL); }
    neo_platform_free(jit);
    return failure;
}

const char *neo_roundtrip(const char *source, const neo_code_platform *platform, int32_t *result) {
    neo_jit *before=neo_platform_calloc(1,sizeof(*before));
    neo_jit *after=neo_platform_calloc(1,sizeof(*after));
    char *serialized=neo_platform_malloc(NEO_BYTES);
    const char *failure="out of memory";
    if (before && after && serialized) {
        size_t root,body,length=0;
        failure=neo_load_graph(before,source,&root,&body);
        if (!failure && !neo_serialize_cell(before,root,serialized,NEO_BYTES,&length,-1)) {
            failure="serialized Warp image exceeds limit";
        }
        if (!failure) {
            serialized[length]=0;
            failure=neo_load_graph(after,serialized,&root,&body);
        }
        if (!failure && !neo_same_graph(before,after)) { failure="Warp round trip changed the graph"; }
        if (!failure) { failure=neo_execute(after,body,after->receiver,platform,result,0,NULL,0,NULL); }
    }
    neo_platform_free(serialized);
    neo_platform_free(after);
    neo_platform_free(before);
    return failure;
}

const char *neo_session_open(const char *source, neo_session **session) {
    neo_jit *jit=neo_platform_calloc(1,sizeof(*jit));
    if (!jit) { return "out of memory"; }
    const char *failure=neo_load_graph(jit,source,&jit->root,&jit->entry);
        if (!failure) {
        jit->checking=true;
        failure=neo_prepare(jit,jit->entry,jit->receiver,NEO_DOCUMENT_READ|NEO_DOCUMENT_WRITE,0,0);
        jit->checking=false;
    }
    if (failure) { neo_platform_free(jit); return failure; }
    jit->revision=1;
    *session=jit;
    return NULL;
}

void neo_session_close(neo_session *session) {
    neo_platform_free(session);
}
void neo_session_schedule(neo_session *session, neo_deferred_sink *sink, void *context) {
    session->schedule=sink;
    session->schedule_context=context;
}
const char *neo_session_grant_buffer(neo_session *session, const char *package, neo_buffer_sink *sink, void *context) {
    if (!package || !*package || strlen(package)>=sizeof(session->buffer_package)) { return "invalid buffer package"; }
    strcpy(session->buffer_package,package);
    session->buffer=sink; session->buffer_context=context; return NULL;
}
const char *neo_session_object_package(const neo_session *session, const char *name, char package[64]) {
    size_t object=neo_object(session,name);
    if (!object) { return "unknown object"; }
    strcpy(package,neo_package(session,object)); return NULL;
}

const char *neo_session_authorize_import(neo_session *session, const char *object,
                                          const char *alias, bool allow) {
    size_t receiver=neo_object(session,object);
    size_t member=neo_member(session,receiver,"import",alias);
    neo_import_grant binding;
    if (!member || !neo_import_binding(session,receiver,member,&binding)) {
        return "unknown package import";
    }
    size_t empty=NEO_IMPORT_GRANTS;
    for (size_t i=0; i<NEO_IMPORT_GRANTS; ++i) {
        neo_import_grant *grant=&session->imports[i];
        if (grant->active && !strcmp(grant->receiver,object) && !strcmp(grant->alias,alias)) {
            if (allow) { *grant=binding; }
            else { grant->active=false; }
            return NULL;
        }
        if (!grant->active) { empty=i; }
    }
    if (!allow) { return NULL; }
    if (empty==NEO_IMPORT_GRANTS) { return "import grant limit exceeded"; }
    session->imports[empty]=binding;
    return NULL;
}

const char *neo_session_grant_package_document(neo_session *session, const char *package,
                                               int32_t *document, unsigned rights) {
    if (!*package || strlen(package)>=sizeof(session->document_package)) {
        return "invalid package name";
    }
    neo_session_grant_document(session,document,rights);
    strcpy(session->document_package,package);
    return NULL;
}
void neo_session_grant_document(neo_session *session, int32_t *document, unsigned rights) {
    session->document_sink=NULL;
    session->document_context=NULL;
    strcpy(session->document_package,"root");
    session->document=document;
    session->document_rights=document ? rights & (NEO_DOCUMENT_READ|NEO_DOCUMENT_WRITE) : 0;
}
void neo_session_document_sink(neo_session *session, neo_document_sink *sink, void *context) {
    session->document_sink=sink;
    session->document_context=context;
}
uint64_t neo_session_revision(const neo_session *session) {
    return session->revision;
}

static const char *neo_send(neo_session *session, const char *object, const char *selector,
                            const int32_t *arguments, size_t count, const neo_code_platform *platform,
                            int32_t *result, neo_trace_sink *sink, void *context) {
    size_t receiver=neo_object(session,object);
    size_t owner;
    size_t handler=neo_behavior(session,receiver,"handler",selector,&owner);
    if (!handler) { return "unresolved receiver or method"; }
    size_t label=session->cells[session->cells[handler].child].next;
    size_t first=session->cells[label].next;
    size_t expected;
    size_t body=neo_signature(session,first,&expected);
    if (count!=expected) { return "wrong argument count"; }
    if (count && !arguments) { return "missing argument values"; }
    neo_trace_request trace={sink,context,
        {NEO_TRACE_SEND,NULL,neo_object_name(session,receiver),selector,
         neo_object_name(session,owner),0,false}};
    return neo_execute(session,body,receiver,platform,result,first,arguments,count,
                       sink ? &trace : NULL);
}

const char *neo_session_send(neo_session *session, const char *object, const char *selector,
                             const neo_code_platform *platform, int32_t *result) {
    return neo_send(session,object,selector,NULL,0,platform,result,NULL,NULL);
}

const char *neo_session_send_value(neo_session *session, const char *object, const char *selector,
                                   int32_t argument, const neo_code_platform *platform,
                                   int32_t *result) {
    return neo_send(session,object,selector,&argument,1,platform,result,NULL,NULL);
}
const char *neo_session_send_args(neo_session *session, const char *object, const char *selector,
                                  const int32_t *arguments, size_t count,
                                  const neo_code_platform *platform, int32_t *result) {
    return neo_send(session,object,selector,arguments,count,platform,result,NULL,NULL);
}
const char *neo_session_trace(neo_session *session, const char *object, const char *selector,
                              const int32_t *arguments, size_t count, const neo_code_platform *platform,
                              int32_t *result, neo_trace_sink *sink, void *context) {
    return neo_send(session,object,selector,arguments,count,platform,result,sink,context);
}
const char *neo_session_reload(neo_session **session, uint64_t expected, const char *source) {
    neo_jit *current=*session;
    if (current->revision!=expected) { return "stale graph revision"; }
    if (expected==UINT64_MAX) { return "graph revision limit exceeded"; }
    neo_jit *next;
    const char *failure=neo_session_open(source,&next);
    if (failure) { return failure; }
    for (size_t object=next->objects; object; object=next->cells[object].next) {
        size_t name=next->cells[next->cells[object].child].next;
        size_t old=neo_object(current,next->cells[name].atom);
        if (!old) { continue; }
        for (size_t item=next->cells[name].next; item; item=next->cells[item].next) {
            size_t tag=next->cells[item].child;
            if (!neo_is(next,tag,"slot")) { continue; }
            size_t label=next->cells[tag].next;
            size_t previous=neo_member(current,old,"slot",next->cells[label].atom);
            if (previous && !neo_slot_literal(current,previous) && !neo_slot_literal(next,item)) {
                next->slots[item]=current->slots[previous];
            } else if (previous && next->text_slots[item][0] &&
                       current->text_slots[previous][0]==next->text_slots[item][0] &&
                       (next->text_slots[item][0]!='~' ||
                        (current->text_slots[previous][1]=='[')==(next->text_slots[item][1]=='['))) {
                const char *value=current->text_slots[previous];
                if (*value!='@' || neo_object(next,value+1)) {
                    if (*value=='@' && !neo_same_package(next,object,neo_object(next,value+1))) {
                        neo_session_close(next);
                        return "live reference would cross package boundary";
                    }
                    strcpy(next->text_slots[item],value);
                }
            }
        }
    }
    next->revision=expected+1;
    next->document=current->document;
    next->document_sink=current->document_sink;
    next->document_context=current->document_context;
    next->schedule=current->schedule;
    next->schedule_context=current->schedule_context;
    next->buffer=current->buffer;
    next->buffer_context=current->buffer_context;
    strcpy(next->buffer_package,current->buffer_package);
    next->document_rights=current->document_rights;
    memcpy(next->imports,current->imports,sizeof(next->imports));
    strcpy(next->document_package,current->document_package);
    *session=next;
    neo_session_close(current);
    return NULL;
}

const char *neo_session_clone(neo_session **session, uint64_t revision,
                               const char *prototype, const char *name) {
    neo_jit *current=*session;
    if (revision!=current->revision) { return "stale graph revision"; }
    if (!*name || strlen(name)>62 || strchr("\"#[@~",*name)) { return "invalid clone name"; }
    for (const unsigned char *p=(const unsigned char *)name; *p; ++p) {
        if (*p<=32 || *p>=127 || strchr("()\"/",*p)) { return "invalid clone name"; }
    }
    size_t source=neo_object(current,prototype);
    if (!source) { return "unknown prototype"; }
    if (neo_object(current,name)) { return "object name already exists"; }
    char *warp=neo_platform_malloc(NEO_BYTES+1u);
    if (!warp) { return "out of memory"; }
    size_t length=0;
    const char *failure="cloned image exceeds limit";
    if (neo_serialize_cell(current,current->root,warp,NEO_BYTES+1u,&length,-1)) {
        --length;
        int written=snprintf(warp+length,NEO_BYTES+1u-length,
                             " (object %s (delegate @%s)",name,prototype);
        if (written>0 && (size_t)written<NEO_BYTES+1u-length) {
            length+=(size_t)written;
            bool valid=true;
            size_t label=current->cells[current->cells[source].child].next;
            for (size_t member=current->cells[label].next; member; member=current->cells[member].next) {
                size_t tag=current->cells[member].child;
                if (!neo_is(current,tag,"slot") && !neo_is(current,tag,"link") &&
                    !neo_is(current,tag,"package") && !neo_is(current,tag,"import")) { continue; }
                if (length>=NEO_BYTES) { valid=false; break; }
                warp[length++]=' ';
                if (!neo_serialize_cell(current,member,warp,NEO_BYTES+1u,&length,0)) {
                    valid=false;
                    break;
                }
            }
            if (valid && length+2<=NEO_BYTES) {
                memcpy(warp+length,"))",3);
                failure=neo_session_reload(session,revision,warp);
            }
        }
    }
    neo_platform_free(warp);
    return failure;
}

static bool neo_mentions_reference(const neo_jit *jit, size_t id, const char *name) {
    const neo_cell *cell=&jit->cells[id];
    if (!cell->child) {
        return cell->atom[0]=='@' && !strcmp(cell->atom+1,name);
    }
    if (neo_is(jit,cell->child,"slot") && jit->text_slots[id][0]=='@') {
        return !strcmp(jit->text_slots[id]+1,name);
    }
    for (size_t child=cell->child; child; child=jit->cells[child].next) {
        if (neo_mentions_reference(jit,child,name)) { return true; }
    }
    return false;
}

const char *neo_session_delete(neo_session **session, uint64_t revision, const char *name) {
    neo_jit *current=*session;
    if (revision!=current->revision) { return "stale graph revision"; }
    size_t object=neo_object(current,name);
    if (!object) { return "unknown deletion object"; }
    if (object==current->receiver) { return "cannot delete the image entry receiver"; }
    for (size_t other=current->objects; other; other=current->cells[other].next) {
        if (other!=object && neo_mentions_reference(current,other,name)) {
            return "object is still referenced";
        }
    }
    neo_jit *candidate=neo_platform_malloc(sizeof(*candidate));
    char *warp=neo_platform_malloc(NEO_BYTES+1u);
    const char *failure="out of memory";
    if (candidate && warp) {
        *candidate=*current;
        size_t previous=current->cells[current->cells[current->root].child].next;
        previous=current->cells[previous].next; /* Entry precedes the first object. */
        while (candidate->cells[previous].next!=object) {
            previous=candidate->cells[previous].next;
        }
        candidate->cells[previous].next=candidate->cells[object].next;
        size_t length=0;
        if (!neo_serialize_cell(candidate,candidate->root,warp,NEO_BYTES+1u,&length,2)) {
            failure="serialized Warp image exceeds limit";
        } else {
            warp[length]=0;
            failure=neo_session_reload(session,revision,warp);
        }
    }
    neo_platform_free(candidate);
    neo_platform_free(warp);
    return failure;
}

const char *neo_session_snapshot(const neo_session *session, char *output, size_t capacity) {
    size_t length=0;
    if (!capacity || !neo_serialize_cell(session,session->root,output,capacity,&length,2)) {
        return "serialized Warp image exceeds limit";
    }
    output[length]=0;
    return NULL;
}

typedef struct {
    char *text;
    size_t length, capacity;
} neo_projection;

static bool neo_project(neo_projection *output, const char *format, ...) {
    va_list arguments;
    va_start(arguments,format);
    int written=vsnprintf(output->text+output->length,output->capacity-output->length,
                          format,arguments);
    va_end(arguments);
    if (written<0 || (size_t)written>=output->capacity-output->length) { return false; }
    output->length+=(size_t)written;
    return true;
}

static bool neo_project_expression(const neo_session *session, size_t id, neo_projection *output) {
    const neo_cell *cells=session->cells;
    size_t op=cells[id].child;
    if (!op) { return neo_project(output,"%s",cells[id].atom); }
    size_t first=cells[op].next;
    size_t second=cells[first].next, third=cells[second].next;
    const char *operator=cells[op].atom;
    if (operator[0] && !operator[1] && strchr("=<>+-*",operator[0]) && second && !third) {
        size_t left_op=cells[first].child;
        bool group_left=neo_is(session,left_op,"set") || neo_is(session,left_op,"return");
        return neo_project(output,group_left ? "((" : "(") &&
            neo_project_expression(session,first,output) &&
            (!group_left || neo_project(output,")")) &&
            neo_project(output," %s ",operator) && neo_project_expression(session,second,output) &&
            neo_project(output,")");
    }
    if (neo_is(session,op,"if") && second && !cells[third].next) {
        if (!neo_project(output,"(if (") || !neo_project_expression(session,first,output) ||
            !neo_project(output,") then (") || !neo_project_expression(session,second,output) ||
            !neo_project(output,")")) { return false; }
        return (!third || (neo_project(output," else (") &&
                           neo_project_expression(session,third,output) &&
                           neo_project(output,")"))) && neo_project(output,")");
    }
    if (neo_is(session,op,"while") && second && !third) {
        return neo_project(output,"(while (") && neo_project_expression(session,first,output) &&
            neo_project(output,") then (") && neo_project_expression(session,second,output) &&
            neo_project(output,"))");
    }
    bool send=neo_is(session,op,"post") || neo_is(session,op,"send") || neo_is(session,op,"send-text") || neo_is(session,op,"send-bytes") || neo_is(session,op,"send-vector") || neo_is(session,op,"send-ref") || neo_is(session,op,"send-f64") || neo_is(session,op,"send-f64-vector"),
        call=neo_is(session,op,"call") || neo_is(session,op,"call-vector") || neo_is(session,op,"call-f64") || neo_is(session,op,"call-f64-vector");
    if (send || call) {
        if (!second || !neo_project(output,"%s ",cells[op].atom)) { return false; }
        if (cells[first].child) {
            if (!neo_project(output,"(") || !neo_project_expression(session,first,output) ||
                !neo_project(output,")")) { return false; }
        } else if (!neo_project(output,"%s",cells[first].atom)) { return false; }
        if (!neo_project(output,".%s(",cells[second].atom)) {
            return false;
        }
        for (size_t arg=third; arg; arg=cells[arg].next) {
            if ((arg!=third && !neo_project(output,", ")) ||
                !neo_project_expression(session,arg,output)) { return false; }
        }
        return neo_project(output,")");
    }
    bool get=neo_is(session,op,"get"), set=neo_is(session,op,"set");
    if (get || set) {
        return second && (set ? third && !cells[third].next : !third) &&
            neo_project(output,"%s %s.%s",cells[op].atom,cells[first].atom,cells[second].atom) &&
            (!set || (neo_project(output," = ") && neo_project_expression(session,third,output)));
    }
    if (neo_is(session,op,"return")) {
        return first && !second && neo_project(output,"return ") &&
            neo_project_expression(session,first,output);
    }
    if (!cells[op].atom[0] || !neo_project(output,"%s(",cells[op].atom)) { return false; }
    for (size_t arg=first; arg; arg=cells[arg].next) {
        if ((arg!=first && !neo_project(output,", ")) ||
            !neo_project_expression(session,arg,output)) { return false; }
    }
    return neo_project(output,")");
}

/* A block recreates the existing outer do. Group a literal let form so
   the reader keeps its original scope instead of extending the block. */
static bool neo_project_steps(const neo_session *session, size_t step, neo_projection *output) {
    const neo_cell *cells=session->cells;
    for (; step; step=cells[step].next) {
        size_t op=cells[step].child;
        size_t test=cells[op].next, yes=cells[test].next;
        bool guard=neo_is(session,op,"if") && yes && !cells[yes].next &&
            neo_is(session,cells[yes].child,"return");
        bool binding=neo_is(session,op,"let");
        if (!neo_project(output,"        ")) { return false; }
        if (guard) {
            if (!neo_project(output,"if ") || !neo_project_expression(session,test,output) ||
                !neo_project(output," then ") ||
                !neo_project_expression(session,yes,output)) { return false; }
        } else {
            if ((binding && !neo_project(output,"(")) ||
                !neo_project_expression(session,step,output) ||
                (binding && !neo_project(output,")"))) { return false; }
        }
        if (!neo_project(output,";\n")) { return false; }
    }
    return true;
}
static bool neo_project_member(const neo_session *session, size_t member, neo_projection *output) {
    const neo_cell *cells=session->cells;
    size_t tag=cells[member].child, name=cells[tag].next, value=cells[name].next;
    if (neo_is(session,tag,"package") || neo_is(session,tag,"export")) {
        return neo_project(output,"    %s %s;\n",cells[tag].atom,cells[name].atom);
    }
    if (neo_is(session,tag,"import")) {
        size_t selector=cells[value].next;
        return neo_project(output,"    import %s = %s.%s;\n",cells[name].atom,
                           cells[value].atom,cells[selector].atom);
    }
    if (neo_is(session,tag,"delegate")) {
        return neo_project(output,"    delegate %s;\n",cells[name].atom);
    }
    if (neo_is(session,tag,"slot") || neo_is(session,tag,"link")) {
        return neo_project(output,"    %s %s = %s;\n",cells[tag].atom,cells[name].atom,
                           cells[value].atom);
    }
    size_t count;
    size_t body=neo_signature(session,value,&count);
    if (!neo_project(output,"    %s %s(",cells[tag].atom,cells[name].atom)) { return false; }
    for (size_t id=value; id!=body; id=cells[id].next) {
        if (!neo_project(output,"%s%s",id==value ? "" : ", ",cells[id].atom)) { return false; }
    }
    size_t op=cells[body].child;
    if (neo_is(session,tag,"handler") && neo_is(session,op,"do") && cells[op].next) {
        return neo_project(output,") {\n") && neo_project_steps(session,cells[op].next,output) &&
            neo_project(output,"    }\n");
    }
    return neo_project(output,") = ") && neo_project_expression(session,body,output) &&
        neo_project(output,";\n");
}
/* Render the deliberately small subset admitted as a state expression.
   A missing rule makes the view unresolved. No expression is run here. */
static const char *neo_transition_expression(const neo_jit *jit, size_t id, const char *slot,
                                             neo_projection *output) {
    const neo_cell *cells=jit->cells;
    const char *limit="inspection output exceeds limit";
    size_t op=cells[id].child;
    if (!op) { return !output || neo_project(output,"%s",cells[id].atom) ? NULL : limit; }
    size_t first=cells[op].next, second=cells[first].next;
    if (neo_is(jit,op,"get")) {
        if (!neo_is(jit,first,"self") || !second || cells[second].next) {
            return "state read outside the receiver";
        }
        if (!neo_is(jit,second,slot)) { return "state read requires another slot"; }
        return !output || neo_project(output,"state.%s",cells[second].atom) ? NULL : limit;
    }
    if (neo_is(jit,op,"read-document") || neo_is(jit,op,"write-document")) {
        return "document I/O";
    }
    if (!neo_is(jit,op,"call") && !neo_is(jit,op,"if") &&
        !neo_is(jit,op,"=") && !neo_is(jit,op,"<") && !neo_is(jit,op,">")) {
        return "operation outside the scalar state subset";
    }
    bool call=neo_is(jit,op,"call");
    if (call) {
        if (!second || (output && !neo_project(output,"%s.%s(",cells[first].atom,cells[second].atom))) {
            return limit;
        }
        first=cells[second].next;
    } else if (output && !neo_project(output,"%s(",cells[op].atom)) { return limit; }
    for (size_t arg=first; arg; arg=cells[arg].next) {
        if (output && arg!=first && !neo_project(output,", ")) { return limit; }
        const char *failure=neo_transition_expression(jit,arg,slot,output);
        if (failure) { return failure; }
    }
    return !output || neo_project(output,")") ? NULL : limit;
}

typedef struct {
    size_t name, first, count, body, slot, value;
} neo_transition;

static const char *neo_transition_shape(const neo_session *session, size_t receiver,
                                        size_t member, neo_transition *transition) {
    const neo_cell *cells=session->cells;
    transition->name=cells[cells[member].child].next;
    transition->first=cells[transition->name].next;
    transition->body=neo_signature(session,transition->first,&transition->count);
    size_t op=cells[transition->body].child;
    size_t target=cells[op].next, slot=cells[target].next, value=cells[slot].next;
    if (neo_is(session,op,"read-document") || neo_is(session,op,"write-document")) {
        return "document I/O";
    }
    if (!neo_is(session,op,"set") || !neo_is(session,target,"self") ||
        !cells[slot].atom[0] || !value || cells[value].next) {
        return "expected one receiver slot write";
    }
    transition->slot=neo_member(session,receiver,"slot",cells[slot].atom);
    transition->value=value;
    if (neo_slot_literal(session,transition->slot)) { return "transition requires an integer slot"; }
    return transition->slot ? NULL : "unresolved receiver slot";
}

static const char *neo_transition_slot(const neo_session *session, size_t slot) {
    return session->cells[session->cells[session->cells[slot].child].next].atom;
}

static bool neo_project_knight(const neo_session *session, size_t receiver, size_t member,
                               const char *kind, neo_projection *output) {
    if (!strcmp(kind,"knight")) { return neo_project_member(session,member,output); }
    if (strcmp(kind,"handler")) { return neo_project(output,"no Knight view for %s\n",kind); }
    const neo_cell *cells=session->cells;
    neo_transition shape;
    const char *failure=neo_transition_shape(session,receiver,member,&shape);
    if (failure) { return neo_project(output,"unresolved: %s\n",failure); }
    const char *slot=neo_transition_slot(session,shape.slot);
    size_t start=output->length;
    if (!neo_project(output,"transition %s(state",cells[shape.name].atom)) { return false; }
    for (size_t param=shape.first; param!=shape.body; param=cells[param].next) {
        if (!neo_project(output,", %s",cells[param].atom)) { return false; }
    }
    if (!neo_project(output,") -> { nextState, result }\n  result = ")) { return false; }
    failure=neo_transition_expression(session,shape.value,slot,output);
    if (!failure) {
        neo_jit *check=neo_platform_malloc(sizeof(*check));
        if (!check) { return false; }
        *check=*session;
        failure=neo_prepare(check,shape.body,receiver,0,shape.first,shape.count);
        neo_platform_free(check);
    }
    if (failure) {
        output->length=start;
        return neo_project(output,"unresolved: %s\n",failure);
    }
    return neo_project(output,"\n  nextState = state with %s = result\n",slot);
}
const char *neo_session_evaluate_knight(const neo_session *session, uint64_t revision,
                                        const char *object, const char *handler, int32_t state,
                                        const int32_t *arguments, size_t count,
                                        const neo_code_platform *platform, int32_t *next,
                                        int32_t *result) {
    if (revision!=session->revision) { return "stale graph revision"; }
    size_t receiver=neo_object(session,object);
    size_t member=neo_behavior(session,receiver,"handler",handler,NULL);
    if (!member) { return "unknown transition handler"; }
    neo_transition shape;
    const char *failure=neo_transition_shape(session,receiver,member,&shape);
    if (failure) { return failure; }
    if (count!=shape.count || (count && !arguments)) { return "wrong argument count"; }
    failure=neo_transition_expression(session,shape.value,neo_transition_slot(session,shape.slot),NULL);
    if (failure) { return failure; }

    neo_jit *scratch=neo_platform_malloc(sizeof(*scratch));
    if (!scratch) { return "out of memory"; }
    *scratch=*session;
    memset(scratch->slots,0,sizeof(scratch->slots));
    scratch->document=NULL;
    scratch->document_rights=0;
    scratch->slots[shape.slot]=state;
    failure=neo_execute(scratch,shape.body,receiver,platform,result,shape.first,
                        arguments,count,NULL);
    if (!failure) { *next=scratch->slots[shape.slot]; }
    neo_platform_free(scratch);
    return failure;
}
static size_t neo_parse_candidate(neo_jit *scratch, const char *source) {
    scratch->cursor=source;
    size_t body=neo_parse(scratch,0);
    neo_skip(scratch);
    if (*scratch->cursor) { body=0; }
    scratch->cursor=NULL;
    return body;
}
const char *neo_session_receive_sequence(neo_session *session, const char *object, const char *selector,
                                     const int32_t *arguments, size_t count,
                                     const neo_code_platform *platform,
                                     char kind, char result[NEO_TEXT_RESULT_CAPACITY]) {
    int32_t length;
    session->text_result=result;
    session->receive_kind=kind;
    const char *failure=neo_send(session,object,selector,arguments,count,platform,&length,NULL,NULL);
    session->text_result=NULL;
    session->receive_kind=0;
    return failure;
}
const char *neo_session_send_text(neo_session *session, const char *object, const char *selector,
                                  const char *literal, const neo_code_platform *platform,
                                  int32_t *result) {
    const char *end=literal;
    size_t bytes;
    if (!neo_text_literal(&end,&bytes) || *end) { return "expected one quoted text argument"; }
    size_t length=(size_t)(end-literal);
    size_t count=session->count;
    if (length>=sizeof(session->cells[0].atom)) { return "quoted text argument exceeds atom limit"; }
    if (count+1>=NEO_CELLS) { return "graph cell limit exceeded"; }

    size_t id=++session->count;
    session->cells[id]=(neo_cell){0};
    memcpy(session->cells[id].atom,literal,length);
    session->text_argument=id;
    const int32_t placeholder=0;
    const char *failure=neo_send(session,object,selector,&placeholder,1,platform,result,NULL,NULL);
    session->text_argument=0;
    session->cells[id]=(neo_cell){0};
    session->count=count;
    return failure;
}

const char *neo_session_probe_knight(const neo_session *session, uint64_t revision,
                                     const char *object, const char *function, const char *source,
                                     const int32_t *arguments, size_t count,
                                     const neo_code_platform *platform, int32_t *result) {
    if (revision!=session->revision) { return "stale graph revision"; }
    size_t receiver=neo_object(session,object);
    size_t member=neo_behavior(session,receiver,"knight",function,NULL);
    if (!member) { return "unknown Knight definition"; }
    size_t label=session->cells[session->cells[member].child].next;
    size_t first=session->cells[label].next, expected;
    size_t body=neo_signature(session,first,&expected);
    if (count!=expected || (count && !arguments)) { return "wrong argument count"; }

    neo_jit *scratch=neo_platform_malloc(sizeof(*scratch));
    if (!scratch) { return "out of memory"; }
    *scratch=*session;
    memset(scratch->slots,0,sizeof(scratch->slots));
    scratch->document=NULL;
    scratch->document_rights=0;
    const char *failure=NULL;
    if (source) {
        body=neo_parse_candidate(scratch,source);
        if (!body) { failure="expected one detached Warp expression"; }
    }
    if (!failure) {
        failure=neo_execute(scratch,body,0,platform,result,first,arguments,count,NULL);
    }
    neo_platform_free(scratch);
    return failure;
}
static size_t neo_replace_atoms(neo_jit *graph, size_t id, const char *from, const char *to) {
    neo_cell *cell=&graph->cells[id];
    if (!cell->child) {
        if (strcmp(cell->atom,from)) { return 0; }
        memcpy(cell->atom,to,strlen(to)+1);
        return 1;
    }
    size_t changes=0;
    for (size_t child=cell->child; child; child=graph->cells[child].next) {
        changes+=neo_replace_atoms(graph,child,from,to);
    }
    return changes;
}

const char *neo_session_transform_knight(const neo_session *session, uint64_t revision,
                                         const char *object, const char *function,
                                         const char *from, const char *to,
                                         char *output, size_t capacity) {
    if (revision!=session->revision) { return "stale graph revision"; }
    size_t receiver=neo_object(session,object);
    size_t member=neo_behavior(session,receiver,"knight",function,NULL);
    if (!member) { return "unknown Knight definition"; }
    size_t label=session->cells[session->cells[member].child].next, count;
    size_t body=neo_signature(session,session->cells[label].next,&count);

    neo_jit *scratch=neo_platform_malloc(sizeof(*scratch));
    if (!scratch) { return "out of memory"; }
    *scratch=*session;
    size_t old_atom=neo_parse_candidate(scratch,from);
    size_t new_atom=neo_parse_candidate(scratch,to);
    const char *failure=NULL;
    if (!old_atom || !new_atom || scratch->cells[old_atom].child || scratch->cells[new_atom].child) {
        failure="transform expects two Warp atoms";
    } else if (!neo_replace_atoms(scratch,body,scratch->cells[old_atom].atom,
                                   scratch->cells[new_atom].atom)) {
        failure="no matching atom in Knight body";
    } else {
        size_t length=0;
        if (!capacity || !neo_serialize_cell(scratch,body,output,capacity,&length,-1)) {
            failure="transformed body exceeds output limit";
        } else {
            output[length]=0;
        }
    }
    neo_platform_free(scratch);
    return failure;
}
const char *neo_session_publish_knight(neo_session **session, uint64_t revision,
                                       const char *object, const char *function,
                                       const char *source) {
    neo_jit *current=*session;
    if (revision!=current->revision) { return "stale graph revision"; }
    size_t owner=neo_object(current,object);
    size_t member=neo_member(current,owner,"knight",function);
    if (!member) { return "unknown local Knight definition"; }
    size_t label=current->cells[current->cells[member].child].next;
    size_t first=current->cells[label].next, count;
    size_t original=neo_signature(current,first,&count);

    neo_jit *scratch=neo_platform_malloc(sizeof(*scratch));
    char *warp=neo_platform_malloc(NEO_BYTES+1u);
    const char *failure="out of memory";
    if (scratch && warp) {
        *scratch=*current;
        size_t candidate=neo_parse_candidate(scratch,source);
        failure="expected one detached Warp expression";
        if (candidate) {
            size_t previous=label;
            while (scratch->cells[previous].next!=original) {
                previous=scratch->cells[previous].next;
            }
            scratch->cells[previous].next=candidate;
            failure=neo_prepare(scratch,candidate,0,0,count ? first : 0,count);
            if (!failure) {
                size_t length=0;
                if (!neo_serialize_cell(scratch,scratch->root,warp,NEO_BYTES+1u,&length,-1)) {
                    failure="serialized Warp image exceeds limit";
                } else {
                    warp[length]=0;
                    failure=neo_session_reload(session,revision,warp);
                }
            }
        }
    }
    neo_platform_free(warp);
    neo_platform_free(scratch);
    return failure;
}
const char *neo_session_inspect(const neo_session *session, const char *object,
                                const char *member, neo_inspect_view view,
                                char *text, size_t capacity) {
    size_t receiver=neo_object(session,object);
    if (!receiver) { return "unknown inspection object"; }
    size_t id=receiver, owner=receiver;
    const neo_cell *cells=session->cells;
    const char *kind="object", *name=object;
    if (member) {
        id=0;
        const char *qualifier=strchr(member,':');
        name=qualifier ? qualifier+1 : member;
        const char *const kinds[]={"slot","link","handler","knight"};
        for (size_t i=0; i<sizeof(kinds)/sizeof(kinds[0]); ++i) {
            const char *candidate_kind=kinds[i];
            if (qualifier && (strlen(candidate_kind)!=(size_t)(qualifier-member) ||
                              strncmp(member,candidate_kind,(size_t)(qualifier-member)))) {
                continue;
            }
            size_t candidate_owner=receiver;
            size_t candidate=i<2 ? neo_member(session,receiver,candidate_kind,name) :
                neo_behavior(session,receiver,candidate_kind,name,&candidate_owner);
            if (!candidate) { continue; }
            if (id) { return "ambiguous inspection member; use KIND:NAME"; }
            id=candidate;
            owner=candidate_owner;
            kind=candidate_kind;
        }
        if (!id) { return "unknown inspection member"; }
    }

    neo_projection output={text,0,capacity};
    const char *limit="inspection cannot be rendered or output exceeds limit";
    if (!capacity ||
        !neo_project(&output,"revision %" PRIu64 " receiver %s owner %s target %s %s\n",
                     session->revision,object,neo_object_name(session,owner),kind,name)) {
        return limit;
    }
    if (view==NEO_INSPECT_WARP) {
        return neo_serialize_cell(session,id,text,capacity,&output.length,-1) &&
            neo_project(&output,"\n") ? NULL : limit;
    }
    if (view==NEO_INSPECT_KNIGHT) {
        return neo_project_knight(session,receiver,id,kind,&output) ? NULL : limit;
    }
    if (view!=NEO_INSPECT_OBJECT) { return "unknown inspection view"; }
    if (member && session->text_slots[id][0]) {
        return neo_project(&output,"    slot %s = %s;\n",name,session->text_slots[id]) ?
            NULL : limit;
    }
    if (member && strcmp(kind,"slot")) {
        return neo_project_member(session,id,&output) ? NULL : limit;
    }
    if (member) {
        return neo_project(&output,"    slot %s = %" PRId32 ";\n",name,session->slots[id]) ?
            NULL : limit;
    }

    if (!neo_project(&output,"object %s {\n",object)) { return limit; }
    size_t label=cells[cells[receiver].child].next;
    for (size_t item=cells[label].next; item; item=cells[item].next) {
        size_t tag=cells[item].child;
        if (session->text_slots[item][0]) {
            if (!neo_project(&output,"    slot %s = %s;\n",cells[cells[tag].next].atom,
                             session->text_slots[item])) { return limit; }
        } else if (neo_is(session,tag,"slot")) {
            if (!neo_project(&output,"    slot %s = %" PRId32 ";\n",
                             cells[cells[tag].next].atom,session->slots[item])) { return limit; }
        } else if (!neo_project_member(session,item,&output)) { return limit; }
    }
    return neo_project(&output,"}\n") ? NULL : limit;
}
const char *neo_session_weft(const neo_session *session, char *text, size_t capacity) {
    const neo_cell *cells=session->cells;
    size_t version=cells[cells[session->root].child].next;
    size_t entry=cells[cells[cells[version].next].child].next;
    neo_projection output={text,0,capacity};
    const char *unsupported="graph cannot be represented in this Weft subset or output exceeds limit";
    if (!capacity || !neo_project(&output,"entry %s.%s;\n",cells[entry].atom,
                                 cells[cells[entry].next].atom)) { return unsupported; }
    for (size_t object=session->objects; object; object=cells[object].next) {
        size_t name=cells[cells[object].child].next;
        if (!neo_project(&output,"\nobject %s {\n",cells[name].atom)) { return unsupported; }
        for (size_t member=cells[name].next; member; member=cells[member].next) {
            if (!neo_project_member(session,member,&output)) { return unsupported; }
        }
        if (!neo_project(&output,"}\n")) { return unsupported; }
    }
    char *warp=neo_platform_malloc(NEO_BYTES+1u);
    neo_jit *check=neo_platform_calloc(1,sizeof(*check));
    const char *failure="out of memory";
    if (warp && check) {
        size_t root,body;
        failure=neo_weft_lower(text,warp,NEO_BYTES+1u);
        if (failure || neo_load_graph(check,warp,&root,&body) || !neo_same_graph(session,check)) {
            failure=unsupported;
        }
    }
    neo_platform_free(check);
    neo_platform_free(warp);
    return failure;
}
const char *neo_snapshot(const char *source, const neo_code_platform *platform,
                         char *output, size_t capacity) {
    neo_jit *jit;
    const char *failure=neo_session_open(source,&jit);
    if (failure) { return failure; }
    int32_t result;
    failure=neo_execute(jit,jit->entry,jit->receiver,platform,&result,0,NULL,0,NULL);
    if (!failure) { failure=neo_session_snapshot(jit,output,capacity); }
    neo_session_close(jit);
    return failure;
}
