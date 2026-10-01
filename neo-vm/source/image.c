#include "internal.h"
#include "neo_image.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEO_SOURCE_LIMIT (1024u * 1024u)
#define NEO_TOKEN_LIMIT 4096u
#define NEO_DEPTH_LIMIT 128u
#define NEO_NODE_LIMIT 4096u

typedef struct neo_load_node {
    neo_object *object;
    char *label;
    struct neo_load_node *next;
} neo_load_node;

typedef struct neo_load_edge {
    neo_edge *edge;
    char *label;
    struct neo_load_edge *next;
} neo_load_edge;

typedef struct neo_reader {
    neo_vm *vm;
    const char *cursor;
    size_t line;
    size_t column;
    size_t count;
    neo_load_node *nodes;
    neo_load_edge *edges;
    neo_diagnostic *error;
} neo_reader;

static neo_status neo_parse_fail(neo_reader *reader, neo_status status, const char *message) {
    if (reader->error != NULL && reader->error->message[0] == '\0') {
        reader->error->line = reader->line;
        reader->error->column = reader->column;
        (void)snprintf(reader->error->message, sizeof(reader->error->message), "%s", message);
    }
    return status;
}

static char neo_advance(neo_reader *reader) {
    char character = *reader->cursor++;
    if (character == '\n') {
        ++reader->line;
        reader->column = 1;
    } else {
        ++reader->column;
    }
    return character;
}

static void neo_space(neo_reader *reader) {
    for (;;) {
        while (isspace((unsigned char)*reader->cursor)) {
            (void)neo_advance(reader);
        }
        if (*reader->cursor != '>') {
            return;
        }
        while (*reader->cursor != '\0' && *reader->cursor != '\n') {
            (void)neo_advance(reader);
        }
    }
}

/* Reader strings are byte strings for this v0 codec: ASCII escapes plus UTF-8
 * bytes preserved verbatim. Unicode normalization is deliberately not implied. */
static neo_status neo_token(neo_reader *reader, char *out) {
    neo_space(reader);
    size_t length = 0;
    bool quoted = *reader->cursor == '"';
    if (quoted) {
        (void)neo_advance(reader);
    }
    while (*reader->cursor != '\0') {
        char c = *reader->cursor;
        if (quoted && c == '"') {
            (void)neo_advance(reader);
            out[length] = '\0';
            return NEO_OK;
        }
        if (!quoted && (isspace((unsigned char)c) || c == '(' || c == ')' || c == '>')) {
            break;
        }
        (void)neo_advance(reader);
        if (quoted && c == '\\') {
            c = *reader->cursor;
            if (c == '\0') {
                break;
            }
            (void)neo_advance(reader);
            switch (c) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case '\\': case '"': break;
                default: return neo_parse_fail(reader, NEO_PARSE_ERROR, "unsupported string escape");
            }
        } else if (quoted && (unsigned char)c < 32u) {
            return neo_parse_fail(reader, NEO_PARSE_ERROR, "escape control characters in strings");
        }
        if (length == NEO_TOKEN_LIMIT) {
            return neo_parse_fail(reader, NEO_LIMIT, "token exceeds 4096 bytes");
        }
        out[length++] = c;
    }
    if (quoted || length == 0) {
        return neo_parse_fail(reader, NEO_PARSE_ERROR, "expected token or closing quote");
    }
    out[length] = '\0';
    return NEO_OK;
}

static neo_status neo_read_edge(neo_reader *reader, neo_object *owner) {
    char name[NEO_TOKEN_LIMIT + 1], label[NEO_TOKEN_LIMIT + 1], number[NEO_TOKEN_LIMIT + 1];
    (void)neo_advance(reader); /* @ */
    neo_status status = neo_token(reader, name);
    if (status == NEO_OK) {
        status = neo_token(reader, label);
    }
    if (status == NEO_OK) {
        status = neo_token(reader, number);
    }
    if (status != NEO_OK) {
        return status;
    }
    if (name[0] == '\0' || label[0] != '#' || label[1] == '\0') {
        return neo_parse_fail(reader, NEO_PARSE_ERROR, "edge syntax: @name #target rights");
    }
    char *end = NULL;
    errno = 0;
    unsigned long rights = strtoul(number, &end, 10);
    if (number[0] == '-' || end == number || *end != '\0' || errno != 0 || rights > NEO_ALL) {
        return neo_parse_fail(reader, NEO_PARSE_ERROR, "invalid capability rights mask");
    }
    for (neo_edge *edge = owner->edges; edge != NULL; edge = edge->next) {
        if (strcmp(edge->name, name) == 0) {
            return neo_parse_fail(reader, NEO_CONFLICT, "duplicate edge name");
        }
    }
    neo_edge *edge = neo_alloc(reader->vm, sizeof(*edge));
    if (edge == NULL) {
        return NEO_OUT_OF_MEMORY;
    }
    edge->next = owner->edges;
    owner->edges = edge;
    edge->name = neo_string(reader->vm, name);
    edge->rights = (unsigned)rights;
    neo_load_edge *pending = neo_alloc(reader->vm, sizeof(*pending));
    if (pending == NULL) {
        return NEO_OUT_OF_MEMORY;
    }
    pending->edge = edge;
    pending->label = neo_string(reader->vm, label + 1);
    pending->next = reader->edges;
    reader->edges = pending;
    return edge->name == NULL || pending->label == NULL ? NEO_OUT_OF_MEMORY : NEO_OK;
}

