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
    bool returning;
    neo_value return_value;
} neo_activation;

neo_status neo_operand(neo_activation *activation, neo_object *operation,
                       const char *name, size_t depth, neo_value *out);
bool neo_graph_primitive_known(const char *name);
neo_status neo_graph_operation(neo_activation *activation, neo_object *operation,
                              size_t depth, neo_value *out);
#endif
