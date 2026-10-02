#include "neo_image.h"
#include "neo_execution.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(EXIT_FAILURE); \
} } while (0)
#define OK(expression) CHECK((expression) == NEO_OK)

static const char *neo_counter_source =
    "(image #root (counter #counter (count :integer 0) @home #root 1"
    " (handlers (increment (body :primitive do"
    "   (set :primitive write (slot :text count)"
    "     (value :primitive add (left :primitive read (slot :text count)) (right :integer 1)))"
    "   (return :primitive return (value :primitive read (slot :text count)))))"
    " (bad (body :primitive do"
    "   (set :primitive write (slot :text count) (value :integer 8))"
    "   (fail :primitive div (left :integer 1) (right :integer 0))))"
    " (loop (body :primitive while (condition :boolean true) (body :object)))"
    " (lazy (body :primitive if (condition :boolean false)"
    "   (then :primitive fail) (else :text \"safe > \\\"quote\\\"\\n\")))"
    " (nested (body :primitive add (left :integer 4)"
    "   (right :primitive return (value :object))))"
    " (short (body :primitive and (left :boolean false) (right :primitive fail)))"
    " (unknown (body :primitive nonexistent (left :primitive fail)))"
    " (overflow (body :primitive add (left :integer 9223372036854775807) (right :integer 1)))"
    " (multiply (body :primitive mul (left :integer -9223372036854775808) (right :integer -1)))"
    " (divide (body :primitive div (left :integer -9223372036854775808) (right :integer -1)))"
    " (sum (body :primitive do"
    "   (reset :primitive write (slot :text count) (value :integer 0))"
    "   (loop :primitive while (condition :primitive lt"
    "     (left :primitive read (slot :text count)) (right :integer 4))"
    "     (body :primitive write (slot :text count) (value :primitive add"
    "       (left :primitive read (slot :text count)) (right :integer 1))))"
    "   (result :primitive read (slot :text count)))))))";

static const neo_capability *neo_parse(neo_vm *vm, const char *source) {
    const neo_capability *root = NULL;
    neo_diagnostic error;
    neo_status status = neo_image_parse(vm, source, &root, &error);
    if (status != NEO_OK) {
        fprintf(stderr, "%zu:%zu %s\n", error.line, error.column, error.message);
    }
    OK(status);
    return root;
}

static const neo_capability *neo_child(neo_vm *vm, const neo_capability *parent, const char *name) {
    const neo_capability *child;
    OK(neo_object_child(vm, parent, name, &child));
    return child;
}

static int64_t neo_count(neo_vm *vm, const neo_capability *actor) {
    neo_value value;
    OK(neo_object_read(vm, neo_child(vm, actor, "count"), &value));
    CHECK(value.kind == NEO_INTEGER);
    return value.integer;
}

