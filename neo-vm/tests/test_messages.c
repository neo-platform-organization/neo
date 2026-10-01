#include "neo_message.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)
#define OK(expression) CHECK((expression) == NEO_OK)

typedef struct neo_fixture {
    neo_vm *vm;
    const neo_capability *root;
    const neo_capability *ether;
    const neo_capability *a;
    const neo_capability *b;
    const neo_capability *d;
    const neo_context *a_context;
    const neo_context *b_context;
    const neo_context *d_context;
} neo_fixture;

static neo_value neo_text(const char *text) {
    return (neo_value){.kind = NEO_TEXT, .text = text};
}

static neo_fixture neo_setup(const neo_allocator *allocator) {
    neo_fixture f = {0};
    OK(neo_vm_create(allocator, &f.vm));
    OK(neo_image_create(f.vm, "plate", &f.root));
    OK(neo_ether_create(f.vm, f.root, &f.ether));
    OK(neo_object_create(f.vm, f.root, "A", (neo_value){0}, &f.a));
    OK(neo_object_create(f.vm, f.root, "B", (neo_value){0}, &f.b));
    OK(neo_object_create(f.vm, f.root, "D", (neo_value){0}, &f.d));
    OK(neo_context_create(f.vm, f.a, &f.a_context));
    OK(neo_context_create(f.vm, f.b, &f.b_context));
    OK(neo_context_create(f.vm, f.d, &f.d_context));
    return f;
}

static void neo_expect_text(neo_fixture *f, const neo_context *context,
                            const neo_message *message, const char *path, const char *text) {
    neo_value value;
    OK(neo_message_read(f->vm, context, message, path, &value));
    CHECK(value.kind == NEO_TEXT && strcmp(value.text, text) == 0);
}

static void neo_test_policy_and_acceptance(void) {
    neo_fixture f = neo_setup(NULL);
    const neo_message *message, *active;
    OK(neo_message_create(f.vm, f.a_context, f.ether, f.d, neo_text("open"), &message));
    neo_value value;
    neo_message_info info;
    CHECK(neo_message_read(f.vm, f.b_context, message, "", &value) == NEO_DENIED);
    CHECK(neo_message_inspect(f.vm, f.b_context, message, &info) == NEO_DENIED);
    CHECK(neo_message_grant(f.vm, f.b_context, message, f.b, NEO_MESSAGE_ALL) == NEO_DENIED);
    OK(neo_message_grant(f.vm, f.a_context, message, f.b, NEO_MESSAGE_READ | NEO_MESSAGE_EDIT));
    neo_expect_text(&f, f.b_context, message, "", "open");
    CHECK(neo_message_inspect(f.vm, f.b_context, message, &info) == NEO_DENIED);
    OK(neo_message_field_create(f.vm, f.a_context, message, "", "arguments", (neo_value){0}));
    OK(neo_message_field_create(f.vm, f.b_context, message, "arguments", "mode", neo_text("slow")));
    CHECK(neo_message_field_create(f.vm, f.a_context, message, "", "../bad", neo_text("x")) == NEO_INVALID);
    CHECK(neo_message_read(f.vm, f.a_context, message, "arguments/../", &value) == NEO_INVALID);
    CHECK(neo_message_read(f.vm, f.a_context, message, "arguments/", &value) == NEO_INVALID);
    CHECK(neo_message_submit(f.vm, f.b_context, message) == NEO_DENIED);
    CHECK(neo_message_accept(f.vm, f.d_context, message) == NEO_BAD_STATE);
    OK(neo_message_submit(f.vm, f.a_context, message));
    CHECK(neo_message_submit(f.vm, f.a_context, message) == NEO_BAD_STATE);
    CHECK(neo_message_grant(f.vm, f.a_context, message, f.b, 0) == NEO_BAD_STATE);
    OK(neo_message_write(f.vm, f.a_context, message, "", neo_text("close")));
    OK(neo_message_write(f.vm, f.b_context, message, "arguments/mode", neo_text("fast")));
    CHECK(neo_message_accept(f.vm, f.b_context, message) == NEO_DENIED);
    OK(neo_message_inspect(f.vm, f.a_context, message, &info));
    CHECK(info.state == NEO_MESSAGE_PENDING && info.receiver_available);
    /* The ETHER contains a real object. Even a host root-derived alias cannot
     * bypass policy through generic reads/writes/copy/delete of the payload. */
    char name[32];
    (void)snprintf(name, sizeof(name), "message-%" PRIu64, info.object);
    const neo_capability *alias, *out;
    OK(neo_object_child(f.vm, f.ether, name, &alias));
    CHECK(neo_object_read(f.vm, alias, &value) == NEO_DENIED);
    CHECK(neo_object_write(f.vm, alias, neo_text("bypass")) == NEO_DENIED);
    CHECK(neo_object_child(f.vm, alias, "arguments", &out) == NEO_DENIED);
    CHECK(neo_object_delete(f.vm, alias) == NEO_DENIED);
    CHECK(neo_object_copy(f.vm, alias, f.root, "bypass", &out) == NEO_DENIED);
    CHECK(neo_object_move(f.vm, alias, f.root, "bypass", &out) == NEO_DENIED);
    OK(neo_message_accept(f.vm, f.d_context, message));
    CHECK(neo_message_accept(f.vm, f.d_context, message) == NEO_BAD_STATE);
    CHECK(neo_message_write(f.vm, f.a_context, message, "", neo_text("late")) == NEO_DENIED);
    CHECK(neo_message_write(f.vm, f.b_context, message, "arguments/mode", neo_text("late")) == NEO_DENIED);
    CHECK(neo_message_field_create(f.vm, f.a_context, message, "", "late", neo_text("late")) == NEO_DENIED);
    CHECK(neo_object_write(f.vm, alias, neo_text("late")) == NEO_DENIED);
    neo_expect_text(&f, f.d_context, message, "", "close");
    neo_expect_text(&f, f.d_context, message, "arguments/mode", "fast");
    CHECK(neo_image_duplicate(f.vm, f.root, "clone", &out) == NEO_UNSUPPORTED);
    CHECK(neo_object_copy(f.vm, f.a, f.root, "A-copy", &out) == NEO_UNSUPPORTED);
    CHECK(neo_object_copy(f.vm, f.d, f.root, "D-copy", &out) == NEO_UNSUPPORTED);
    CHECK(neo_object_delete(f.vm, f.ether) == NEO_DENIED);
    OK(neo_message_begin(f.vm, f.d_context, &active));
    CHECK(active == message);
    CHECK(neo_message_begin(f.vm, f.d_context, &active) == NEO_BUSY && active == NULL);
    CHECK(neo_message_finish(f.vm, f.a_context, message) == NEO_DENIED);
    OK(neo_message_finish(f.vm, f.d_context, message));
    CHECK(neo_message_finish(f.vm, f.d_context, message) == NEO_BAD_STATE);
    OK(neo_message_inspect(f.vm, f.d_context, message, &info));
    CHECK(info.state == NEO_MESSAGE_PROCESSED);
    CHECK(neo_message_begin(f.vm, f.d_context, &active) == NEO_UNAVAILABLE);
    size_t count;
    OK(neo_object_child_count(f.vm, f.ether, &count));
    CHECK(count == 1); /* Acceptance/processing did not move the message. */
    neo_vm_destroy(f.vm);
}

