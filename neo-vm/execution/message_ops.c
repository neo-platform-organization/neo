#include "execution/evaluator_internal.h"

#include <string.h>

typedef struct neo_message_form {
    const char *name;
    const char *operands[4];
} neo_message_form;

static const neo_message_form neo_message_forms[] = {
    {"dispatch", {"target"}},
    {"receiver-retry", {"target"}},
    {"receiver-discard", {"target"}},
    {"message-create", {"ether", "receiver", "value"}},
    {"message-link", {"target", "name"}},
    {"message-info", {"target", "field"}},
    {"message-read", {"target", "path"}},
    {"message-write", {"target", "path", "value"}},
    {"message-field", {"target", "path", "name", "value"}},
    {"message-grant", {"target", "actor", "rights"}},
    {"message-submit", {"target"}},
    {"message-accept", {"target"}},
    {"message-finish", {"target"}},
    {"message-release", {"target"}},
    {"message-pending", {"ether"}},
    {"message-begin", {NULL}},
    {"message-active", {NULL}},
    {"message-retry", {NULL}}
};

static const neo_message_form *neo_message_form_find(const char *name) {
    for (size_t i = 0; i < sizeof(neo_message_forms) / sizeof(neo_message_forms[0]); ++i) {
        if (strcmp(name, neo_message_forms[i].name) == 0) { return &neo_message_forms[i]; }
    }
    return NULL;
}
bool neo_message_primitive_known(const char *name) { return neo_message_form_find(name) != NULL; }

static bool neo_message_managed(neo_activation *a, const neo_message *message) {
    for (neo_activation *frame = a; frame != NULL; frame = frame->parent) {
        if (frame->message == message) { return true; }
    }
    return false;
}

static neo_status neo_message_information(neo_activation *a, const neo_message *message,
                                          const char *field, neo_value *out) {
    neo_message_info info;
    neo_status status = neo_message_inspect(a->vm, a->context, message, &info);
    if (status != NEO_OK) { return status; }
    const char *text = NULL;
    uint64_t number = 0;
    if (strcmp(field, "state") == 0) { text = neo_message_state_name(info.state); }
    else if (strcmp(field, "name") == 0) { text = neo_lookup(a->vm, info.object)->name; }
    else if (strcmp(field, "sender") == 0) { number = info.sender; }
    else if (strcmp(field, "receiver") == 0) { number = info.receiver; }
    else if (strcmp(field, "submission") == 0) { number = info.submission; }
    else if (strcmp(field, "receiver-available") == 0 || strcmp(field, "sender-available") == 0) {
        *out = (neo_value){.kind=NEO_BOOLEAN,.boolean=strcmp(field, "receiver-available") == 0 ?
            info.receiver_available : info.sender_available};
        return NEO_OK;
    } else { return NEO_INVALID; }
    if (text != NULL) { return neo_value_copy(a->vm, (neo_value){.kind=NEO_TEXT,.text=text}, out); }
    if (number > INT64_MAX) { return NEO_OVERFLOW; }
    *out = (neo_value){.kind=NEO_INTEGER,.integer=(int64_t)number};
    return NEO_OK;
}

static neo_status neo_dispatch_operation(neo_activation *a, const char *op,
                                          const neo_capability *target, size_t depth, neo_value *out) {
    neo_object *receiver = NULL;
    neo_status status = neo_resolve(a->vm, target, NEO_READ | NEO_ACT, &receiver);
    if (status != NEO_OK) { return status; }
    if (neo_scheduler_contains(a->vm, receiver->id)) { return NEO_BUSY; }
    const neo_context *context = NULL;
    status = neo_context_create(a->vm, target, &context);
    if (status != NEO_OK) { return status; }
    if (strcmp(op, "dispatch") != 0) {
        const neo_message *active = NULL;
        status = neo_message_active(a->vm, context, &active);
        if (status != NEO_OK) { return status; }
        if (neo_message_managed(a, active)) { return NEO_BUSY; }
        return strcmp(op, "receiver-retry") == 0 ? neo_message_retry(a->vm, context) : neo_message_finish(a->vm, context, active);
    }
    if (receiver->active_message != NULL) { return NEO_BUSY; }
    size_t accepted = 0;
    status = neo_message_accept_pending(a->vm, context, a->submission_cutoff, &accepted);
    const neo_message *message = NULL;
    if (status == NEO_OK) { status = neo_message_begin(a->vm, context, &message); }
    if (status == NEO_UNAVAILABLE) {
        *out = (neo_value){.kind=NEO_BOOLEAN,.boolean=false};
        return NEO_OK;
    }
    if (status != NEO_OK) { return status; }
    neo_value selector = {0}, result = {0};
    status = neo_message_read(a->vm, context, message, "", &selector);
    if (status == NEO_OK && selector.kind != NEO_TEXT) { status = NEO_WRONG_KIND; }
    if (status != NEO_OK) {
        a->report->receiver = receiver->id;
        a->report->operation = receiver->id;
    }
    if (status == NEO_OK) { status = neo_invoke_behavior(a, target, selector.text, message, depth, &result); }
    neo_value_free(a->vm, result);
    if (status == NEO_OK) { status = neo_message_finish(a->vm, context, message); }
    if (status == NEO_OK) { *out = (neo_value){.kind=NEO_BOOLEAN,.boolean=true}; }
    return status;
}

