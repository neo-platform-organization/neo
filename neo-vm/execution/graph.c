#include "execution/evaluator_internal.h"
#include "packages/packages.h"
#include "replication/replication.h"
#include "image/image.h"

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
    {"network-finish", {"target"}},
    {"authenticated-peer", {"target"}},
    {"replica-channel", {"target", "destination", "name"}},
    {"replica-prepare", {"target", "value"}},
    {"replica-send", {"target"}},
    {"replica-receive", {"target"}},
    {"replica-ended", {"target"}},
    {"replica-import", {"target", "destination", "name"}},
    {"package-validate", {"target"}},
    {"package-install", {"target", "value"}},
    {"package-update", {"target", "value"}},
    {"package-remove", {"target", "name"}},
    {"package-resolve", {"target", "name", "value", "rights"}},
    {"package-export", {"target", "name"}},
    {"self", {NULL}},
    {"execution-budget", {NULL}},
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
    {"text-bytes", {"value"}},
    {"text-concat", {"left", "right"}},
    {"text-size", {"value"}},
    {"text-part", {"value", "start", "count"}},
    {"text-edit", {"value", "start", "count", "insert"}},
    {"text-offset", {"value", "start", "direction"}},
    {"text-line", {"value", "index"}},
    {"text-lines", {"value"}},
    {"text-position", {"value", "index", "field"}},
    {"text-index", {"value", "line", "column"}},
    {"text-find", {"value", "needle", "start"}},
    {"text-from-bytes", {"value"}},
    {"source-text", {"target"}},
    {"source-check", {"value"}},
    {"source-apply", {"target", "value", "expected"}},
    {"object-create", {"target", "name", "value"}},
    {"object-write", {"target", "value"}},
    {"object-copy", {"target", "destination", "name"}},
    {"object-move", {"target", "destination", "name"}},
    {"object-delete", {"target"}},
    {"disconnect", {"target", "name"}},
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
neo_status neo_graph_mutable(neo_activation *a, const neo_capability *cap,
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
    if (strcmp(op,"execution-budget") == 0) { *out=(neo_value){.kind=NEO_INTEGER,.integer=(int64_t)(a->budget-a->report->steps)}; goto done; }
    if (strcmp(op, "network-finish") == 0) {
        bool complete = false;
        status = v[0].kind == NEO_REFERENCE ? neo_network_finish(a->vm, v[0].reference, &complete) : NEO_WRONG_KIND;
        if (status == NEO_OK) { *out = (neo_value){.kind=NEO_BOOLEAN,.boolean=complete}; }
        goto done;
    }
    if (strcmp(op, "authenticated-peer") == 0) {
        char peer[65];
        status = v[0].kind == NEO_REFERENCE ? neo_network_authenticated_peer(a->vm, v[0].reference, peer) : NEO_WRONG_KIND;
        if (status == NEO_OK) { status = neo_value_copy(a->vm, (neo_value){.kind=NEO_TEXT,.text=peer}, out); }
        goto done;
    }
    if (strncmp(op, "replica-", 8) == 0) {
        if (v[0].kind != NEO_REFERENCE) { status = NEO_WRONG_KIND; goto done; }
        if (strcmp(op, "replica-channel") == 0 || strcmp(op, "replica-import") == 0) {
            if (v[1].kind != NEO_REFERENCE || v[2].kind != NEO_TEXT) { status = NEO_WRONG_KIND; goto done; }
            status = neo_graph_mutable(a, v[1].reference, false, false);
            if (status == NEO_OK) {
                status = strcmp(op, "replica-channel") == 0 ?
                    neo_replica_channel_create(a->vm, v[0].reference, v[1].reference, v[2].text, &result) :
                    neo_replica_import(a->vm, v[0].reference, v[1].reference, v[2].text, &result);
            }
        } else if (strcmp(op, "replica-prepare") == 0) {
            status = v[1].kind == NEO_REFERENCE ? neo_replica_prepare(a->vm, v[0].reference, v[1].reference) : NEO_WRONG_KIND;
        } else {
            bool complete = false;
            if (strcmp(op, "replica-ended") == 0) { status = neo_replica_ended(a->vm, v[0].reference, &complete); }
            else {
                status = strcmp(op, "replica-send") == 0 ? neo_replica_send(a->vm, v[0].reference, &complete) :
                    neo_replica_receive(a->vm, v[0].reference, &complete);
            }
            *out = (neo_value){.kind=NEO_BOOLEAN,.boolean=complete};
        }
        goto done;
    }
    if (strncmp(op, "package-", 8) == 0) {
        if (v[0].kind != NEO_REFERENCE) { status = NEO_WRONG_KIND; goto done; }
        if (strcmp(op, "package-validate") == 0) { status = neo_package_validate(a->vm, v[0].reference); }
        else if (strcmp(op, "package-install") == 0 || strcmp(op, "package-update") == 0) {
            status = neo_graph_mutable(a, v[0].reference, true, false);
            if (status == NEO_OK) { status = v[1].kind == NEO_REFERENCE ?
                neo_package_install(a->vm, v[0].reference, v[1].reference, strcmp(op, "package-update") == 0, &result) : NEO_WRONG_KIND; }
        } else if (v[1].kind != NEO_TEXT) { status = NEO_WRONG_KIND; }
        else if (strcmp(op, "package-remove") == 0) {
            status = neo_graph_mutable(a, v[0].reference, true, false);
            if (status == NEO_OK) { status = neo_package_remove(a->vm, v[0].reference, v[1].text); }
        } else if (strcmp(op, "package-resolve") == 0) {
            unsigned rights = 0;
            status = v[2].kind == NEO_TEXT ? neo_graph_rights(v[3], &rights) : NEO_WRONG_KIND;
            if (status == NEO_OK) { status = neo_package_resolve(a->vm, v[0].reference, v[1].text, v[2].text, rights, &result); }
        } else {
            const neo_capability *artifact = NULL;
            char *text = NULL;
            status = neo_package_export(a->vm, v[0].reference, v[1].text, &artifact);
            if (status == NEO_OK) { status = neo_image_format(a->vm, artifact, &text); }
            if (artifact != NULL) { (void)neo_image_unload(a->vm, artifact); }
            if (status == NEO_OK) { *out = (neo_value){.kind=NEO_TEXT,.text=text}; }
        }
        goto done;
    }
    if (strncmp(op,"source-",7) == 0) {
        if (strcmp(op,"source-check") == 0) {
            const neo_capability *parsed=NULL; neo_diagnostic error={0};
            if (v[0].kind != NEO_TEXT) { status=NEO_WRONG_KIND; goto done; }
            neo_status checked=neo_image_parse(a->vm,v[0].text,&parsed,&error);
            if (parsed != NULL) { (void)neo_image_unload(a->vm,parsed); }
            if (checked == NEO_OUT_OF_MEMORY) { status=checked; goto done; }
            char message[256]="";
            if (checked != NEO_OK) { (void)snprintf(message,sizeof(message),"line%zu column%zu: %s",error.line,error.column,
                error.message[0] != '\0' ? error.message : neo_status_name(checked)); }
            status=neo_value_copy(a->vm,(neo_value){.kind=NEO_TEXT,.text=message},out);
        } else if (v[0].kind != NEO_REFERENCE) { status=NEO_WRONG_KIND; }
        else if (strcmp(op,"source-text") == 0) {
            char *source=NULL; status=neo_image_source(a->vm,v[0].reference,&source);
            if (status == NEO_OK) { *out=(neo_value){.kind=NEO_TEXT,.text=source}; }
        } else {
            status=v[1].kind != NEO_TEXT || v[2].kind != NEO_TEXT ? NEO_WRONG_KIND : neo_graph_mutable(a,v[0].reference,true,true);
            neo_diagnostic error={0};
            if (status == NEO_OK) { status=neo_image_apply_source(a->vm,v[0].reference,v[1].text,v[2].text,&error); }
            if (status == NEO_OK) { *out=(neo_value){.kind=NEO_BOOLEAN,.boolean=true}; }
        }
        goto done;
    }
    if (strncmp(op,"text-",5) == 0 && strcmp(op,"text-bytes") != 0 && strcmp(op,"text-concat") != 0) {
        status=neo_text_value_operation(a,op,v,out); goto done;
    }
    if (strcmp(op, "text-bytes") == 0 || strcmp(op, "text-concat") == 0) {
        if (v[0].kind != NEO_TEXT || (strcmp(op, "text-concat") == 0 && v[1].kind != NEO_TEXT)) { status = NEO_WRONG_KIND; goto done; }
        size_t first = strlen(v[0].text), second = strcmp(op, "text-concat") == 0 ? strlen(v[1].text) : 0;
        if (first > SIZE_MAX - second - 1u || first + second > a->budget - a->report->steps) { status = NEO_LIMIT; goto done; }
        if (strcmp(op, "text-bytes") == 0) {
            if (first > SIZE_MAX / sizeof(int64_t)) { status = NEO_LIMIT; goto done; }
            int64_t *bytes = first == 0 ? NULL : neo_alloc(a->vm, first * sizeof(*bytes));
            if (first != 0 && bytes == NULL) { status = NEO_OUT_OF_MEMORY; goto done; }
            for (size_t i = 0; i < first; ++i) { bytes[i] = (uint8_t)v[0].text[i]; }
            *out = (neo_value){.kind=NEO_INTEGERS,.integers=bytes,.count=first};
        } else {
            char *text = neo_alloc(a->vm, first + second + 1u);
            if (text == NULL) { status = NEO_OUT_OF_MEMORY; goto done; }
            memcpy(text, v[0].text, first); memcpy(text + first, v[1].text, second + 1u);
            *out = (neo_value){.kind=NEO_TEXT,.text=text};
        }
        a->report->steps += first + second;
        goto done;
    }
    if (strcmp(op, "to-text") == 0) {
        char integer[32];
        const char *text = NULL;
        if (v[0].kind == NEO_TEXT || v[0].kind == NEO_PRIMITIVE) { text = v[0].text; }
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
    } else if (strcmp(op, "disconnect") == 0) {
        status = v[1].kind == NEO_TEXT ? neo_graph_mutable(a, target, false, false) : NEO_WRONG_KIND;
        if (status == NEO_OK) { status = neo_object_disconnect(a->vm, target, v[1].text); }
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