static void neo_test_reader_and_roundtrip(void) {
    neo_vm *vm = NULL;
    OK(neo_vm_create(NULL, &vm));
    const neo_capability *root = neo_parse(vm, neo_counter_source);
    const neo_capability *actor = neo_child(vm, root, "counter");
    CHECK(neo_count(vm, actor) == 0); /* Loading never ran code. */
    char *text;
    OK(neo_image_format(vm, root, &text));
    const neo_capability *copy = neo_parse(vm, text);
    neo_image_text_free(vm, text);
    const neo_capability *copy_actor = neo_child(vm, copy, "counter");
    const neo_capability *home;
    OK(neo_object_connection(vm, copy_actor, "home", &home));
    neo_object_id a, b;
    OK(neo_object_identity(vm, copy, &a));
    OK(neo_object_identity(vm, home, &b));
    CHECK(a == b);
    CHECK(neo_object_write(vm, home, (neo_value){.kind = NEO_INTEGER, .integer = 7}) == NEO_DENIED);
    neo_execution result;
    OK(neo_behavior_run(vm, copy_actor, "increment", NULL, 100, &result));
    CHECK(result.result.kind == NEO_INTEGER && result.result.integer == 1);
    neo_execution_release(vm, &result);
    CHECK(neo_count(vm, actor) == 0 && neo_count(vm, copy_actor) == 1);
    const neo_capability *clone;
    OK(neo_image_duplicate(vm, root, "clone", &clone));
    OK(neo_behavior_run(vm, neo_child(vm, clone, "counter"), "sum", NULL, 200, &result));
    CHECK(result.result.integer == 4); /* Copied do order was preserved. */
    neo_execution_release(vm, &result);
    const char *bad[] = {
        "", "(x", "(x) trailing", "(x (same) (same))", "(x #x (y #x))",
        "(x @missing #missing 1)", "(x :integer 9223372036854775808)",
        "(x :boolean yes)", "(x :text \"unterminated)", "(x :text \"bad\\q\")",
        "(x :unknown)", "(x @bad #x 256)", "(x #x @bad #x -1)",
        "(x #x @same #x 1 @same #x 1)"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        neo_diagnostic error;
        const neo_capability *invalid = root;
        CHECK(neo_image_parse(vm, bad[i], &invalid, &error) != NEO_OK);
        CHECK(invalid == NULL && error.message[0] != '\0');
        CHECK(neo_count(vm, actor) == 0);
    }
    const neo_capability *cycle = neo_parse(vm, "(x #x @self #x 1 (y #y @parent #x 1) @y #y 1)");
    OK(neo_image_format(vm, cycle, &text));
    (void)neo_parse(vm, text);
    neo_image_text_free(vm, text);
    neo_vm_destroy(vm);
}

static void neo_test_evaluator(void) {
    neo_vm *vm;
    OK(neo_vm_create(NULL, &vm));
    const neo_capability *root = neo_parse(vm, neo_counter_source);
    const neo_capability *actor = neo_child(vm, root, "counter");
    neo_execution result;
    OK(neo_behavior_run(vm, actor, "lazy", NULL, 100, &result));
    CHECK(strcmp(result.result.text, "safe > \"quote\"\n") == 0);
    neo_execution_release(vm, &result);
    OK(neo_behavior_run(vm, actor, "nested", NULL, 100, &result));
    CHECK(result.result.kind == NEO_OBJECT);
    neo_execution_release(vm, &result);
    OK(neo_behavior_run(vm, actor, "short", NULL, 100, &result));
    CHECK(result.result.kind == NEO_BOOLEAN && !result.result.boolean);
    neo_execution_release(vm, &result);
    CHECK(neo_behavior_run(vm, actor, "unknown", NULL, 100, &result) == NEO_UNSUPPORTED);
    CHECK(result.operation != 0);
    CHECK(neo_behavior_run(vm, actor, "overflow", NULL, 100, &result) == NEO_OVERFLOW);
    CHECK(neo_behavior_run(vm, actor, "multiply", NULL, 100, &result) == NEO_OVERFLOW);
    CHECK(neo_behavior_run(vm, actor, "divide", NULL, 100, &result) == NEO_OVERFLOW);
    CHECK(neo_behavior_run(vm, actor, "loop", NULL, 30, &result) == NEO_LIMIT);
    CHECK(result.steps == 30);
    CHECK(neo_behavior_run(vm, actor, "bad", NULL, 100, &result) == NEO_DIVIDE_BY_ZERO);
    CHECK(neo_count(vm, actor) == 8); /* Failure has no implicit rollback. */
    const neo_capability *read_only;
    OK(neo_capability_restrict(vm, actor, NEO_READ | NEO_ACT, &read_only));
    CHECK(neo_behavior_run(vm, read_only, "increment", NULL, 100, &result) == NEO_DENIED);
    CHECK(neo_count(vm, actor) == 8);
    OK(neo_behavior_run(vm, actor, "sum", NULL, 200, &result));
    CHECK(result.result.integer == 4);
    neo_execution_release(vm, &result);
    neo_vm_destroy(vm);
}