static neo_status neo_read_node(neo_reader *reader, neo_object *parent,
                                size_t depth, neo_object **out_node) {
    if (depth > NEO_DEPTH_LIMIT || reader->count == NEO_NODE_LIMIT) {
        return neo_parse_fail(reader, NEO_LIMIT, "image depth or object limit exceeded");
    }
    neo_space(reader);
    if (*reader->cursor != '(') {
        return neo_parse_fail(reader, NEO_PARSE_ERROR, "expected opening parenthesis");
    }
    (void)neo_advance(reader);
    char name[NEO_TOKEN_LIMIT + 1], token[NEO_TOKEN_LIMIT + 1];
    neo_status status = neo_token(reader, name);
    if (status != NEO_OK) {
        return status;
    }
    if (name[0] == '\0') {
        return neo_parse_fail(reader, NEO_PARSE_ERROR, "object name must not be empty");
    }
    for (neo_load_node *entry = reader->nodes; entry != NULL; entry = entry->next) {
        if (parent != NULL && entry->object->parent == parent->id &&
            strcmp(entry->object->name, name) == 0) {
            return neo_parse_fail(reader, NEO_CONFLICT, "duplicate contained name");
        }
    }
    neo_object *node = NULL;
    status = neo_node_new(reader->vm, name, (neo_value){0}, &node);
    if (status != NEO_OK) {
        return status;
    }
    neo_load_node *entry = neo_alloc(reader->vm, sizeof(*entry));
    if (entry == NULL) {
        neo_object_free(reader->vm, node);
        return NEO_OUT_OF_MEMORY;
    }
    entry->object = node;
    entry->next = reader->nodes;
    reader->nodes = entry;
    ++reader->count;
    node->parent = parent == NULL ? 0 : parent->id;
    node->image = parent == NULL ? node->id : parent->image;
    neo_space(reader);
    if (*reader->cursor == '#') {
        status = neo_token(reader, token);
        if (status != NEO_OK) {
            return status;
        }
        if (token[1] == '\0') {
            return neo_parse_fail(reader, NEO_PARSE_ERROR, "empty label");
        }
        for (neo_load_node *previous = entry->next; previous != NULL; previous = previous->next) {
            if (previous->label != NULL && strcmp(previous->label, token + 1) == 0) {
                return neo_parse_fail(reader, NEO_CONFLICT, "duplicate label");
            }
        }
        entry->label = neo_string(reader->vm, token + 1);
        if (entry->label == NULL) {
            return NEO_OUT_OF_MEMORY;
        }
    }
    neo_space(reader);
    if (*reader->cursor == ':') {
        status = neo_token(reader, token);
        if (status != NEO_OK) {
            return status;
        }
        neo_value value = {0};
        if (strcmp(token, ":object") == 0) {
            value.kind = NEO_OBJECT;
        } else if (strcmp(token, ":integer") == 0) {
            status = neo_token(reader, token);
            if (status != NEO_OK) {
                return status;
            }
            char *end = NULL;
            errno = 0;
            intmax_t integer = strtoimax(token, &end, 10);
            if (end == token || *end != '\0' || errno != 0 || integer < INT64_MIN || integer > INT64_MAX) {
                return neo_parse_fail(reader, NEO_PARSE_ERROR, "invalid signed 64-bit integer");
            }
            value.kind = NEO_INTEGER;
            value.integer = (int64_t)integer;
        } else if (strcmp(token, ":boolean") == 0) {
            status = neo_token(reader, token);
            if (status != NEO_OK) {
                return status;
            }
            if (strcmp(token, "true") != 0 && strcmp(token, "false") != 0) {
                return neo_parse_fail(reader, NEO_PARSE_ERROR, "expected true or false");
            }
            value.kind = NEO_BOOLEAN;
            value.boolean = strcmp(token, "true") == 0;
        } else if (strcmp(token, ":text") == 0 || strcmp(token, ":primitive") == 0) {
            value.kind = strcmp(token, ":text") == 0 ? NEO_TEXT : NEO_PRIMITIVE;
            status = neo_token(reader, token);
            if (status != NEO_OK) {
                return status;
            }
            value.text = token;
        } else {
            return neo_parse_fail(reader, NEO_PARSE_ERROR, "unknown payload tag");
        }
        status = neo_value_copy(reader->vm, value, &node->value);
        if (status != NEO_OK) {
            return status;
        }
    }
    for (;;) {
        neo_space(reader);
        if (*reader->cursor == ')') {
            (void)neo_advance(reader);
            *out_node = node;
            return NEO_OK;
        }
        if (*reader->cursor == '@') {
            status = neo_read_edge(reader, node);
        } else if (*reader->cursor == '(') {
            neo_object *child = NULL;
            status = neo_read_node(reader, node, depth + 1, &child);
        } else {
            return neo_parse_fail(reader, NEO_PARSE_ERROR, "expected child, edge, or closing parenthesis");
        }
        if (status != NEO_OK) {
            return status;
        }
    }
}

