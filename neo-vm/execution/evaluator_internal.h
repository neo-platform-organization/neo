#ifndef NEO_EVALUATOR_INTERNAL_H
#define NEO_EVALUATOR_INTERNAL_H
#include "internal.h"
#include "execution/execution.h"

typedef struct neo_activation {
    struct neo_activation *parent;
    neo_vm *vm;
    const neo_capability *authority;
    neo_object *receiver;
    neo_object *body;
    const neo_context *context;
    const neo_message *message;
    neo_execution *report;
    size_t budget;
    uint64_t submission_cutoff;
    neo_status caught_status;
    neo_object_id caught_operation;
    neo_object_id caught_receiver;
    bool returning;
    neo_value return_value;
} neo_activation;

neo_status neo_operand(neo_activation *activation, neo_object *operation,
                       const char *name, size_t depth, neo_value *out);
neo_status neo_graph_mutable(neo_activation *a, const neo_capability *cap,
                             bool destructive, bool payload_write);
bool neo_graph_primitive_known(const char *name);
neo_status neo_graph_operation(neo_activation *activation, neo_object *operation,
                              size_t depth, neo_value *out);
neo_status neo_text_value_operation(neo_activation *a, const char *op, neo_value *v, neo_value *out);
bool neo_text_document_valid(const char *text);
bool neo_message_primitive_known(const char *name);
neo_status neo_message_operation(neo_activation *a, neo_object *node, size_t depth, neo_value *out);
neo_status neo_invoke_behavior(neo_activation *a, const neo_capability *authority,
                                const char *selector, const neo_message *message,
                                size_t depth, neo_value *out);
#endif
