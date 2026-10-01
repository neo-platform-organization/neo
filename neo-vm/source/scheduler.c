#include "internal.h"
#include "neo_execution.h"

struct neo_registration {
    neo_object_id actor;
    const neo_capability *authority;
    const neo_context *context;
    bool enabled;
    bool failed;
    bool failed_tick;
    struct neo_registration *next;
};

neo_status neo_scheduler_register(neo_vm *vm, const neo_capability *actor) {
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, actor, NEO_ACT | NEO_READ, &node);
    if (status != NEO_OK) { return status; }
    neo_registration **tail = &vm->registrations;
    while (*tail != NULL) {
        if ((*tail)->actor == node->id) {
            (*tail)->enabled = true;
            (*tail)->authority = actor;
            return NEO_OK;
        }
        tail = &(*tail)->next;
    }
    neo_registration *registration = neo_alloc(vm, sizeof(*registration));
    if (registration == NULL) { return NEO_OUT_OF_MEMORY; }
    status = neo_context_create(vm, actor, &registration->context);
    if (status != NEO_OK) { neo_free(vm, registration); return status; }
    registration->actor = node->id;
    registration->authority = actor;
    registration->enabled = true;
    *tail = registration;
    return NEO_OK;
}

neo_status neo_scheduler_unregister(neo_vm *vm, const neo_capability *actor) {
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, actor, NEO_ACT, &node);
    if (status != NEO_OK) { return status; }
    for (neo_registration *r = vm->registrations; r != NULL; r = r->next) {
        if (r->actor == node->id) { r->enabled = false; return NEO_OK; }
    }
    return NEO_UNAVAILABLE;
}

static neo_status neo_message_turn(neo_vm *vm, neo_registration *r,
                                   size_t budget, neo_execution *report) {
    const neo_message *message = NULL;
    neo_status status = neo_message_begin(vm, r->context, &message);
    if (status != NEO_OK) { return status; }
    neo_value selector;
    status = neo_message_read(vm, r->context, message, "", &selector);
    if (status == NEO_OK && selector.kind != NEO_TEXT) { status = NEO_WRONG_KIND; }
    if (status == NEO_OK) {
        status = neo_behavior_run(vm, r->authority, selector.text, message, budget, report);
    }
    if (status == NEO_OK) { status = neo_message_finish(vm, r->context, message); }
    if (status != NEO_OK) {
        report->status = status;
        report->receiver = r->actor;
        r->failed = true;
        r->failed_tick = false;
    }
    return status;
}

neo_status neo_scheduler_tick(neo_vm *vm, size_t budget_per_turn,
                              size_t *out_turns, neo_execution *out_failure) {
    if (out_turns == NULL || out_failure == NULL) { return NEO_INVALID; }
    *out_turns = 0;
    *out_failure = (neo_execution){0};
    if (vm == NULL || budget_per_turn == 0) { return NEO_INVALID; }
    uint64_t cutoff = vm->next_submission;
    for (neo_registration *r = vm->registrations; r != NULL; r = r->next) {
        if (!r->enabled || r->failed || neo_lookup(vm, r->actor) == NULL) { continue; }
        size_t accepted = 0;
        neo_status status = neo_message_accept_pending(vm, r->context, cutoff, &accepted);
        if (status != NEO_OK) {
            out_failure->status = status;
            out_failure->receiver = r->actor;
            return status;
        }
    }
    /* No language primitive currently mutates registration during a turn. */
    for (neo_registration *r = vm->registrations; r != NULL; r = r->next) {
        neo_object *actor = neo_lookup(vm, r->actor);
        if (!r->enabled || actor == NULL) { continue; }
        if (r->failed) {
            out_failure->status = NEO_BUSY;
            out_failure->receiver = r->actor;
            return NEO_BUSY;
        }
        if (actor->active_message != NULL) {
            out_failure->status = NEO_BUSY;
            out_failure->receiver = r->actor;
            return NEO_BUSY;
        }
        if (actor->inbox_first != NULL) {
            ++*out_turns;
            neo_status status = neo_message_turn(vm, r, budget_per_turn, out_failure);
            if (status != NEO_OK) { return status; }
            neo_execution_release(vm, out_failure);
        }
        neo_object *handlers = neo_child_named(vm, actor->id, "handlers");
        if (handlers != NULL && neo_child_named(vm, handlers->id, "tick") != NULL) {
            ++*out_turns;
            neo_status status = neo_behavior_run(vm, r->authority, "tick", NULL,
                                                 budget_per_turn, out_failure);
            if (status != NEO_OK) {
                r->failed = true;
                r->failed_tick = true;
                return status;
            }
            neo_execution_release(vm, out_failure);
        }
    }
    return NEO_OK;
}

neo_status neo_scheduler_recover(neo_vm *vm, const neo_capability *actor, bool retry) {
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, actor, NEO_ACT, &node);
    if (status != NEO_OK) { return status; }
    for (neo_registration *r = vm->registrations; r != NULL; r = r->next) {
        if (r->actor != node->id) { continue; }
        if (!r->failed) { return NEO_BAD_STATE; }
        if (!r->failed_tick) {
            status = retry ? neo_message_retry(vm, r->context)
                           : neo_message_finish(vm, r->context, node->active_message);
            if (status != NEO_OK) { return status; }
        } else if (!retry) {
            /* Discarding a repeatedly scheduled failure disables this receiver.
             * Explicit re-registration can enable it after repair. */
            r->enabled = false;
        }
        r->failed = false;
        return NEO_OK;
    }
    return NEO_UNAVAILABLE;
}

void neo_scheduler_destroy(neo_vm *vm) {
    while (vm->registrations != NULL) {
        neo_registration *next = vm->registrations->next;
        neo_free(vm, vm->registrations);
        vm->registrations = next;
    }
}

bool neo_scheduler_contains(neo_vm *vm, neo_object_id actor) {
    for (neo_registration *r = vm->registrations; r != NULL; r = r->next) {
        if (r->actor == actor && (r->enabled || r->failed)) { return true; }
    }
    return false;
}
