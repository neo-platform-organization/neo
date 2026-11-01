#include "internal.h"
#include "execution/evaluator_internal.h"
#include "display/display.h"
#include "io/io.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#define NEO_EVAL_DEPTH 128u

static neo_status neo_evaluate(neo_activation *activation, neo_object *expression,
                               size_t depth, neo_value *out);

neo_status neo_operand(neo_activation *activation, neo_object *operation,
                              const char *name, size_t depth, neo_value *out) {
    neo_object *child = neo_child_named(activation->vm, operation->id, name);
    if (child == NULL && strcmp(name, "condition") == 0 &&
        operation->value.kind == NEO_PRIMITIVE &&
        (strcmp(operation->value.text, "if") == 0 || strcmp(operation->value.text, "while") == 0)) {
        child = neo_child_after(activation->vm, operation->id, 0);
        if (child != NULL && (strcmp(child->name, "then") == 0 ||
            strcmp(child->name, "else") == 0 || strcmp(child->name, "body") == 0)) { child = NULL; }
    }
    if (child == NULL) {
        return NEO_INVALID;
    }
    return neo_evaluate(activation, child, depth + 1, out);
}

static neo_status neo_math(const char *operation, int64_t a, int64_t b, neo_value *out) {
    out->kind = NEO_INTEGER;
    if (strcmp(operation, "add") == 0) {
        if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
            return NEO_OVERFLOW;
        }
        out->integer = a + b;
    } else if (strcmp(operation, "sub") == 0) {
        if ((b > 0 && a < INT64_MIN + b) || (b < 0 && a > INT64_MAX + b)) {
            return NEO_OVERFLOW;
        }
        out->integer = a - b;
    } else if (strcmp(operation, "mul") == 0) {
        if ((a > 0 && b > 0 && a > INT64_MAX / b) ||
            (a > 0 && b < 0 && b < INT64_MIN / a) ||
            (a < 0 && b > 0 && a < INT64_MIN / b) ||
            (a < 0 && b < 0 && a < INT64_MAX / b)) {
            return NEO_OVERFLOW;
        }
        out->integer = a * b;
    } else if (strcmp(operation, "div") == 0 || strcmp(operation, "rem") == 0) {
        if (b == 0) {
            return NEO_DIVIDE_BY_ZERO;
        }
        if (a == INT64_MIN && b == -1) {
            return NEO_OVERFLOW;
        }
        out->integer = strcmp(operation, "div") == 0 ? a / b : a % b;
    } else {
        out->kind = NEO_BOOLEAN;
        if (strcmp(operation, "<") == 0) { out->boolean = a < b; }
        else if (strcmp(operation, "<=") == 0) { out->boolean = a <= b; }
        else if (strcmp(operation, ">") == 0) { out->boolean = a > b; }
        else if (strcmp(operation, ">=") == 0) { out->boolean = a >= b; }
        else { return NEO_UNSUPPORTED; }
    }
    return NEO_OK;
}

static bool neo_equal(neo_value a, neo_value b) {
    if (a.kind != b.kind) { return false; }
    switch (a.kind) {
        case NEO_INTEGER: return a.integer == b.integer;
        case NEO_BOOLEAN: return a.boolean == b.boolean;
        case NEO_TEXT: return strcmp(a.text, b.text) == 0;
        case NEO_OBJECT: return true; /* unit payload only */
        case NEO_INTEGERS:
            return a.count == b.count && (a.count == 0 ||
                memcmp(a.integers, b.integers, a.count * sizeof(int64_t)) == 0);
        case NEO_REFERENCE: return false;
        case NEO_PRIMITIVE: return false;
    }
    return false;
}

bool neo_primitive_known(const char *op) {
    if (neo_message_primitive_known(op)) { return true; }
    if (neo_graph_primitive_known(op)) { return true; }
    static const char *const primitives[] = {
        "network-open", "network-ready", "network-port", "network-stream", "network-close",
        "platform-info", "array-new", "array-get", "array-set", "array-size", "call", "invoke", "do", "if", "while", "return", "fail", "attempt", "failure", "rethrow", "read", "write", "message", "send",
        "not", "and", "or", "==", "!=", "add", "sub", "mul", "div", "rem", "<", "<=", ">", ">=",
        "buffer-fill", "buffer-read", "buffer-write", "buffer-size", "window-present", "window-poll",
        "window-width", "window-height", "window-capture", "window-state", "stream-read", "stream-write", "stream-read-text",
        "buffer-width", "buffer-height", "window-next-event", "window-event"
        , "buffer-create", "buffer-copy", "buffer-scale-copy", "surface-create", "surface-focus", "surface-route"
    };
    bool known = false;
    for (size_t i = 0; i < sizeof(primitives) / sizeof(primitives[0]); ++i) {
        if (strcmp(op, primitives[i]) == 0) { known = true; break; }
    }
    return known;

}