neo_status neo_image_parse(neo_vm *vm, const char *source,
                           const neo_capability **out_root, neo_diagnostic *out_error) {
    if (out_error != NULL) {
        *out_error = (neo_diagnostic){0};
    }
    if (out_root == NULL) {
        return NEO_INVALID;
    }
    *out_root = NULL;
    if (vm == NULL || source == NULL) {
        return NEO_INVALID;
    }
    size_t length = 0;
    while (length <= NEO_SOURCE_LIMIT && source[length] != '\0') {
        ++length;
    }
    if (length > NEO_SOURCE_LIMIT) {
        if (out_error != NULL) {
            out_error->line = 1;
            out_error->column = 1;
            (void)snprintf(out_error->message, sizeof(out_error->message), "source exceeds 1 MiB");
        }
        return NEO_LIMIT;
    }
    neo_reader reader = {.vm = vm, .cursor = source, .line = 1, .column = 1, .error = out_error};
    neo_object *root = NULL;
    neo_status status = neo_read_node(&reader, NULL, 1, &root);
    neo_space(&reader);
    if (status == NEO_OK && *reader.cursor != '\0') {
        status = neo_parse_fail(&reader, NEO_PARSE_ERROR, "unexpected trailing input");
    }
    for (neo_load_edge *pending = reader.edges; status == NEO_OK && pending != NULL; pending = pending->next) {
        neo_load_node *target = reader.nodes;
        while (target != NULL && (target->label == NULL || strcmp(target->label, pending->label) != 0)) {
            target = target->next;
        }
        if (target == NULL) {
            status = neo_parse_fail(&reader, NEO_UNAVAILABLE, "unresolved image-local label");
        } else {
            pending->edge->target = target->object->id;
        }
    }
    if (status == NEO_OK) {
        status = neo_issue(vm, root->id, NEO_ALL, out_root);
    }
    /* All allocations and validation precede publication. */
    while (reader.nodes != NULL) {
        neo_load_node *entry = reader.nodes;
        reader.nodes = entry->next;
        if (status == NEO_OK) {
            entry->object->next = vm->objects;
            vm->objects = entry->object;
        } else {
            neo_object_free(vm, entry->object);
        }
        neo_free(vm, entry->label);
        neo_free(vm, entry);
    }
    while (reader.edges != NULL) {
        neo_load_edge *entry = reader.edges;
        reader.edges = entry->next;
        neo_free(vm, entry->label);
        neo_free(vm, entry);
    }
    if (status != NEO_OK) {
        (void)neo_parse_fail(&reader, status, neo_status_name(status));
    }
    return status;
}

typedef struct neo_writer {
    neo_vm *vm;
    char *text;
    size_t length;
    size_t capacity;
    size_t nodes;
} neo_writer;

static neo_status neo_append(neo_writer *writer, const char *text) {
    size_t length = strlen(text);
    if (writer->length > NEO_SOURCE_LIMIT || length > NEO_SOURCE_LIMIT - writer->length) {
        return NEO_LIMIT;
    }
    size_t needed = writer->length + length + 1;
    if (needed > writer->capacity) {
        size_t capacity = writer->capacity == 0 ? 256 : writer->capacity;
        while (capacity < needed) {
            capacity *= 2;
        }
        char *replacement = neo_alloc(writer->vm, capacity);
        if (replacement == NULL) {
            return NEO_OUT_OF_MEMORY;
        }
        if (writer->text != NULL) {
            memcpy(replacement, writer->text, writer->length);
        }
        neo_free(writer->vm, writer->text);
        writer->text = replacement;
        writer->capacity = capacity;
    }
    memcpy(writer->text + writer->length, text, length + 1);
    writer->length += length;
    return NEO_OK;
}