static void neo_send_request(neo_vm *vm, const neo_context *sender,
                              const neo_capability *ether, const neo_capability *receiver,
                              const char *selector) {
    const neo_message *message;
    OK(neo_message_create(vm, sender, ether, receiver,
        (neo_value){.kind = NEO_TEXT, .text = selector}, &message));
    OK(neo_message_submit(vm, sender, message));
}

static void neo_test_scheduler(void) {
    neo_vm *vm;
    OK(neo_vm_create(NULL, &vm));
    const neo_capability *root = neo_parse(vm, neo_counter_source);
    const neo_capability *actor = neo_child(vm, root, "counter");
    const neo_capability *ether;
    OK(neo_ether_create(vm, root, &ether));
    const neo_context *sender;
    OK(neo_context_create(vm, root, &sender));
    OK(neo_scheduler_register(vm, actor));
    neo_send_request(vm, sender, ether, actor, "increment");
    neo_send_request(vm, sender, ether, actor, "increment");
    size_t turns;
    neo_execution failure;
    OK(neo_scheduler_tick(vm, 100, &turns, &failure));
    CHECK(turns == 1 && neo_count(vm, actor) == 1);
    OK(neo_scheduler_tick(vm, 100, &turns, &failure));
    CHECK(turns == 1 && neo_count(vm, actor) == 2);
    OK(neo_scheduler_tick(vm, 100, &turns, &failure));
    CHECK(turns == 0);
    neo_send_request(vm, sender, ether, actor, "bad");
    CHECK(neo_scheduler_tick(vm, 100, &turns, &failure) == NEO_DIVIDE_BY_ZERO);
    CHECK(neo_count(vm, actor) == 8 && failure.operation != 0);
    CHECK(neo_scheduler_tick(vm, 100, &turns, &failure) == NEO_BUSY);
    /* Repair the behavior graph, then explicitly retry the retained message. */
    const neo_capability *handlers = neo_child(vm, actor, "handlers");
    const neo_capability *body = neo_child(vm, neo_child(vm, handlers, "bad"), "body");
    const neo_capability *divisor = neo_child(vm, neo_child(vm, body, "fail"), "right");
    OK(neo_object_write(vm, divisor, (neo_value){.kind = NEO_INTEGER, .integer = 1}));
    OK(neo_scheduler_recover(vm, actor, true));
    OK(neo_scheduler_tick(vm, 100, &turns, &failure));
    CHECK(turns == 1);
    neo_send_request(vm, sender, ether, actor, "unknown");
    CHECK(neo_scheduler_tick(vm, 100, &turns, &failure) == NEO_UNSUPPORTED);
    OK(neo_scheduler_recover(vm, actor, false));
    OK(neo_scheduler_tick(vm, 100, &turns, &failure));
    CHECK(turns == 0);
    OK(neo_image_unload(vm, root));
    OK(neo_scheduler_tick(vm, 100, &turns, &failure));
    CHECK(turns == 0);
    neo_vm_destroy(vm);
}

static void neo_test_limits_and_scheduled_state(void) {
    neo_vm *vm;
    OK(neo_vm_create(NULL, &vm));
    const neo_capability *root = neo_parse(vm, neo_counter_source);
    const neo_capability *actor = neo_child(vm, root, "counter");
    OK(neo_scheduler_register(vm, actor));
    char *text = NULL;
    CHECK(neo_image_format(vm, root, &text) == NEO_UNSUPPORTED && text == NULL);
    const neo_capability *copy = NULL;
    CHECK(neo_image_duplicate(vm, root, "copy", &copy) == NEO_UNSUPPORTED && copy == NULL);
    OK(neo_scheduler_unregister(vm, actor));
    OK(neo_image_format(vm, root, &text));
    neo_image_text_free(vm, text);
    char deep[1024];
    size_t length = 0;
    for (size_t i = 0; i < 129; ++i) {
        memcpy(deep + length, "(x ", 3);
        length += 3;
    }
    for (size_t i = 0; i < 129; ++i) { deep[length++] = ')'; }
    deep[length] = '\0';
    neo_diagnostic error;
    CHECK(neo_image_parse(vm, deep, &copy, &error) == NEO_LIMIT);
    CHECK(copy == NULL && error.line == 1);
    char *large = malloc(1024u * 1024u + 2u);
    CHECK(large != NULL);
    memset(large, ' ', 1024u * 1024u + 1u);
    large[1024u * 1024u + 1u] = '\0';
    CHECK(neo_image_parse(vm, large, &copy, &error) == NEO_LIMIT);
    free(large);
    CHECK(neo_count(vm, actor) == 0);
    neo_vm_destroy(vm);
}