static neo_status neo_surface_operation(neo_activation *a, neo_object *node,
                                        size_t depth, neo_value *out) {
    const char *op = node->value.text;
    const char *names[8] = {"target", "source", "x", "y", "width", "height", "output-width", "output-height"};
    size_t count = 6;
    if (strcmp(op,"buffer-scale-copy") == 0) { count=8; }
    bool create = strcmp(op,"buffer-create") == 0 || strcmp(op,"surface-create") == 0;
    if (create) { names[1]="name"; names[2]="width"; names[3]="height"; count=4; }
    else if (strcmp(op,"surface-focus") == 0) { names[1]="value"; count=2; }
    else if (strcmp(op,"surface-route") == 0) { count=4; }
    neo_value v[8] = {{0}};
    neo_status status = NEO_OK;
    for (size_t i=0; i<count && status==NEO_OK && !a->returning; ++i) {
        status=neo_operand(a,node,names[i],depth,&v[i]);
    }
    if (status != NEO_OK || a->returning) { goto done; }
    if (v[0].kind != NEO_REFERENCE) { status=NEO_WRONG_KIND; goto done; }
    if (create) {
        if (v[1].kind != NEO_TEXT || v[2].kind != NEO_INTEGER || v[3].kind != NEO_INTEGER) { status=NEO_WRONG_KIND; goto done; }
        if (v[2].integer <= 0 || v[3].integer <= 0 || (uint64_t)v[2].integer > SIZE_MAX || (uint64_t)v[3].integer > SIZE_MAX) { status=NEO_LIMIT; goto done; }
        const neo_capability *result=NULL;
        status=neo_graph_mutable(a,v[0].reference,false,false);
        if (status == NEO_OK) {
            status=strcmp(op,"buffer-create") == 0 ?
                neo_buffer_create(a->vm,v[0].reference,v[1].text,(size_t)v[2].integer,(size_t)v[3].integer,&result) :
                neo_surface_create(a->vm,v[0].reference,v[1].text,(size_t)v[2].integer,(size_t)v[3].integer,&result);
        }
        if (status == NEO_OK) { *out=(neo_value){.kind=NEO_REFERENCE,.reference=result}; }
    } else if (strcmp(op,"surface-focus") == 0) {
        status=v[1].kind == NEO_BOOLEAN ? neo_surface_focus(a->vm,v[0].reference,v[1].boolean) : NEO_WRONG_KIND;
    } else {
        if (v[1].kind != NEO_REFERENCE || v[2].kind != NEO_INTEGER || v[3].kind != NEO_INTEGER) { status=NEO_WRONG_KIND; goto done; }
        if (strcmp(op,"surface-route") == 0) {
            status=neo_surface_route(a->vm,v[0].reference,v[1].reference,v[2].integer,v[3].integer);
        } else {
            if (v[4].kind != NEO_INTEGER || v[5].kind != NEO_INTEGER) { status=NEO_WRONG_KIND; goto done; }
            if (v[4].integer < 0 || v[5].integer < 0 || (uint64_t)v[4].integer > SIZE_MAX || (uint64_t)v[5].integer > SIZE_MAX) { status=NEO_LIMIT; goto done; }
            /* Charge bounded memory work before mutation, in64-byte units. */
            uint64_t width=(uint64_t)v[4].integer, height=(uint64_t)v[5].integer;
            if (width > 16777216u || height > 16777216u || width * height > 16777216u) { status=NEO_LIMIT; goto done; }
            uint64_t output_width=width,output_height=height;
            if (count == 8) {
                if (v[6].kind != NEO_INTEGER || v[7].kind != NEO_INTEGER) { status=NEO_WRONG_KIND; goto done; }
                if (v[6].integer < 0 || v[7].integer < 0) { status=NEO_LIMIT; goto done; }
                output_width=(uint64_t)v[6].integer; output_height=(uint64_t)v[7].integer;
                if (output_width > 16777216u || output_height > 16777216u || output_width*output_height > 16777216u) { status=NEO_LIMIT; goto done; }
            }
            size_t work=0;
            status=neo_buffer_transfer_work(a->vm,v[0].reference,v[2].integer,v[3].integer,(size_t)output_width,(size_t)output_height,&work);
            if (status != NEO_OK) { goto done; }
            if (work > a->budget - a->report->steps) { status=NEO_LIMIT; goto done; }
            a->report->steps += work;
            status=count == 8 ? neo_buffer_scale_copy(a->vm,v[0].reference,v[1].reference,v[2].integer,v[3].integer,(size_t)width,(size_t)height,(size_t)output_width,(size_t)output_height) :
                neo_buffer_copy_region(a->vm,v[0].reference,v[1].reference,v[2].integer,v[3].integer,(size_t)width,(size_t)height);
        }
    }
done:
    for (size_t i=0; i<8; ++i) { neo_value_free(a->vm,v[i]); }
    return status;
}