static neo_status neo_quote(neo_writer *writer, const char *text) {
    if (strlen(text) > NEO_TOKEN_LIMIT) {
        return NEO_LIMIT;
    }
    neo_status status = neo_append(writer, "\"");
    for (const unsigned char *p = (const unsigned char *)text; status == NEO_OK && *p != 0; ++p) {
        char single[2] = {(char)*p, '\0'};
        const char *escaped = single;
        switch (*p) {
            case '\n': escaped = "\\n"; break;
            case '\r': escaped = "\\r"; break;
            case '\t': escaped = "\\t"; break;
            case '\\': escaped = "\\\\"; break;
            case '"': escaped = "\\\""; break;
            default:
                if (*p < 32u) {
                    return NEO_UNSUPPORTED;
                }
        }
        status = neo_append(writer, escaped);
    }
    return status == NEO_OK ? neo_append(writer, "\"") : status;
}

#define NEO_WRITE(expression) do { neo_status write_status = (expression); \
    if (write_status != NEO_OK) { return write_status; } } while (0)

static neo_status neo_write_node(neo_writer *writer, neo_object *node, size_t depth) {
    if (depth > NEO_DEPTH_LIMIT || writer->nodes++ == NEO_NODE_LIMIT) {
        return NEO_LIMIT;
    }
    if (node->ether || node->message != NULL || node->inbox_first != NULL ||
        node->active_message != NULL || neo_actor_has_messages(writer->vm, node->id) ||
        neo_scheduler_contains(writer->vm, node->id)) {
        return NEO_UNSUPPORTED;
    }
    for (size_t i = 1; i < depth; ++i) {
        NEO_WRITE(neo_append(writer, "  "));
    }
    NEO_WRITE(neo_append(writer, "("));
    NEO_WRITE(neo_quote(writer, node->name));
    char buffer[96];
    (void)snprintf(buffer, sizeof(buffer), " #n%" PRIu64, node->id);
    NEO_WRITE(neo_append(writer, buffer));
    switch (node->value.kind) {
        case NEO_OBJECT: NEO_WRITE(neo_append(writer, " :object")); break;
        case NEO_INTEGER:
            (void)snprintf(buffer, sizeof(buffer), " :integer %" PRId64, node->value.integer);
            NEO_WRITE(neo_append(writer, buffer)); break;
        case NEO_BOOLEAN:
            NEO_WRITE(neo_append(writer, node->value.boolean ? " :boolean true" : " :boolean false")); break;
        case NEO_TEXT: case NEO_PRIMITIVE:
            NEO_WRITE(neo_append(writer, node->value.kind == NEO_TEXT ? " :text " : " :primitive "));
            NEO_WRITE(neo_quote(writer, node->value.text)); break;
    }
    for (neo_edge *edge = node->edges; edge != NULL; edge = edge->next) {
        neo_object *target = neo_lookup(writer->vm, edge->target);
        if (target == NULL) {
            return NEO_UNAVAILABLE;
        }
        if (target->image != node->image) {
            return NEO_WRONG_IMAGE;
        }
        NEO_WRITE(neo_append(writer, " @"));
        NEO_WRITE(neo_quote(writer, edge->name));
        (void)snprintf(buffer, sizeof(buffer), " #n%" PRIu64 " %u", target->id, edge->rights);
        NEO_WRITE(neo_append(writer, buffer));
    }
    neo_object *child = neo_child_after(writer->vm, node->id, 0);
    while (child != NULL) {
        NEO_WRITE(neo_append(writer, "\n"));
        NEO_WRITE(neo_write_node(writer, child, depth + 1));
        child = neo_child_after(writer->vm, node->id, child->order);
    }
    return neo_append(writer, ")");
}

neo_status neo_image_format(neo_vm *vm, const neo_capability *root, char **out_text) {
    if (out_text == NULL) {
        return NEO_INVALID;
    }
    *out_text = NULL;
    neo_object *node = NULL;
    neo_status status = neo_resolve(vm, root, NEO_READ, &node);
    if (status != NEO_OK) {
        return status;
    }
    if (node->parent != 0) {
        return NEO_WRONG_KIND;
    }
    neo_writer writer = {.vm = vm};
    status = neo_write_node(&writer, node, 1);
    if (status == NEO_OK) {
        status = neo_append(&writer, "\n");
    }
    if (status == NEO_OK) {
        *out_text = writer.text;
    } else {
        neo_free(vm, writer.text);
    }
    return status;
}

void neo_image_text_free(neo_vm *vm, char *text) {
    if (vm != NULL) {
        neo_free(vm, text);
    }
}