static void neo_test_ordering(void) {
    neo_fixture f = neo_setup(NULL);
    const neo_message *a1, *a2, *b1, *current;
    OK(neo_message_create(f.vm, f.a_context, f.ether, f.d, neo_text("open"), &a1));
    OK(neo_message_create(f.vm, f.a_context, f.ether, f.d, neo_text("close"), &a2));
    OK(neo_message_create(f.vm, f.b_context, f.ether, f.d, neo_text("open"), &b1));
    OK(neo_message_submit(f.vm, f.a_context, a1));
    OK(neo_message_submit(f.vm, f.a_context, a2));
    OK(neo_message_submit(f.vm, f.b_context, b1));
    CHECK(neo_message_accept(f.vm, f.d_context, a2) == NEO_BUSY);
    OK(neo_message_accept(f.vm, f.d_context, b1));
    OK(neo_message_accept(f.vm, f.d_context, a1));
    OK(neo_message_accept(f.vm, f.d_context, a2));
    const neo_message *expected[] = {b1, a1, a2};
    for (size_t i = 0; i < 3; ++i) {
        OK(neo_message_begin(f.vm, f.d_context, &current));
        CHECK(current == expected[i]);
        OK(neo_message_finish(f.vm, f.d_context, current));
    }
    CHECK(neo_message_begin(f.vm, f.d_context, &current) == NEO_UNAVAILABLE);
    neo_vm_destroy(f.vm);
}