static neo_status neo_display_operation(neo_activation *a, neo_object *node,
                                        size_t depth, neo_value *out) {
    const char *op = node->value.text;
    neo_value target_name = {0}, operand = {0}, value = {0};
    const neo_capability *target = NULL;
    neo_status status = neo_operand(a, node, "target", depth, &target_name);
    if (status != NEO_OK || a->returning) { goto done; }
    if (target_name.kind == NEO_REFERENCE) { target=target_name.reference; }
    else if (target_name.kind == NEO_TEXT) { status = neo_object_connection(a->vm, a->authority, target_name.text, &target); }
    else { status = NEO_WRONG_KIND; }
    if (status != NEO_OK) { goto done; }
    if (strcmp(op, "window-capture") == 0) {
        status = neo_operand(a, node, "value", depth, &value);
        if (status != NEO_OK || a->returning) { goto done; }
        if (value.kind != NEO_BOOLEAN) { status = NEO_WRONG_KIND; goto done; }
        status = neo_window_capture(a->vm, target, value.boolean);
        if (status == NEO_OK) { *out = (neo_value){.kind=NEO_BOOLEAN,.boolean=value.boolean}; }
    } else if (strcmp(op, "window-state") == 0) {
        status = neo_operand(a, node, "field", depth, &operand);
        if (status != NEO_OK || a->returning) { goto done; }
        if (operand.kind != NEO_TEXT) { status = NEO_WRONG_KIND; goto done; }
        neo_window_state state = {0};
        status = neo_window_get_state(a->vm, target, &state);
        if (status != NEO_OK) { goto done; }
        if (strcmp(operand.text,"focused") == 0) { *out = (neo_value){.kind=NEO_BOOLEAN,.boolean=state.focused}; }
        else if (strcmp(operand.text,"captured") == 0) { *out = (neo_value){.kind=NEO_BOOLEAN,.boolean=state.captured}; }
        else if (strcmp(operand.text,"presentation-lost") == 0) { *out = (neo_value){.kind=NEO_BOOLEAN,.boolean=state.presentation_lost}; }
        else if (strcmp(operand.text,"scale-numerator") == 0) { *out = (neo_value){.kind=NEO_INTEGER,.integer=state.scale_numerator}; }
        else if (strcmp(operand.text,"scale-denominator") == 0) { *out = (neo_value){.kind=NEO_INTEGER,.integer=state.scale_denominator}; }
        else if (strcmp(operand.text,"surface-epoch") == 0) {
            if (state.surface_epoch > INT64_MAX) { status = NEO_LIMIT; }
            else { *out = (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)state.surface_epoch}; }
        }
        else if (strcmp(operand.text,"input-epoch") == 0) {
            if (state.input_epoch > INT64_MAX) { status = NEO_LIMIT; }
            else { *out = (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)state.input_epoch}; }
        } else { status = NEO_INVALID; }
    } else if (strcmp(op, "buffer-fill") == 0) {
        status = neo_operand(a, node, "value", depth, &value);
        if (status != NEO_OK || a->returning) { goto done; }
        if (value.kind != NEO_INTEGER) { status = NEO_WRONG_KIND; goto done; }
        if (value.integer < 0 || value.integer > 255) { status = NEO_LIMIT; goto done; }
        status = neo_buffer_fill(a->vm, target, (uint8_t)value.integer);
    } else if (strcmp(op, "window-next-event") == 0) {
        status = neo_value_copy(a->vm, (neo_value){.kind = NEO_TEXT, .text = "input-reset"}, out);
        if (status != NEO_OK) { goto done; }
        neo_input_event event;
        status = neo_window_next_event(a->vm, target, &event);
        if (status == NEO_OK) { strcpy((char *)out->text, neo_input_kind_name(event.kind)); }
    } else if (strcmp(op, "window-event") == 0) {
        status = neo_operand(a, node, "field", depth, &operand);
        if (status != NEO_OK || a->returning) { goto done; }
        if (operand.kind != NEO_TEXT) { status = NEO_WRONG_KIND; goto done; }
        neo_input_event event;
        status = neo_window_get_event(a->vm, target, &event);
        if (status != NEO_OK) { goto done; }
        const char *field = operand.text;
        if (strcmp(field, "type") == 0 || strcmp(field, "key") == 0 || strcmp(field, "text") == 0) {
            status = neo_value_copy(a->vm, (neo_value){.kind = NEO_TEXT,
                .text = strcmp(field, "type") == 0 ? neo_input_kind_name(event.kind) :
                    strcmp(field, "text") == 0 ? event.text : event.key}, out);
        } else if (strcmp(field, "epoch") == 0) {
            if (event.epoch > INT64_MAX) { status = NEO_LIMIT; }
            else { *out = (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)event.epoch}; }
        } else if (strcmp(field, "pressed") == 0) {
            *out = (neo_value){.kind = NEO_BOOLEAN, .boolean = event.pressed};
        } else if (strcmp(field,"control") == 0 || strcmp(field,"shift") == 0) {
            *out=(neo_value){.kind=NEO_BOOLEAN,.boolean=strcmp(field,"control") == 0 ? event.control : event.shift};
        } else {
            out->kind = NEO_INTEGER;
            if (strcmp(field, "x") == 0) { out->integer = event.x; }
            else if (strcmp(field, "y") == 0) { out->integer = event.y; }
            else if (strcmp(field, "width") == 0) { out->integer = event.width; }
            else if (strcmp(field, "height") == 0) { out->integer = event.height; }
            else if (strcmp(field, "button") == 0) { out->integer = event.button; }
            else if (strcmp(field, "lost") == 0 && event.lost <= INT64_MAX) { out->integer = (int64_t)event.lost; }
            else { status = strcmp(field, "lost") == 0 ? NEO_LIMIT : NEO_INVALID; }
        }
    } else if (strcmp(op, "buffer-width") == 0 || strcmp(op, "buffer-height") == 0) {
        size_t width = 0, height = 0;
        status = neo_buffer_dimensions(a->vm, target, &width, &height);
        if (status == NEO_OK) {
            *out = (neo_value){.kind = NEO_INTEGER,
                .integer = (int64_t)(strcmp(op, "buffer-width") == 0 ? width : height)};
        }
    } else if (strcmp(op, "buffer-size") == 0) {
        size_t size = 0;
        status = neo_buffer_size(a->vm, target, &size);
        if (status == NEO_OK) { *out = (neo_value){.kind = NEO_INTEGER, .integer = (int64_t)size}; }
    } else if (strcmp(op, "buffer-read") == 0 || strcmp(op, "buffer-write") == 0) {
        status = neo_operand(a, node, "index", depth, &operand);
        if (status != NEO_OK || a->returning) { goto done; }
        if (operand.kind != NEO_INTEGER) { status = NEO_WRONG_KIND; goto done; }
        if (operand.integer < 0 || (uint64_t)operand.integer > SIZE_MAX) {
            status = NEO_LIMIT; goto done;
        }
        uint8_t byte = 0;
        if (strcmp(op, "buffer-write") == 0) {
            status = neo_operand(a, node, "value", depth, &value);
            if (status != NEO_OK || a->returning) { goto done; }
            if (value.kind != NEO_INTEGER) { status = NEO_WRONG_KIND; goto done; }
            if (value.integer < 0 || value.integer > 255) { status = NEO_LIMIT; goto done; }
            byte = (uint8_t)value.integer;
            status = neo_buffer_write(a->vm, target, (size_t)operand.integer, &byte, 1);
        } else {
            status = neo_buffer_read(a->vm, target, (size_t)operand.integer, &byte, 1);
        }
        if (status == NEO_OK) { *out = (neo_value){.kind = NEO_INTEGER, .integer = byte}; }
    } else if (strcmp(op, "window-present") == 0) {
        status = neo_operand(a, node, "buffer", depth, &operand);
        if (status != NEO_OK || a->returning) { goto done; }
        if (operand.kind != NEO_TEXT) { status = NEO_WRONG_KIND; goto done; }
        const neo_capability *buffer = NULL;
        status = neo_object_connection(a->vm, a->authority, operand.text, &buffer);
        if (status == NEO_OK) { status = neo_window_present(a->vm, target, buffer); }
    } else {
        neo_window_state state = {0};
        bool poll = strcmp(op, "window-poll") == 0;
        status = poll ? neo_window_poll(a->vm, target, &state) :
            neo_window_get_state(a->vm, target, &state);
        if (status == NEO_OK && poll) {
            *out = (neo_value){.kind = NEO_BOOLEAN, .boolean = state.closed};
        } else if (status == NEO_OK) {
            size_t size = strcmp(op, "window-width") == 0 ? state.width : state.height;
            if (size > INT64_MAX) { status = NEO_LIMIT; }
            else { *out = (neo_value){.kind = NEO_INTEGER, .integer = (int64_t)size}; }
        }
    }