typedef struct neo_faults { size_t call, fail, live; } neo_faults;
static void *neo_fault_allocate(void *context, size_t size) {
    neo_faults *f = context;
    if (f->call++ == f->fail) { return NULL; }
    void *memory = malloc(size);
    if (memory != NULL) { ++f->live; }
    return memory;
}
static void neo_fault_release(void *context, void *memory) {
    neo_faults *f = context;
    CHECK(memory != NULL && f->live > 0);
    --f->live;
    free(memory);
}
static void neo_test_codec_allocation_failures(void) {
    for (unsigned operation = 0; operation < 2; ++operation) {
        bool done = false;
        for (size_t fail = 0; fail < 300 && !done; ++fail) {
            neo_faults faults = {.fail = SIZE_MAX};
            neo_allocator allocator = {&faults, neo_fault_allocate, neo_fault_release};
            neo_vm *vm;
            OK(neo_vm_create(&allocator, &vm));
            const neo_capability *root = neo_parse(vm, "(root #root (text :text \"hello\") @self #root 1)");
            size_t before = faults.live;
            faults.call = 0;
            faults.fail = fail;
            const neo_capability *copy = NULL;
            char *text = NULL;
            neo_status status = operation == 0
                ? neo_image_parse(vm, "(copy #copy @self #copy 1 (n :integer 9))", &copy, NULL)
                : neo_image_format(vm, root, &text);
            faults.fail = SIZE_MAX;
            if (status == NEO_OUT_OF_MEMORY) {
                CHECK(copy == NULL && text == NULL && faults.live == before);
            } else {
                CHECK(status == NEO_OK);
                done = true;
            }
            neo_image_text_free(vm, text);
            neo_vm_destroy(vm);
            CHECK(faults.live == 0);
        }
        CHECK(done);
    }
}

static void neo_test_dynamic_literals(void) {
    neo_vm *vm;
    OK(neo_vm_create(NULL, &vm));
    const neo_capability *root = neo_parse(vm,
        "(image (actor (n 7) (text \"7\") (flag true)"
        " (handlers (change (body (write (slot \"n\") (value \"seven\"))))"
        " (bad (body (add (left (read (slot \"n\"))) (right 1)))))))");
    const neo_capability *actor = neo_child(vm, root, "actor");
    const neo_capability *n = neo_child(vm, actor, "n");
    neo_value value;
    OK(neo_object_read(vm, n, &value));
    CHECK(value.kind == NEO_INTEGER && value.integer == 7);
    OK(neo_object_read(vm, neo_child(vm, actor, "text"), &value));
    CHECK(value.kind == NEO_TEXT && strcmp(value.text, "7") == 0);
    const neo_capability *names = neo_parse(vm, "(names (if :object) (add 7) (future))");
    OK(neo_object_read(vm, neo_child(vm, names, "if"), &value));
    CHECK(value.kind == NEO_OBJECT);
    OK(neo_object_read(vm, neo_child(vm, names, "add"), &value));
    CHECK(value.kind == NEO_INTEGER);
    char *data_text;
    OK(neo_image_format(vm, names, &data_text));
    const neo_capability *names_copy = neo_parse(vm, data_text);
    neo_image_text_free(vm, data_text);
    OK(neo_object_read(vm, neo_child(vm, names_copy, "if"), &value));
    CHECK(value.kind == NEO_OBJECT);
    neo_execution report;
    OK(neo_behavior_run(vm, actor, "change", NULL, 100, &report));
    neo_execution_release(vm, &report);
    OK(neo_object_read(vm, n, &value));
    CHECK(value.kind == NEO_TEXT && strcmp(value.text, "seven") == 0);
    CHECK(neo_behavior_run(vm, actor, "bad", NULL, 100, &report) == NEO_WRONG_KIND);
    neo_execution_release(vm, &report);
    char *formatted;
    OK(neo_image_format(vm, root, &formatted));
    CHECK(strstr(formatted, ":integer") == NULL && strstr(formatted, ":text") == NULL);
    CHECK(strstr(formatted, ":primitive") == NULL);
    (void)neo_parse(vm, formatted);
    neo_image_text_free(vm, formatted);
    const char *bad[] = {"(n 9223372036854775808)", "(n 1.5)", "(n unquoted)", "(n :object 7)", "(n 7 8)"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        const neo_capability *invalid;
        neo_diagnostic error;
        CHECK(neo_image_parse(vm, bad[i], &invalid, &error) != NEO_OK);
    }
    neo_vm_destroy(vm);
}

