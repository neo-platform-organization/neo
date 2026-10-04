#include "execution/evaluator_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Operand order is explicit. All values are evaluated before resolving targets,
 * since a later operand can delete an object referenced by an earlier operand. */
typedef struct neo_graph_form {
    const char *name;
    const char *operands[4];
} neo_graph_form;

static const neo_graph_form neo_graph_forms[] = {
    {"self", {NULL}},
    {"child", {"target", "name"}},
    {"child-at", {"target", "index"}},
    {"child-count", {"target"}},
    {"has-child", {"target", "name"}},
    {"connection", {"target", "name"}},
    {"restrict", {"target", "rights"}},
    {"same", {"left", "right"}},
    {"object-name", {"target"}},
    {"object-kind", {"target"}},
    {"inspect", {"target"}},
    {"to-text", {"value"}},
    {"object-create", {"target", "name", "value"}},
    {"object-write", {"target", "value"}},
    {"object-copy", {"target", "destination", "name"}},
    {"object-move", {"target", "destination", "name"}},
    {"object-delete", {"target"}},
    {"connect", {"target", "name", "value", "rights"}}
};

static const neo_graph_form *neo_graph_form_find(const char *name) {
    for (size_t i = 0; i < sizeof(neo_graph_forms) / sizeof(neo_graph_forms[0]); ++i) {
        if (strcmp(name, neo_graph_forms[i].name) == 0) { return &neo_graph_forms[i]; }
    }
    return NULL;
}

bool neo_graph_primitive_known(const char *name) { return neo_graph_form_find(name) != NULL; }

static neo_status neo_graph_rights(neo_value value, unsigned *out) {
    if (value.kind != NEO_INTEGER) { return NEO_WRONG_KIND; }
    if (value.integer < 0 || value.integer > NEO_ALL) { return NEO_INVALID; }
    *out = (unsigned)value.integer;
    return NEO_OK;
}

static const char *neo_graph_kind_name(neo_kind kind) {
    switch (kind) {
        case NEO_OBJECT: return "object";
        case NEO_INTEGER: return "integer";
        case NEO_BOOLEAN: return "boolean";
        case NEO_TEXT: return "text";
        case NEO_PRIMITIVE: return "primitive";
        case NEO_INTEGERS: return "integers";
        case NEO_REFERENCE: return "reference";
    }
    return "unknown";
}

/* Native activations retain node pointers. Protect all receiver-owned handlers,
 * including suspended local callers, until activations support live code edits. */
static neo_status neo_graph_mutable(neo_activation *a, const neo_capability *cap,
                                    bool destructive, bool payload_write) {
    neo_object *target = NULL;
    neo_status status = neo_resolve(a->vm, cap, 0, &target);
    if (status != NEO_OK) { return status; }
    for (neo_activation *frame = a; frame != NULL; frame = frame->parent) {
        neo_object *handlers = neo_child_named(a->vm, frame->receiver->id, "handlers");
        if ((handlers != NULL && neo_inside(a->vm, target, handlers->id)) ||
            (destructive && neo_inside(a->vm, frame->receiver, target->id)) ||
            (payload_write && neo_inside(a->vm, frame->body, target->id))) { return NEO_DENIED; }
    }
    return NEO_OK;
}