done:
    neo_value_free(a->vm, target_name);
    neo_value_free(a->vm, operand);
    neo_value_free(a->vm, value);
    return status;
}

static neo_status neo_stream_operation(neo_activation *a, neo_object *node,
                                       size_t depth, neo_value *out) {
    neo_value target_name = {0}, value = {0}, offset = {0};
    const neo_capability *target = NULL;
    neo_status status = neo_operand(a, node, "target", depth, &target_name);
    if (status != NEO_OK || a->returning) { goto done; }
    if (target_name.kind != NEO_TEXT) { status = NEO_WRONG_KIND; goto done; }
    status = neo_object_connection(a->vm, a->authority, target_name.text, &target);
    if (status != NEO_OK) { goto done; }
    if (strcmp(node->value.text,"stream-read-text") == 0) {
        status=neo_operand(a,node,"limit",depth,&value);
        if (status != NEO_OK || a->returning) { goto done; }
        if (value.kind != NEO_INTEGER || value.integer<1 || value.integer>NEO_IMAGE_MAX_SOURCE_BYTES) { status=NEO_INVALID; goto done; }
        size_t capacity=(size_t)value.integer;
        if ((capacity+63u)/64u>a->budget-a->report->steps) { status=NEO_LIMIT; goto done; }
        char *text=neo_alloc(a->vm,capacity+1u);
        if (text == NULL) { status=NEO_OUT_OF_MEMORY; goto done; }
        neo_io_result result={0};
        status=neo_stream_read(a->vm,target,(uint8_t *)text,capacity,&result);
        if (status == NEO_OK && result.state == NEO_IO_WOULD_BLOCK) { status=NEO_BUSY; }
        if (status == NEO_OK && (memchr(text,'\0',result.count) != NULL || !neo_text_document_valid(text))) { status=NEO_INVALID; }
        if (status == NEO_OK) { a->report->steps+=(result.count+63u)/64u; *out=(neo_value){.kind=NEO_TEXT,.text=text}; }
        else { neo_free(a->vm,text); }
        goto done;
    }
    bool writing = strcmp(node->value.text, "stream-write") == 0;
    uint8_t byte = 0;
    const uint8_t *bytes = &byte;
    size_t count = 1;
    if (writing) {
        status = neo_operand(a, node, "value", depth, &value);
        if (status != NEO_OK || a->returning) { goto done; }
        if (value.kind == NEO_INTEGER) {
            if (value.integer < 0 || value.integer > 255) { status = NEO_LIMIT; goto done; }
            byte = (uint8_t)value.integer;
        } else if (value.kind == NEO_TEXT) {
            bytes = (const uint8_t *)value.text;
            count = strlen(value.text);
        } else { status = NEO_WRONG_KIND; goto done; }
        neo_object *start = neo_child_named(a->vm, node->id, "offset");
        if (start != NULL) {
            status = neo_evaluate(a, start, depth + 1, &offset);
            if (status != NEO_OK || a->returning) { goto done; }
            if (offset.kind != NEO_INTEGER) { status = NEO_WRONG_KIND; goto done; }
            if (offset.integer < 0 || (uint64_t)offset.integer > count) { status = NEO_LIMIT; goto done; }
            bytes += (size_t)offset.integer;
            count -= (size_t)offset.integer;
        }
    }
    /* Allocate any possible text result before consuming input or writing. */
    status = neo_value_copy(a->vm, (neo_value){.kind = NEO_TEXT, .text = "would-block"}, out);
    if (status != NEO_OK) { goto done; }
    neo_io_result result;
    status = writing ? neo_stream_write(a->vm, target, bytes, count, &result) :
        neo_stream_read(a->vm, target, &byte, 1, &result);
    if (status == NEO_OK && result.state == NEO_IO_TRANSFERRED) {
        neo_value_free(a->vm, *out);
        *out = (neo_value){.kind = NEO_INTEGER, .integer = writing ? (int64_t)result.count : byte};
    } else if (status == NEO_OK && result.state == NEO_IO_EOF) {
        memcpy((char *)out->text, "eof", 4);
    }
done:
    neo_value_free(a->vm, target_name);
    neo_value_free(a->vm, value);
    neo_value_free(a->vm, offset);
    return status;
}