static void neo_test_identity_and_deletion(void) {
    neo_fixture f = neo_setup(NULL), other = neo_setup(NULL);
    const neo_message *message;
    const neo_context *context;
    const neo_capability *restricted, *root2, *actor2;
    OK(neo_capability_restrict(f.vm, f.a, NEO_READ, &restricted));
    CHECK(neo_context_create(f.vm, restricted, &context) == NEO_DENIED);
    CHECK(neo_message_create(f.vm, other.a_context, f.ether, f.d, neo_text("open"), &message) == NEO_DENIED);
    OK(neo_capability_restrict(f.vm, f.d, NEO_READ, &restricted));
    CHECK(neo_message_create(f.vm, f.a_context, f.ether, restricted, neo_text("open"), &message) == NEO_DENIED);
    OK(neo_image_create(f.vm, "other", &root2));
    OK(neo_object_create(f.vm, root2, "actor", (neo_value){0}, &actor2));
    CHECK(neo_message_create(f.vm, f.a_context, f.ether, actor2, neo_text("open"), &message) == NEO_WRONG_IMAGE);
    OK(neo_message_create(f.vm, f.a_context, f.ether, f.d, neo_text("open"), &message));
    neo_message_info info;
    CHECK(neo_message_inspect(other.vm, other.a_context, message, &info) == NEO_DENIED);
    OK(neo_message_submit(f.vm, f.a_context, message));
    OK(neo_object_delete(f.vm, f.d));
    CHECK(neo_message_accept(f.vm, f.d_context, message) == NEO_UNAVAILABLE);
    OK(neo_message_inspect(f.vm, f.a_context, message, &info));
    CHECK(!info.receiver_available && info.state == NEO_MESSAGE_PENDING);
    /* A replacement with the same name cannot accept the old request. */
    OK(neo_object_create(f.vm, f.root, "D", (neo_value){0}, &actor2));
    OK(neo_context_create(f.vm, actor2, &context));
    CHECK(neo_message_accept(f.vm, context, message) == NEO_DENIED);
    OK(neo_image_unload(f.vm, f.root));
    CHECK(neo_message_inspect(f.vm, f.a_context, message, &info) == NEO_UNAVAILABLE);
    neo_vm_destroy(other.vm);
    neo_vm_destroy(f.vm);
}

typedef struct neo_faults {
    size_t calls;
    size_t fail_at;
    size_t live;
} neo_faults;

static void *neo_fault_allocate(void *context, size_t size) {
    neo_faults *faults = context;
    if (faults->calls++ == faults->fail_at) {
        return NULL;
    }
    void *memory = malloc(size);
    if (memory != NULL) {
        ++faults->live;
    }
    return memory;
}

static void neo_fault_release(void *context, void *memory) {
    neo_faults *faults = context;
    CHECK(memory != NULL && faults->live > 0);
    --faults->live;
    free(memory);
}

static void neo_test_failure_boundaries(void) {
    for (unsigned operation = 0; operation < 4; ++operation) {
        bool succeeded = false;
        for (size_t fail = 0; fail < 32; ++fail) {
            neo_faults faults = {.fail_at = SIZE_MAX};
            neo_allocator allocator = {&faults, neo_fault_allocate, neo_fault_release};
            neo_fixture f = neo_setup(&allocator);
            const neo_message *message, *created = NULL;
            OK(neo_message_create(f.vm, f.a_context, f.ether, f.d, neo_text("open"), &message));
            size_t before = faults.live;
            faults.calls = 0;
            faults.fail_at = fail;
            neo_status status;
            if (operation == 0) {
                status = neo_message_create(f.vm, f.a_context, f.ether, f.d, neo_text("copy"), &created);
            } else if (operation == 1) {
                status = neo_message_write(f.vm, f.a_context, message, "", neo_text("close"));
            } else if (operation == 2) {
                status = neo_message_field_create(f.vm, f.a_context, message, "", "field", neo_text("nested"));
            } else {
                status = neo_message_grant(f.vm, f.a_context, message, f.b, NEO_MESSAGE_READ);
            }
            faults.fail_at = SIZE_MAX;
            if (status == NEO_OUT_OF_MEMORY) {
                CHECK(faults.live == before);
                CHECK(created == NULL);
                size_t count;
                OK(neo_object_child_count(f.vm, f.ether, &count));
                CHECK(count == 1);
                neo_expect_text(&f, f.a_context, message, "", "open");
                neo_value value;
                CHECK(neo_message_read(f.vm, f.a_context, message, "field", &value) == NEO_UNAVAILABLE);
                CHECK(neo_message_read(f.vm, f.b_context, message, "", &value) == NEO_DENIED);
            } else {
                CHECK(status == NEO_OK);
                succeeded = true;
            }
            /* No allocation failure can split acceptance from queue insertion. */
            OK(neo_message_submit(f.vm, f.a_context, message));
            faults.calls = 0;
            faults.fail_at = 0;
            OK(neo_message_accept(f.vm, f.d_context, message));
            const neo_message *active;
            OK(neo_message_begin(f.vm, f.d_context, &active));
            CHECK(active == message);
            OK(neo_message_finish(f.vm, f.d_context, active));
            CHECK(faults.calls == 0);
            faults.fail_at = SIZE_MAX;
            neo_vm_destroy(f.vm);
            CHECK(faults.live == 0);
            if (succeeded) {
                break;
            }
        }
        CHECK(succeeded);
    }
}

int main(void) {
    neo_test_policy_and_acceptance();
    neo_test_ordering();
    neo_test_identity_and_deletion();
    neo_test_failure_boundaries();
    puts("PASS: message policy, nested edits, acceptance, FIFO, identity, deletion, allocation rollback");
    return EXIT_SUCCESS;
}