static void neo_test_tokens_and_comments(void) {
    neo_vm *vm;
    OK(neo_vm_create(NULL, &vm));
    const neo_capability *root = neo_parse(vm,
        "// leading comment\n(image #root\n"
        " (word \"if\")// adjacent comment\n"
        " (if (condition true) (then 1) (else 0))\n"
        " (url \"https://example.test/a>b\")\n"
        " (greater>name) (slash/name) @self #root 1)// trailing comment");
    neo_value value;
    OK(neo_object_read(vm, neo_child(vm, root, "word"), &value));
    CHECK(value.kind == NEO_TEXT && strcmp(value.text, "if") == 0);
    OK(neo_object_read(vm, neo_child(vm, root, "if"), &value));
    CHECK(value.kind == NEO_PRIMITIVE && strcmp(value.text, "if") == 0);
    OK(neo_object_read(vm, neo_child(vm, root, "url"), &value));
    CHECK(value.kind == NEO_TEXT && strcmp(value.text, "https://example.test/a>b") == 0);
    char *text;
    OK(neo_image_format(vm, root, &text));
    CHECK(strstr(text, "(image ") != NULL && strstr(text, "@self ") != NULL);
    const neo_capability *copy = neo_parse(vm, text);
    neo_image_text_free(vm, text);
    OK(neo_object_read(vm, neo_child(vm, copy, "word"), &value));
    CHECK(value.kind == NEO_TEXT && strcmp(value.text, "if") == 0);
    const char *bad[] = {
        "(\"if\")", "(\"display name\")", "(na\"me)",
        "(x :primitive \"if\")", "(x :integer \"7\")", "(x :boolean \"true\")",
        "(x #x @\"edge\" #x 1)", "(x #x @edge \"#x\" 1)",
        "(x #x @edge #x \"1\")", "> old comment\n(image)"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        const neo_capability *invalid = NULL;
        neo_diagnostic error;
        CHECK(neo_image_parse(vm, bad[i], &invalid, &error) == NEO_PARSE_ERROR);
        CHECK(invalid == NULL && error.message[0] != '\0');
    }
    const neo_capability *host;
    OK(neo_image_create(vm, "display name", &host));
    CHECK(neo_image_format(vm, host, &text) == NEO_UNSUPPORTED && text == NULL);
    neo_vm_destroy(vm);
}

int main(void) {
    neo_test_tokens_and_comments();
    neo_test_dynamic_literals();
    neo_test_reader_and_roundtrip();
    neo_test_evaluator();
    neo_test_scheduler();
    neo_test_codec_allocation_failures();
    neo_test_limits_and_scheduled_state();
    puts("PASS: image roundtrip, parse errors, lazy control flow, arithmetic, execution limits, scheduler recovery, allocation rollback");
    return EXIT_SUCCESS;
}