static neo_status neo_array_operation(neo_activation *a, neo_object *node,
                                       size_t depth, neo_value *out) {
    const char *op = node->value.text;
    bool size = strcmp(op, "array-size") == 0;
    bool writing = strcmp(op, "array-set") == 0;
    neo_value slot = {0}, index = {0}, value = {0};
    neo_status status;
    if (strcmp(op, "array-new") == 0) {
        status = neo_operand(a, node, "size", depth, &index);
        if (status != NEO_OK || a->returning) { goto done; }
        status = neo_operand(a, node, "value", depth, &value);
        if (status != NEO_OK || a->returning) { goto done; }
        if (index.kind != NEO_INTEGER || value.kind != NEO_INTEGER) {
            status = NEO_WRONG_KIND; goto done;
        }
        if (index.integer < 0 || (uint64_t)index.integer > SIZE_MAX / sizeof(int64_t)) {
            status = NEO_LIMIT; goto done;
        }
        size_t count = (size_t)index.integer;
        if (count > a->budget - a->report->steps) { status = NEO_LIMIT; goto done; }
        int64_t *items = count == 0 ? NULL : neo_alloc(a->vm, count * sizeof(*items));
        if (count != 0 && items == NULL) { status = NEO_OUT_OF_MEMORY; goto done; }
        for (size_t i = 0; i < count; ++i) { items[i] = value.integer; }
        a->report->steps += count;
        *out = (neo_value){.kind = NEO_INTEGERS, .integers = items, .count = count};
        goto done;
    }
    status = neo_operand(a, node, "slot", depth, &slot);
    if (status != NEO_OK || a->returning) { goto done; }
    if (slot.kind != NEO_TEXT) { status = NEO_WRONG_KIND; goto done; }
    if (!size) {
        status = neo_operand(a, node, "index", depth, &index);
        if (status != NEO_OK || a->returning) { goto done; }
        if (index.kind != NEO_INTEGER) { status = NEO_WRONG_KIND; goto done; }
        if (index.integer < 0 || (uint64_t)index.integer > SIZE_MAX) { status = NEO_INVALID; goto done; }
    }
    if (writing) {
        if ((a->authority->rights & NEO_WRITE) == 0 || strcmp(slot.text, "handlers") == 0) {
            status = NEO_DENIED; goto done;
        }
        status = neo_operand(a, node, "value", depth, &value);
        if (status != NEO_OK || a->returning) { goto done; }
        if (value.kind != NEO_INTEGER) { status = NEO_WRONG_KIND; goto done; }
    }
    const neo_capability *target = NULL;
    status = neo_object_child(a->vm, a->authority, slot.text, &target);
    if (status != NEO_OK) { goto done; }
    if (writing) {
        neo_object *field = neo_child_named(a->vm, a->receiver->id, slot.text);
        if (neo_inside(a->vm, a->body, field->id)) { status = NEO_DENIED; goto done; }
        status = neo_array_set(a->vm, target, (size_t)index.integer, value.integer);
        if (status == NEO_OK) { *out = value; }
    } else if (size) {
        size_t count = 0;
        status = neo_array_size(a->vm, target, &count);
        if (status == NEO_OK && count > INT64_MAX) { status = NEO_OVERFLOW; }
        if (status == NEO_OK) { *out = (neo_value){.kind = NEO_INTEGER, .integer = (int64_t)count}; }
    } else {
        int64_t number = 0;
        status = neo_array_get(a->vm, target, (size_t)index.integer, &number);
        if (status == NEO_OK) { *out = (neo_value){.kind = NEO_INTEGER, .integer = number}; }
    }
done:
    neo_value_free(a->vm, slot);
    neo_value_free(a->vm, index);
    neo_value_free(a->vm, value);
    return status;
}

neo_status neo_invoke_behavior(neo_activation *a, const neo_capability *authority,
                                const char *selector, const neo_message *message,
                                size_t depth, neo_value *out) {
    neo_object *receiver = NULL;
    neo_status status = neo_resolve(a->vm, authority, NEO_ACT | NEO_READ, &receiver);
    const neo_context *context = NULL;
    if (status == NEO_OK) { status = neo_context_create(a->vm, authority, &context); }
    if (status != NEO_OK) { return status; }
    neo_object *handlers = neo_child_named(a->vm, receiver->id, "handlers");
    neo_object *handler = handlers == NULL ? NULL : neo_child_named(a->vm, handlers->id, selector);
    neo_object *body = handler == NULL ? NULL : neo_child_named(a->vm, handler->id, "body");
    if (body == NULL) {
        /* No instruction exists for a missing handler: locate its receiver. */
        a->report->receiver = receiver->id;
        a->report->operation = receiver->id;
        return NEO_UNAVAILABLE;
    }
    neo_activation nested = *a;
    nested.parent = a;
    nested.receiver = receiver;
    nested.authority = authority;
    nested.context = context;
    nested.body = body;
    nested.message = message;
    nested.returning = false;
    nested.return_value = (neo_value){0};
    status = neo_evaluate(&nested, body, depth + 1, out);
    if (nested.returning) {
        neo_value_free(a->vm, *out);
        *out = nested.return_value;
    }
    return status;
}

static neo_status neo_network_operation(neo_activation *a, neo_object *node,
                                         size_t depth, neo_value *out) {
    neo_value target = {0}, name = {0};
    const neo_capability *endpoint = NULL, *stream = NULL;
    const char *op = node->value.text;
    neo_status status = neo_operand(a, node, "target", depth, &target);
    if (status != NEO_OK || a->returning) { goto done; }
    if (target.kind != NEO_TEXT) { status = NEO_WRONG_KIND; goto done; }
    status = neo_object_connection(a->vm, a->authority, target.text, &endpoint);
    if (status != NEO_OK) { goto done; }
    if (strcmp(op, "network-open") == 0) { status = neo_network_endpoint_open(a->vm, endpoint); }
    else if (strcmp(op, "network-close") == 0) { status = neo_network_endpoint_close(a->vm, endpoint); }
    else if (strcmp(op, "network-ready") == 0) {
        bool ready = false;
        status = neo_network_endpoint_ready(a->vm, endpoint, &ready);
        *out = (neo_value){.kind=NEO_BOOLEAN,.boolean=ready};
    } else if (strcmp(op, "network-port") == 0) {
        uint16_t port = 0;
        status = neo_network_endpoint_port(a->vm, endpoint, &port);
        *out = (neo_value){.kind=NEO_INTEGER,.integer=port};
    } else {
        status = neo_operand(a, node, "name", depth, &name);
        if (status != NEO_OK || a->returning) { goto done; }
        if (name.kind != NEO_TEXT) { status = NEO_WRONG_KIND; goto done; }
        status = neo_graph_mutable(a, a->authority, false, false);
        if (status == NEO_OK) {
            status = neo_network_endpoint_stream(a->vm, endpoint, a->authority, name.text, &stream);
        }
        *out = (neo_value){.kind=NEO_BOOLEAN,.boolean=stream != NULL};
    }
done:
    neo_value_free(a->vm, target); neo_value_free(a->vm, name);
    return status;
}