neo_status neo_message_operation(neo_activation *a, neo_object *node, size_t depth, neo_value *out) {
    const char *op = node->value.text;
    const neo_message_form *form = neo_message_form_find(op);
    if (form == NULL) { return NEO_UNSUPPORTED; }
    neo_value v[4] = {{0}};
    neo_status status = NEO_OK;
    const neo_message *message = NULL;
    bool reference_result = false;
    for (size_t i = 0; i < 4 && form->operands[i] != NULL; ++i) {
        status = neo_operand(a, node, form->operands[i], depth, &v[i]);
        if (status != NEO_OK || a->returning) { goto done; }
    }
    if (strcmp(op, "dispatch") == 0 || strcmp(op, "receiver-retry") == 0 || strcmp(op, "receiver-discard") == 0) {
        status = v[0].kind == NEO_REFERENCE ? neo_dispatch_operation(a, op, v[0].reference, depth, out) : NEO_WRONG_KIND;
        goto done;
    }
    if ((strcmp(op, "message-begin") == 0 || strcmp(op, "message-retry") == 0 || strcmp(op, "message-finish") == 0) &&
        neo_scheduler_contains(a->vm, a->receiver->id)) { status = NEO_BUSY; goto done; }
    if (strcmp(op, "message-begin") == 0) {
        status = neo_message_begin(a->vm, a->context, &message); reference_result = true;
    } else if (strcmp(op, "message-active") == 0 || strcmp(op, "message-retry") == 0) {
        status = neo_message_active(a->vm, a->context, &message);
        if (status == NEO_OK && strcmp(op, "message-retry") == 0) {
            status = neo_message_managed(a, message) ? NEO_BUSY : neo_message_retry(a->vm, a->context);
        } else { reference_result = true; }
    } else if (strcmp(op, "message-create") == 0) {
        if (v[0].kind != NEO_REFERENCE || v[1].kind != NEO_REFERENCE || !neo_valid_value(v[2])) { status = NEO_WRONG_KIND; }
        else { status = neo_message_create(a->vm, a->context, v[0].reference, v[1].reference, v[2], &message); }
        reference_result = true;
    } else if (strcmp(op, "message-pending") == 0) {
        status = v[0].kind == NEO_REFERENCE ? neo_message_pending(a->vm, a->context, v[0].reference, &message) : NEO_WRONG_KIND;
        reference_result = true;
    } else {
        status = v[0].kind == NEO_REFERENCE ? neo_message_from_reference(a->vm, v[0].reference, &message) : NEO_WRONG_KIND;
        if (status != NEO_OK) { goto done; }
        if (strcmp(op, "message-submit") == 0) { status = neo_message_submit(a->vm, a->context, message); }
        else if (strcmp(op, "message-accept") == 0) { status = neo_message_accept(a->vm, a->context, message); }
        else if (strcmp(op, "message-finish") == 0) {
            status = neo_message_managed(a, message) ? NEO_BUSY : neo_message_finish(a->vm, a->context, message);
        } else if (strcmp(op, "message-release") == 0) {
            status = neo_message_managed(a, message) ? NEO_BUSY : neo_message_release(a->vm, a->context, message);
        } else if (strcmp(op, "message-grant") == 0) {
            if (v[1].kind != NEO_REFERENCE || v[2].kind != NEO_INTEGER) { status = NEO_WRONG_KIND; }
            else if (v[2].integer < 0 || v[2].integer > NEO_MESSAGE_ALL) { status = NEO_INVALID; }
            else { status = neo_message_grant(a->vm, a->context, message, v[1].reference, (unsigned)v[2].integer); }
        } else if (v[1].kind != NEO_TEXT) { status = NEO_WRONG_KIND; }
        else if (strcmp(op, "message-link") == 0) {
            status = neo_message_link(a->vm, a->context, a->authority, v[1].text, message);
            reference_result = true;
        } else if (strcmp(op, "message-info") == 0) { status = neo_message_information(a, message, v[1].text, out); }
        else if (strcmp(op, "message-read") == 0) {
            neo_value borrowed = {0};
            status = neo_message_read(a->vm, a->context, message, v[1].text, &borrowed);
            if (status == NEO_OK) { status = neo_value_copy(a->vm, borrowed, out); }
        } else if (strcmp(op, "message-write") == 0) {
            status = neo_valid_value(v[2]) ? neo_message_write(a->vm, a->context, message, v[1].text, v[2]) : NEO_WRONG_KIND;
        } else if (strcmp(op, "message-field") == 0) {
            status = v[2].kind == NEO_TEXT && neo_valid_value(v[3]) ?
                neo_message_field_create(a->vm, a->context, message, v[1].text, v[2].text, v[3]) : NEO_WRONG_KIND;
        }
    }
done:
    for (size_t i = 0; i < 4; ++i) { neo_value_free(a->vm, v[i]); }
    if (status == NEO_OK && !a->returning && reference_result) {
        const neo_capability *reference = NULL;
        status = neo_message_reference(a->vm, message, &reference);
        if (status == NEO_OK) { *out = (neo_value){.kind=NEO_REFERENCE,.reference=reference}; }
    }
    return status;
}
