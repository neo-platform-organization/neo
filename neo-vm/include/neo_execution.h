#ifndef NEO_EXECUTION_H
#define NEO_EXECUTION_H

#include "neo.h"
#include "neo_message.h"

typedef struct neo_execution {
    neo_status status;
    neo_object_id receiver;
    neo_object_id operation;
    size_t steps;
    neo_value result;
} neo_execution;

/* Execute actor/handlers/selector/body, using only actor's supplied authority
 * and its explicitly delegated connections. No automatic message completion.
 * budget > 0 is a host safety limit (not a language time slice). Result text and arrays
 * are owned: release it before reusing the report or destroying the VM.
 * On failure earlier writes remain and the report locates the failed operation. */
neo_status neo_behavior_run(neo_vm *vm, const neo_capability *actor,
                            const char *selector, const neo_message *message,
                            size_t budget, neo_execution *out_report);
void neo_execution_release(neo_vm *vm, neo_execution *report);

/* Host-controlled sequential scheduler. Registration order is turn order.
 * A registered receiver handles at most one message then its optional 'tick'
 * handler per tick. Pending submissions are accepted at the boundary. */
neo_status neo_scheduler_register(neo_vm *vm, const neo_capability *actor);
neo_status neo_scheduler_unregister(neo_vm *vm, const neo_capability *actor);
neo_status neo_scheduler_tick(neo_vm *vm, size_t budget_per_turn,
                              size_t *out_turns, neo_execution *out_failure);

/* A failure pauses that receiver. No automatic retry/discard. Retry repeats
 * the handler from its beginning and may repeat earlier effects. Discard only
 * completes its failed message; neither choice rolls back mutations. */
neo_status neo_scheduler_recover(neo_vm *vm, const neo_capability *actor, bool retry);

#endif