static neo_status neo_run_operation(neo_activation *a, neo_object *node,
                                    size_t depth, neo_value *out) {
    const char *op = node->value.text;
    if (!neo_primitive_known(op)) { return NEO_UNSUPPORTED; }

    if (neo_graph_primitive_known(op)) { return neo_graph_operation(a, node, depth, out); }
    if (strncmp(op, "network-", 8) == 0) { return neo_network_operation(a, node, depth, out); }
    if (neo_message_primitive_known(op)) { return neo_message_operation(a, node, depth, out); }
    if (strcmp(op, "attempt") == 0) {
        size_t outer_budget=a->budget;
        bool bounded=false;
        if (neo_child_named(a->vm,node->id,"budget") != NULL) {
            neo_value limit={0};
            neo_status checked=neo_operand(a,node,"budget",depth,&limit);
            if (checked == NEO_OK && (limit.kind != NEO_INTEGER || limit.integer <= 0)) { checked=NEO_INVALID; }
            if (checked == NEO_OK && !a->returning) {
                uint64_t available=(uint64_t)(outer_budget-a->report->steps);
                if ((uint64_t)limit.integer < available) { a->budget=a->report->steps+(size_t)limit.integer; bounded=true; }
            }
            neo_value_free(a->vm,limit);
            if (checked != NEO_OK || a->returning) { return checked; }
        }
        neo_status status = neo_operand(a, node, "body", depth, out);
        a->budget=outer_budget;
        if (status == NEO_OK || (status == NEO_LIMIT && !bounded) || status == NEO_OUT_OF_MEMORY || a->returning) { return status; }
        neo_value_free(a->vm, *out);
        *out = (neo_value){0};
        neo_status previous_status = a->caught_status;
        neo_object_id previous_operation = a->caught_operation;
        neo_object_id previous_receiver = a->caught_receiver;
        a->caught_status = status;
        a->caught_operation = a->report->operation;
        a->caught_receiver = a->report->receiver;
        a->report->operation = 0;
        status = neo_operand(a, node, "recover", depth, out);
        a->caught_status = previous_status;
        a->caught_operation = previous_operation;
        a->caught_receiver = previous_receiver;
        if (status == NEO_OK) { a->report->receiver = a->receiver->id; }
        return status;
    }
    if (strcmp(op, "rethrow") == 0) {
        if (a->caught_status == NEO_OK) { return NEO_BAD_STATE; }
        a->report->operation = a->caught_operation;
        a->report->receiver = a->caught_receiver;
        return a->caught_status;
    }
    if (strcmp(op, "failure") == 0) {
        if (a->caught_status == NEO_OK) { return NEO_BAD_STATE; }
        neo_value field = {0};
        neo_status status = neo_operand(a, node, "field", depth, &field);
        if (status == NEO_OK && !a->returning) {
            if (field.kind != NEO_TEXT) { status = NEO_WRONG_KIND; }
            else if (strcmp(field.text, "receiver-reference") == 0 || strcmp(field.text, "operation-reference") == 0) {
                neo_value scope = {0};
                status = neo_operand(a, node, "target", depth, &scope);
                if (status == NEO_OK && !a->returning) {
                    neo_object *container = NULL;
                    if (scope.kind != NEO_REFERENCE) { status = NEO_WRONG_KIND; }
                    else { status = neo_resolve(a->vm, scope.reference, NEO_READ, &container); }
                    neo_object_id id = strcmp(field.text, "receiver-reference") == 0 ? a->caught_receiver : a->caught_operation;
                    neo_object *location = neo_lookup(a->vm, id);
                    if (status == NEO_OK && location == NULL) { status = NEO_UNAVAILABLE; }
                    if (status == NEO_OK && !neo_inside(a->vm, location, container->id)) { status = NEO_DENIED; }
                    const neo_capability *reference = NULL;
                    if (status == NEO_OK) {
                        status = neo_issue(a->vm, id, scope.reference->rights & (NEO_READ | NEO_DELEGATE), &reference);
                    }
                    if (status == NEO_OK) { *out = (neo_value){.kind=NEO_REFERENCE,.reference=reference}; }
                }
                neo_value_free(a->vm, scope);
            }
            else if (strcmp(field.text, "status") == 0) {
                status = neo_value_copy(a->vm, (neo_value){.kind=NEO_TEXT,.text=neo_status_name(a->caught_status)}, out);
            } else {
                neo_object_id id = 0;
                if (strcmp(field.text, "operation") == 0) { id = a->caught_operation; }
                else if (strcmp(field.text, "receiver") == 0) { id = a->caught_receiver; }
                else { status = NEO_INVALID; }
                if (status == NEO_OK && id > INT64_MAX) { status = NEO_OVERFLOW; }
                if (status == NEO_OK) { *out = (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)id}; }
            }
        }
        neo_value_free(a->vm, field);
        return status;
    }
    if (strcmp(op, "platform-info") == 0) {
        neo_value field = {0};
        neo_status status = neo_operand(a, node, "field", depth, &field);
        if (status == NEO_OK && !a->returning) {
            neo_platform_info info;
            status = field.kind == NEO_TEXT ? neo_vm_platform_info(a->vm, &info) : NEO_WRONG_KIND;
            if (status == NEO_OK) {
                const char *text = NULL;
                unsigned service = 0;
                if (strcmp(field.text, "environment") == 0) { text = neo_environment_name(info.environment); }
                else if (strcmp(field.text, "virtualization") == 0) { text = neo_virtualization_name(info.virtualization); }
                else if (strcmp(field.text, "architecture") == 0) { text = info.architecture; }
                else if (strcmp(field.text, "host-os") == 0) { text = info.host_os; }
                else if (strcmp(field.text, "backend") == 0) { text = info.backend; }
                else if (strcmp(field.text, "byte-streams") == 0) { service = NEO_PLATFORM_BYTE_STREAMS; }
                else if (strcmp(field.text, "host-files") == 0) { service = NEO_PLATFORM_HOST_FILES; }
                else if (strcmp(field.text, "pixel-windows") == 0) { service = NEO_PLATFORM_PIXEL_WINDOWS; }
                else if (strcmp(field.text, "wait") == 0) { service = NEO_PLATFORM_WAIT; }
                else if (strcmp(field.text, "input-events") == 0) { service = NEO_PLATFORM_INPUT_EVENTS; }
                else { status = NEO_INVALID; }
                if (text != NULL) {
                    status = neo_value_copy(a->vm, (neo_value){.kind = NEO_TEXT, .text = text}, out);
                } else if (status == NEO_OK) {
                    *out = (neo_value){.kind = NEO_BOOLEAN, .boolean = (info.services & service) != 0};
                }
            }
        }
        neo_value_free(a->vm, field);
        return status;
    }
    if (strncmp(op, "array-", 6) == 0) { return neo_array_operation(a, node, depth, out); }
    if (strncmp(op, "stream-", 7) == 0) {
        return neo_stream_operation(a, node, depth, out);
    }
    if (strncmp(op,"surface-",8) == 0 || strcmp(op,"buffer-create") == 0 || strcmp(op,"buffer-copy") == 0 || strcmp(op,"buffer-scale-copy") == 0) {
        return neo_surface_operation(a,node,depth,out);
    }
    if (strncmp(op, "buffer-", 7) == 0 || strncmp(op, "window-", 7) == 0) {
        return neo_display_operation(a, node, depth, out);
    }
    neo_vm *vm = a->vm;
    neo_value left = {0}, right = {0};
    neo_status status = NEO_OK;
    if (strcmp(op, "call") == 0 || strcmp(op, "invoke") == 0) {
        bool cross_receiver = strcmp(op, "invoke") == 0;
        neo_value target = {0};
        if (cross_receiver) { status = neo_operand(a, node, "target", depth, &target); }
        if (status == NEO_OK && !a->returning) { status = neo_operand(a, node, "selector", depth, &left); }
        if (status == NEO_OK && !a->returning) {
            if (left.kind != NEO_TEXT || (cross_receiver && target.kind != NEO_REFERENCE)) { status = NEO_WRONG_KIND; }
            else { status = neo_invoke_behavior(a, cross_receiver ? target.reference : a->authority,
                                               left.text, cross_receiver ? NULL : a->message, depth, out); }
        }
        neo_value_free(vm, target);
        neo_value_free(vm, left);
        return status;
    }
    if (strcmp(op, "do") == 0) {
        for (neo_object *child = neo_child_after(vm, node->id, 0); child != NULL;
             child = neo_child_after(vm, node->id, child->order)) {
            neo_value_free(vm, *out);
            *out = (neo_value){0};
            status = neo_evaluate(a, child, depth + 1, out);
            if (status != NEO_OK || a->returning) { return status; }
        }
        return NEO_OK;
    }
    if (strcmp(op, "if") == 0 || strcmp(op, "while") == 0) {
        for (;;) {
            status = neo_operand(a, node, "condition", depth, &left);
            if (status != NEO_OK || a->returning) { break; }
            if (left.kind != NEO_BOOLEAN) { status = NEO_WRONG_KIND; break; }
            bool condition = left.boolean;
            neo_value_free(vm, left);
            left = (neo_value){0};
            if (strcmp(op, "if") == 0) {
                return neo_operand(a, node, condition ? "then" : "else", depth, out);
            }
            if (!condition) { return NEO_OK; }
            status = neo_operand(a, node, "body", depth, &left);
            if (status != NEO_OK || a->returning) { break; }
            neo_value_free(vm, left);
            left = (neo_value){0};
        }
    } else if (strcmp(op, "return") == 0) {
        status = neo_operand(a, node, "value", depth, out);
        if (status == NEO_OK && !a->returning) {
            a->returning = true;
            a->return_value = *out;
            *out = (neo_value){0};
        }
        return status;
    } else if (strcmp(op, "fail") == 0) {
        return NEO_RAISED;
    } else if (strcmp(op, "read") == 0 || strcmp(op, "write") == 0) {
        status = neo_operand(a, node, "slot", depth, &left);
        if (status == NEO_OK && !a->returning) {
            if (left.kind != NEO_TEXT) {
                status = NEO_WRONG_KIND;
            } else {
                neo_object *slot = neo_child_named(vm, a->receiver->id, left.text);
                if (slot == NULL) {
                    status = NEO_UNAVAILABLE;
                } else if (strcmp(op, "read") == 0) {
                    status = neo_value_copy(vm, slot->value, out);
                } else if ((a->authority->rights & NEO_WRITE) == 0) {
                    status = NEO_DENIED;
                } else if (slot->ether || slot->message != NULL ||
                           neo_inside(vm, a->body, slot->id) ||
                           strcmp(slot->name, "handlers") == 0) {
                    status = NEO_DENIED;
                } else {
                    neo_object_id slot_id = slot->id;
                    status = neo_operand(a, node, "value", depth, &right);
                    /* Structural operands can delete the original slot. Keep its
                     * identity, never a node pointer or a fresh name lookup. */
                    slot = neo_lookup(vm, slot_id);
                    if (status == NEO_OK && !a->returning && slot == NULL) { status = NEO_UNAVAILABLE; }
                    if (status == NEO_OK && !a->returning) {
                        neo_value replacement;
                        status = neo_value_copy(vm, right, &replacement);
                        if (status == NEO_OK) {
                            neo_value_free(vm, slot->value);
                            slot->value = replacement;
                            *out = right;
                            right = (neo_value){0};
                        }
                    }
                }
            }
        }
    } else if (strcmp(op, "message") == 0) {
        status = neo_operand(a, node, "path", depth, &left);
        if (status == NEO_OK && !a->returning) {
            if (left.kind != NEO_TEXT) { status = NEO_WRONG_KIND; }
            else if (a->message == NULL) { status = NEO_UNAVAILABLE; }
            else {
                neo_value borrowed;
                status = neo_message_read(vm, a->context, a->message, left.text, &borrowed);
                if (status == NEO_OK) { status = neo_value_copy(vm, borrowed, out); }
            }
        }
    } else if (strcmp(op, "send") == 0) {
        status = neo_operand(a, node, "target", depth, &left);
        if (status == NEO_OK && !a->returning) {
            status = neo_operand(a, node, "value", depth, &right);
        }
        if (status == NEO_OK && !a->returning) {
            const neo_capability *target = NULL, *ether = NULL;
            if (left.kind != NEO_TEXT) { status = NEO_WRONG_KIND; }
            else { status = neo_object_connection(vm, a->authority, left.text, &target); }
            if (status == NEO_OK) {
                status = neo_object_connection(vm, a->authority, "ether", &ether);
            }
            const neo_message *message = NULL;
            if (status == NEO_OK) {
                status = neo_message_create(vm, a->context, ether, target, right, &message);
            }
            if (status == NEO_OK) { status = neo_message_submit(vm, a->context, message); }
        }
    } else if (strcmp(op, "not") == 0) {
        status = neo_operand(a, node, "value", depth, &left);
        if (status == NEO_OK && !a->returning) {
            if (left.kind != NEO_BOOLEAN) { status = NEO_WRONG_KIND; }
            else { *out = (neo_value){.kind = NEO_BOOLEAN, .boolean = !left.boolean}; }
        }
    } else {
        status = neo_operand(a, node, "left", depth, &left);
        bool short_circuit = false;
        if (status == NEO_OK && !a->returning &&
            (strcmp(op, "and") == 0 || strcmp(op, "or") == 0)) {
            if (left.kind != NEO_BOOLEAN) { status = NEO_WRONG_KIND; }
            else {
                short_circuit = strcmp(op, "and") == 0 ? !left.boolean : left.boolean;
                if (short_circuit) { *out = left; }
            }
        }
        if (status == NEO_OK && !a->returning && !short_circuit) {
            status = neo_operand(a, node, "right", depth, &right);
            if (status == NEO_OK && !a->returning) {
                if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
                    if (left.kind == NEO_REFERENCE || right.kind == NEO_REFERENCE) {
                        status = NEO_WRONG_KIND;
                    } else {
                    bool equal = neo_equal(left, right);
                    *out = (neo_value){.kind = NEO_BOOLEAN,
                        .boolean = strcmp(op, "==") == 0 ? equal : !equal};
                    }
                } else if (strcmp(op, "and") == 0 || strcmp(op, "or") == 0) {
                    if (right.kind != NEO_BOOLEAN) { status = NEO_WRONG_KIND; }
                    else { *out = right; }
                } else if (left.kind != NEO_INTEGER || right.kind != NEO_INTEGER) {
                    status = NEO_WRONG_KIND;
                } else {
                    status = neo_math(op, left.integer, right.integer, out);
                }
            }
        }
    }
    neo_value_free(vm, left);
    neo_value_free(vm, right);
    return status;
}