neo_status neo_graph_operation(neo_activation *a, neo_object *node, size_t depth, neo_value *out) {
    const char *op = node->value.text;
    const neo_graph_form *form = neo_graph_form_find(op);
    if (form == NULL) { return NEO_UNSUPPORTED; }
    neo_value v[4] = {{0}};
    neo_status status = NEO_OK;
    const neo_capability *result = NULL;
    for (size_t i = 0; i < 4 && form->operands[i] != NULL; ++i) {
        status = neo_operand(a, node, form->operands[i], depth, &v[i]);
        if (status != NEO_OK || a->returning) { goto done; }
    }
    if (strcmp(op, "self") == 0) { result = a->authority; goto done; }
    if (strcmp(op, "to-text") == 0) {
        char integer[32];
        const char *text = NULL;
        if (v[0].kind == NEO_TEXT) { text = v[0].text; }
        else if (v[0].kind == NEO_BOOLEAN) { text = v[0].boolean ? "true" : "false"; }
        else if (v[0].kind == NEO_INTEGER) {
            (void)snprintf(integer, sizeof(integer), "%" PRId64, v[0].integer); text = integer;
        } else { status = NEO_WRONG_KIND; }
        if (status == NEO_OK) { status = neo_value_copy(a->vm, (neo_value){.kind = NEO_TEXT, .text = text}, out); }
        goto done;
    }
    if (v[0].kind != NEO_REFERENCE) { status = NEO_WRONG_KIND; goto done; }
    const neo_capability *target = v[0].reference;
    if (strcmp(op, "same") == 0) {
        neo_object_id left = 0, right = 0;
        status = v[1].kind == NEO_REFERENCE ? neo_object_identity(a->vm, target, &left) : NEO_WRONG_KIND;
        if (status == NEO_OK) { status = neo_object_identity(a->vm, v[1].reference, &right); }
        if (status == NEO_OK) { *out = (neo_value){.kind = NEO_BOOLEAN, .boolean = left == right}; }
    } else if (strcmp(op, "child") == 0 || strcmp(op, "connection") == 0 || strcmp(op, "has-child") == 0) {
        if (v[1].kind != NEO_TEXT) { status = NEO_WRONG_KIND; goto done; }
        /* A stale/denied parent is an error, not evidence of an absent child. */
        neo_object *parent = NULL;
        status = neo_resolve(a->vm, target, NEO_READ, &parent);
        if (status == NEO_OK) {
            status = strcmp(op, "connection") == 0 ? neo_object_connection(a->vm, target, v[1].text, &result) :
                neo_object_child(a->vm, target, v[1].text, &result);
            if (strcmp(op, "has-child") == 0 && (status == NEO_OK || status == NEO_UNAVAILABLE)) {
                *out = (neo_value){.kind = NEO_BOOLEAN, .boolean = status == NEO_OK};
                status = NEO_OK; result = NULL;
            }
        }
    } else if (strcmp(op, "child-at") == 0) {
        if (v[1].kind != NEO_INTEGER) { status = NEO_WRONG_KIND; }
        else if (v[1].integer < 0 || (uint64_t)v[1].integer > SIZE_MAX) { status = NEO_INVALID; }
        else { status = neo_object_child_at(a->vm, target, (size_t)v[1].integer, &result); }
    } else if (strcmp(op, "child-count") == 0) {
        size_t count = 0;
        status = neo_object_child_count(a->vm, target, &count);
        if (status == NEO_OK && count > INT64_MAX) { status = NEO_OVERFLOW; }
        if (status == NEO_OK) { *out = (neo_value){.kind = NEO_INTEGER, .integer = (int64_t)count}; }
    } else if (strcmp(op, "restrict") == 0) {
        unsigned rights = 0;
        status = neo_graph_rights(v[1], &rights);
        if (status == NEO_OK) { status = neo_capability_restrict(a->vm, target, rights, &result); }
    } else if (strcmp(op, "object-name") == 0) {
        const char *name = NULL;
        status = neo_object_name(a->vm, target, &name);
        if (status == NEO_OK) { status = neo_value_copy(a->vm, (neo_value){.kind = NEO_TEXT, .text = name}, out); }
    } else if (strcmp(op, "object-kind") == 0 || strcmp(op, "inspect") == 0) {
        neo_value value = {0};
        status = neo_object_read(a->vm, target, &value);
        if (status == NEO_OK) {
            if (strcmp(op, "object-kind") == 0) { value = (neo_value){.kind = NEO_TEXT, .text = neo_graph_kind_name(value.kind)}; }
            status = neo_value_copy(a->vm, value, out);
        }
    } else if (strcmp(op, "object-create") == 0) {
        status = v[1].kind != NEO_TEXT || !neo_valid_value(v[2]) ? NEO_WRONG_KIND : neo_graph_mutable(a, target, false, false);
        if (status == NEO_OK) { status = neo_object_create(a->vm, target, v[1].text, v[2], &result); }
    } else if (strcmp(op, "object-write") == 0) {
        status = !neo_valid_value(v[1]) ? NEO_WRONG_KIND : neo_graph_mutable(a, target, false, true);
        if (status == NEO_OK) { status = neo_object_write(a->vm, target, v[1]); }
        if (status == NEO_OK) { *out = v[1]; v[1] = (neo_value){0}; }
    } else if (strcmp(op, "object-copy") == 0 || strcmp(op, "object-move") == 0) {
        if (v[1].kind != NEO_REFERENCE || v[2].kind != NEO_TEXT) { status = NEO_WRONG_KIND; goto done; }
        bool moving = strcmp(op, "object-move") == 0;
        status = neo_graph_mutable(a, v[1].reference, false, false);
        if (status == NEO_OK && moving) { status = neo_graph_mutable(a, target, true, false); }
        if (status == NEO_OK) {
            status = moving ? neo_object_move(a->vm, target, v[1].reference, v[2].text, &result) :
                neo_object_copy(a->vm, target, v[1].reference, v[2].text, &result);
        }
    } else if (strcmp(op, "object-delete") == 0) {
        status = neo_graph_mutable(a, target, true, false);
        if (status == NEO_OK) { status = neo_object_delete(a->vm, target); }
    } else if (strcmp(op, "connect") == 0) {
        unsigned rights = 0;
        if (v[1].kind != NEO_TEXT || v[2].kind != NEO_REFERENCE) { status = NEO_WRONG_KIND; goto done; }
        status = neo_graph_rights(v[3], &rights);
        if (status == NEO_OK) { status = neo_graph_mutable(a, target, false, false); }
        if (status == NEO_OK) { status = neo_object_connect(a->vm, target, v[1].text, v[2].reference, rights); }
    }
done:
    for (size_t i = 0; i < 4; ++i) { neo_value_free(a->vm, v[i]); }
    if (status == NEO_OK && !a->returning && result != NULL) {
        *out = (neo_value){.kind = NEO_REFERENCE, .reference = result};
    }
    return status;
}
