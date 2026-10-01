#include "internal.h"
#include "neo_execution.h"

#include <string.h>

#define NEO_EVAL_DEPTH 128u

typedef struct neo_activation {
    neo_vm *vm;
    const neo_capability *authority;
    neo_object *receiver;
    neo_object *body;
    const neo_context *context;
    const neo_message *message;
    neo_execution *report;
    size_t budget;
    bool returning;
    neo_value return_value;
} neo_activation;

static neo_status neo_evaluate(neo_activation *activation, neo_object *expression,
                               size_t depth, neo_value *out);

static neo_status neo_operand(neo_activation *activation, neo_object *operation,
                              const char *name, size_t depth, neo_value *out) {
    neo_object *child = neo_child_named(activation->vm, operation->id, name);
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
        if (strcmp(operation, "lt") == 0) { out->boolean = a < b; }
        else if (strcmp(operation, "le") == 0) { out->boolean = a <= b; }
        else if (strcmp(operation, "gt") == 0) { out->boolean = a > b; }
        else if (strcmp(operation, "ge") == 0) { out->boolean = a >= b; }
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
        case NEO_PRIMITIVE: return false;
    }
    return false;
}

static neo_status neo_run_operation(neo_activation *a, neo_object *node,
                                    size_t depth, neo_value *out) {
    const char *op = node->value.text;
    static const char *const primitives[] = {
        "do", "if", "while", "return", "fail", "read", "write", "message", "send",
        "not", "and", "or", "eq", "add", "sub", "mul", "div", "rem", "lt", "le", "gt", "ge"
    };
    bool known = false;
    for (size_t i = 0; i < sizeof(primitives) / sizeof(primitives[0]); ++i) {
        if (strcmp(op, primitives[i]) == 0) { known = true; break; }
    }
    if (!known) { return NEO_UNSUPPORTED; }

    neo_vm *vm = a->vm;
    neo_value left = {0}, right = {0};
    neo_status status = NEO_OK;
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
                           neo_inside(vm, a->body, slot->id)) {
                    status = NEO_DENIED;
                } else {
                    status = neo_operand(a, node, "value", depth, &right);
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
                if (strcmp(op, "eq") == 0) {
                    *out = (neo_value){.kind = NEO_BOOLEAN, .boolean = neo_equal(left, right)};
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
        return NEO_LIMIT;
    }
    ++a->report->steps;
    neo_status status;
    if (node->value.kind != NEO_PRIMITIVE) {
        status = neo_value_copy(a->vm, node->value, out);
    } else {
        status = neo_run_operation(a, node, depth, out);
    }
    if (status != NEO_OK && a->report->operation == 0) {
        a->report->operation = node->id;
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
            .body = body, .context = context, .message = message, .report = out_report, .budget = budget};
        status = neo_evaluate(&activation, body, 1, &out_report->result);
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