static neo_status neo_evaluate(neo_activation *a, neo_object *node,
                               size_t depth, neo_value *out) {
    *out = (neo_value){0};
    if (depth > NEO_EVAL_DEPTH || a->report->steps == a->budget) {
        a->report->operation = node->id;
        a->report->receiver = a->receiver->id;
        return NEO_LIMIT;
    }
    ++a->report->steps;
    neo_status status;
    if (node->value.kind != NEO_PRIMITIVE) {
        neo_object *child = node->value.kind == NEO_OBJECT ? neo_child_after(a->vm, node->id, 0) : NULL;
        if (node->value.kind == NEO_OBJECT && child != NULL &&
            child->value.kind == NEO_PRIMITIVE &&
            neo_child_after(a->vm, node->id, child->order) == NULL) {
            /* Named operands/body containers forward to their operation. */
            status = neo_evaluate(a, child, depth + 1, out);
        } else {
            status = neo_value_copy(a->vm, node->value, out);
        }
    } else {
        status = neo_run_operation(a, node, depth, out);
    }
    if (status != NEO_OK && a->report->operation == 0) {
        a->report->operation = node->id;
        a->report->receiver = a->receiver->id;
    }
    return status;
}

neo_status neo_behavior_run(neo_vm *vm, const neo_capability *actor,
                            const char *selector, const neo_message *message,
                            size_t budget, neo_execution *out_report) {
    if (out_report == NULL) { return NEO_INVALID; }
    *out_report = (neo_execution){0};
    if (selector == NULL || budget == 0) {
        out_report->status = NEO_INVALID;
        return NEO_INVALID;
    }
    neo_object *receiver = NULL;
    neo_status status = neo_resolve(vm, actor, NEO_ACT | NEO_READ, &receiver);
    if (status != NEO_OK) { out_report->status = status; return status; }
    out_report->receiver = receiver->id;
    const neo_context *context = NULL;
    status = neo_context_create(vm, actor, &context);
    neo_object *handlers = neo_child_named(vm, receiver->id, "handlers");
    neo_object *handler = handlers == NULL ? NULL : neo_child_named(vm, handlers->id, selector);
    neo_object *body = handler == NULL ? NULL : neo_child_named(vm, handler->id, "body");
    if (status == NEO_OK && body == NULL) { status = NEO_UNAVAILABLE; }
    if (status == NEO_OK && message != NULL) {
        neo_message_info info;
        status = neo_message_inspect(vm, context, message, &info);
        if (status == NEO_OK && (info.receiver != receiver->id || info.state != NEO_MESSAGE_PROCESSING)) {
            status = NEO_BAD_STATE;
        }
    }
    if (status == NEO_OK) {
        neo_activation activation = {.vm = vm, .authority = actor, .receiver = receiver,
            .body = body, .context = context, .message = message, .report = out_report, .budget = budget,
            .submission_cutoff = vm->next_submission};
        ++vm->active_invocations;
        status = neo_evaluate(&activation, body, 1, &out_report->result);
        --vm->active_invocations;
        if (activation.returning) {
            neo_value_free(vm, out_report->result);
            out_report->result = activation.return_value;
        }
    }
    if (status != NEO_OK) {
        neo_value_free(vm, out_report->result);
        out_report->result = (neo_value){0};
    }
    out_report->status = status;
    return status;
}

void neo_execution_release(neo_vm *vm, neo_execution *report) {
    if (vm != NULL && report != NULL) {
        neo_value_free(vm, report->result);
        *report = (neo_execution){0};
    }
}
