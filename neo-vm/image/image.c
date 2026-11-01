#include "internal.h"
#include "image/image.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEO_SOURCE_LIMIT NEO_IMAGE_MAX_SOURCE_BYTES
#define NEO_TOKEN_LIMIT NEO_IMAGE_MAX_TOKEN_BYTES
#define NEO_DEPTH_LIMIT NEO_IMAGE_MAX_DEPTH
#define NEO_NODE_LIMIT NEO_IMAGE_MAX_OBJECTS

typedef struct neo_load_grant {
    char *label;
    unsigned rights;
    struct neo_load_grant *next;
} neo_load_grant;

typedef struct neo_load_node {
    neo_object *object;
    char *label;
    char *sender;
    char *receiver;
    uint64_t queue_position;
    neo_load_grant *grants;
    struct neo_load_node *next;
    struct neo_load_node *name_next;
    struct neo_load_node *label_next;
    struct neo_load_node *id_next;
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
    neo_load_node **names;
    neo_load_node **labels;
    neo_load_node **ids;
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
        if (reader->cursor[0] != '/' || reader->cursor[1] != '/') {
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
        if (!quoted && (isspace((unsigned char)c) || c == '(' || c == ')' || c == '[' || c == ']' || (c == '/' && reader->cursor[1] == '/'))) {
            break;
        }
        if (!quoted && c == '"') {
            return neo_parse_fail(reader, NEO_PARSE_ERROR, "strings must be separate payload tokens");
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

/* Names, labels, numeric metadata, and operation bindings are never strings. */
static neo_status neo_bare_token(neo_reader *reader, char *out) {
    neo_space(reader);
    if (*reader->cursor == '"') {
        return neo_parse_fail(reader, NEO_PARSE_ERROR, "expected bare token; quoted text is a payload");
    }
    return neo_token(reader, out);
}

static neo_status neo_runtime_label(neo_reader *reader, char **out) {
    char token[NEO_TOKEN_LIMIT + 1];
    neo_status status = neo_bare_token(reader, token);
    if (status != NEO_OK) { return status; }
    if (token[0] != '#' || token[1] == '\0') { return NEO_PARSE_ERROR; }
    *out = neo_string(reader->vm, token + 1);
    return *out == NULL ? NEO_OUT_OF_MEMORY : NEO_OK;
}

static neo_status neo_runtime_number(neo_reader *reader, uint64_t *out) {
    char token[NEO_TOKEN_LIMIT + 1], *end = NULL;
    neo_status status = neo_bare_token(reader, token);
    if (status != NEO_OK) { return status; }
    errno = 0;
    uintmax_t value = strtoumax(token, &end, 10);
    if (token[0] == '-' || end == token || *end != '\0' || errno != 0 || value > UINT64_MAX) { return NEO_PARSE_ERROR; }
    *out = (uint64_t)value;
    return NEO_OK;
}

static neo_status neo_read_role(neo_reader *reader, neo_load_node *entry) {
    char token[NEO_TOKEN_LIMIT + 1];
    neo_status status = neo_bare_token(reader, token);
    if (status != NEO_OK) { return status; }
    if (strcmp(token, "!ether") == 0) {
        if (entry->object->ether || entry->object->message != NULL) { return NEO_CONFLICT; }
        entry->object->ether = true;
        return NEO_OK;
    }
    if (strcmp(token, "!message") == 0) {
        if (entry->object->message != NULL || entry->object->ether) { return NEO_CONFLICT; }
        neo_message *message = neo_alloc(reader->vm, sizeof(*message));
        if (message == NULL) { return NEO_OUT_OF_MEMORY; }
        entry->object->message = message;
        status = neo_runtime_label(reader, &entry->sender);
        if (status == NEO_OK) { status = neo_runtime_label(reader, &entry->receiver); }
        if (status == NEO_OK) { status = neo_bare_token(reader, token); }
        if (status == NEO_OK) {
            bool found = false;
            for (int state = NEO_MESSAGE_CREATED; state <= NEO_MESSAGE_PROCESSED; ++state) {
                if (strcmp(token, neo_message_state_name((neo_message_state)state)) == 0) {
                    message->state = (neo_message_state)state; found = true; break;
                }
            }
            if (!found) { status = NEO_PARSE_ERROR; }
        }
        if (status == NEO_OK) { status = neo_runtime_number(reader, &message->submission); }
        if (status == NEO_OK) { status = neo_runtime_number(reader, &entry->queue_position); }
        return status;
    }
    if (strcmp(token, "!grant") == 0) {
        neo_load_grant *grant = neo_alloc(reader->vm, sizeof(*grant));
        if (grant == NULL) { return NEO_OUT_OF_MEMORY; }
        grant->next = entry->grants; entry->grants = grant;
        status = neo_runtime_label(reader, &grant->label);
        uint64_t rights = 0;
        if (status == NEO_OK) { status = neo_runtime_number(reader, &rights); }
        if (status == NEO_OK && rights > NEO_MESSAGE_ALL) { status = NEO_INVALID; }
        if (status == NEO_OK) { grant->rights = (unsigned)rights; }
        return status;
    }
    return NEO_PARSE_ERROR;
}

static neo_object *neo_load_label(neo_reader *reader, const char *label) {
    if (label == NULL) { return NULL; }
    for (neo_load_node *entry = reader->labels[neo_name_bucket(0,label)]; entry != NULL; entry = entry->label_next) {
        if (strcmp(label, entry->label) == 0) { return entry->object; }
    }
    return NULL;
}
static neo_object *neo_load_identity(neo_reader *reader, neo_object_id id) {
    for (neo_load_node *entry = reader->ids[id % NEO_INDEX_BUCKETS]; entry != NULL; entry = entry->id_next) {
        if (entry->object->id == id) { return entry->object; }
    }
    return NULL;
}
static bool neo_load_actor(neo_reader *reader, neo_object *node) {
    for (; node != NULL; node = neo_load_identity(reader, node->parent)) {
        if (node->ether || node->message != NULL) { return false; }
    }
    return true;
}

static neo_status neo_restore_roles(neo_reader *reader, neo_object *root) {
    for (neo_load_node *entry = reader->nodes; entry != NULL; entry = entry->next) {
        neo_object *node = entry->object;
        if (node->ether && (node->parent != root->id || strcmp(node->name, "ETHER") != 0 || node->value.kind != NEO_OBJECT)) { return NEO_BAD_STATE; }
        neo_message *message = node->message;
        if (message == NULL) {
            if (entry->grants != NULL) { return NEO_BAD_STATE; }
            continue;
        }
        neo_object *parent = neo_load_identity(reader, node->parent);
        neo_object *sender = neo_load_label(reader, entry->sender);
        neo_object *receiver = neo_load_label(reader, entry->receiver);
        if (parent == NULL || !parent->ether || sender == NULL || receiver == NULL ||
            !neo_load_actor(reader, sender) || !neo_load_actor(reader, receiver)) { return NEO_BAD_STATE; }
        if ((message->state == NEO_MESSAGE_CREATED) != (message->submission == 0) ||
            (message->state == NEO_MESSAGE_ACCEPTED) != (entry->queue_position != 0) ||
            entry->queue_position > reader->count) { return NEO_BAD_STATE; }
        message->object = node->id; message->sender = sender->id; message->receiver = receiver->id;
        for (neo_load_grant *grant = entry->grants; grant != NULL; grant = grant->next) {
            neo_object *actor = neo_load_label(reader, grant->label);
            if (actor == NULL || !neo_load_actor(reader, actor)) { return NEO_BAD_STATE; }
            for (neo_message_grant_entry *known = message->grants; known != NULL; known = known->next) {
                if (known->actor == actor->id) { return NEO_CONFLICT; }
            }
            neo_message_grant_entry *created = neo_alloc(reader->vm, sizeof(*created));
            if (created == NULL) { return NEO_OUT_OF_MEMORY; }
            *created = (neo_message_grant_entry){.actor=actor->id,.rights=grant->rights,.next=message->grants};
            message->grants = created;
        }
        if (message->state == NEO_MESSAGE_PROCESSING) {
            if (receiver->active_message != NULL) { return NEO_CONFLICT; }
            receiver->active_message = message;
        }
    }
    for (neo_load_node *entry = reader->nodes; entry != NULL; entry = entry->next) {
        neo_message *message = entry->object->message;
        if (message == NULL) { continue; }
        for (neo_load_node *other = reader->nodes; other != NULL; other = other->next) {
            neo_message *previous = other->object->message;
            if (previous == NULL || previous == message) { continue; }
            if (message->submission != 0 && previous->submission == message->submission) { return NEO_CONFLICT; }
            if (message->receiver != previous->receiver) { continue; }
            if (entry->queue_position != 0 && entry->queue_position == other->queue_position) { return NEO_CONFLICT; }
            if (message->sender == previous->sender && previous->submission < message->submission && previous->submission != 0) {
                if (previous->state == NEO_MESSAGE_PENDING && message->state >= NEO_MESSAGE_ACCEPTED) { return NEO_BAD_STATE; }
                if (previous->state == NEO_MESSAGE_ACCEPTED && message->state == NEO_MESSAGE_PROCESSING) { return NEO_BAD_STATE; }
                if (previous->state == NEO_MESSAGE_ACCEPTED && message->state == NEO_MESSAGE_ACCEPTED && other->queue_position > entry->queue_position) { return NEO_BAD_STATE; }
            }
        }
        if (entry->queue_position != 0) {
            neo_object *receiver = neo_load_identity(reader, message->receiver);
            if (entry->queue_position == 1) { receiver->inbox_first = message; }
            else {
                neo_message *previous = NULL;
                for (neo_load_node *other = reader->nodes; other != NULL; other = other->next) {
                    if (other->object->message != NULL && other->object->message->receiver == message->receiver && other->queue_position + 1 == entry->queue_position) {
                        previous = other->object->message; break;
                    }
                }
                if (previous == NULL) { return NEO_BAD_STATE; }
                previous->queue_next = message;
            }
        }
    }
    for (neo_load_node *entry = reader->nodes; entry != NULL; entry = entry->next) {
        neo_object *node = entry->object;
        node->inbox_last = node->inbox_first;
        while (node->inbox_last != NULL && node->inbox_last->queue_next != NULL) { node->inbox_last = node->inbox_last->queue_next; }
    }
    return NEO_OK;
}

/* A bracket literal is one packed payload, not a child per component. */
static neo_status neo_read_array(neo_reader *reader, neo_value *out) {
    (void)neo_advance(reader);
    int64_t *data = NULL;
    size_t count = 0, capacity = 0;
    neo_status status = NEO_OK;
    for (;;) {
        neo_space(reader);
        if (*reader->cursor == ']') { (void)neo_advance(reader); break; }
        char token[NEO_TOKEN_LIMIT + 1];
        status = neo_bare_token(reader, token);
        if (status != NEO_OK) { break; }
        char *end = NULL;
        errno = 0;
        intmax_t number = strtoimax(token, &end, 10);
        if (end == token || *end != '\0' || errno != 0 || number < INT64_MIN || number > INT64_MAX) {
            status = neo_parse_fail(reader, NEO_PARSE_ERROR, "expected array integer or ]"); break;
        }
        if (count == capacity) {
            size_t next = capacity == 0 ? 16 : capacity * 2;
            if (next > NEO_SOURCE_LIMIT / sizeof(*data)) { status = NEO_LIMIT; break; }
            int64_t *grown = neo_alloc(reader->vm, next * sizeof(*data));
            if (grown == NULL) { status = NEO_OUT_OF_MEMORY; break; }
            if (count != 0) { memcpy(grown, data, count * sizeof(*data)); }
            neo_free(reader->vm, data);
            data = grown; capacity = next;
        }
        data[count++] = (int64_t)number;
    }
    if (status != NEO_OK) { neo_free(reader->vm, data); return status; }
    *out = (neo_value){.kind = NEO_INTEGERS, .integers = data, .count = count};
    return NEO_OK;
}

static neo_status neo_read_edge(neo_reader *reader, neo_object *owner) {
    char name[NEO_TOKEN_LIMIT + 1], label[NEO_TOKEN_LIMIT + 1], number[NEO_TOKEN_LIMIT + 1];
    (void)neo_advance(reader); /* @ */
    neo_status status = neo_bare_token(reader, name);
    if (status == NEO_OK) {
        status = neo_bare_token(reader, label);
    }
    if (status == NEO_OK) {
        status = neo_bare_token(reader, number);
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
    neo_status status = neo_bare_token(reader, name);
    if (status != NEO_OK) {
        return status;
    }
    if (name[0] == '\0') {
        return neo_parse_fail(reader, NEO_PARSE_ERROR, "object name must not be empty");
    }
    size_t name_bucket=neo_name_bucket(parent == NULL ? 0 : parent->id,name);
    for (neo_load_node *entry = reader->names[name_bucket]; entry != NULL; entry = entry->name_next) {
        if (parent != NULL && !(parent->value.kind == NEO_PRIMITIVE &&
            strcmp(parent->value.text, "do") == 0) && entry->object->parent == parent->id &&
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
    entry->name_next=reader->names[name_bucket]; reader->names[name_bucket]=entry;
    size_t id_bucket=node->id % NEO_INDEX_BUCKETS;
    entry->id_next=reader->ids[id_bucket]; reader->ids[id_bucket]=entry;
    neo_space(reader);
    if (*reader->cursor == '#') {
        status = neo_token(reader, token);
        if (status != NEO_OK) {
            return status;
        }
        if (token[1] == '\0') {
            return neo_parse_fail(reader, NEO_PARSE_ERROR, "empty label");
        }
        if (neo_load_label(reader,token+1) != NULL) { return neo_parse_fail(reader,NEO_CONFLICT,"duplicate label"); }
        entry->label = neo_string(reader->vm, token + 1);
        if (entry->label == NULL) {
            return NEO_OUT_OF_MEMORY;
        }
        size_t label_bucket=neo_name_bucket(0,entry->label);
        entry->label_next=reader->labels[label_bucket]; reader->labels[label_bucket]=entry;
    }
    neo_space(reader);
    bool tagged = *reader->cursor == ':';
    if (tagged) {
        status = neo_token(reader, token);
        if (status != NEO_OK) {
            return status;
        }
        neo_value value = {0};
        if (strcmp(token, ":object") == 0) {
            value.kind = NEO_OBJECT;
        } else if (strcmp(token, ":integer") == 0) {
            status = neo_bare_token(reader, token);
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
            status = neo_bare_token(reader, token);
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
            neo_space(reader);
            if (value.kind == NEO_PRIMITIVE &&
                (*reader->cursor == '(' || *reader->cursor == ')' ||
                 *reader->cursor == '@' || *reader->cursor == '!' || *reader->cursor == '\0')) {
                value.text = name;
            } else {
                status = value.kind == NEO_PRIMITIVE
                    ? neo_bare_token(reader, token) : neo_token(reader, token);
                if (status != NEO_OK) {
                    return status;
                }
                value.text = token;
            }
        } else {
            return neo_parse_fail(reader, NEO_PARSE_ERROR, "unknown payload tag");
        }
        status = neo_value_copy(reader->vm, value, &node->value);
        if (status != NEO_OK) {
            return status;
        }
    }
    neo_space(reader);
    if (*reader->cursor == '[') {
        if (tagged) { return neo_parse_fail(reader, NEO_PARSE_ERROR, "unexpected array payload"); }
        status = neo_read_array(reader, &node->value);
        if (status != NEO_OK) { return status; }
        tagged = true; /* A second payload is forbidden, including another array. */
        neo_space(reader);
    }
    if (*reader->cursor != '(' && *reader->cursor != ')' &&
        *reader->cursor != '@' && *reader->cursor != '!' && *reader->cursor != '\0') {
        /* Inferred literals are values, never permanent field declarations. */
        if (tagged) {
            return neo_parse_fail(reader, NEO_PARSE_ERROR, "unexpected extra payload");
        }
        bool quoted = *reader->cursor == '"';
        status = neo_token(reader, token);
        if (status != NEO_OK) { return status; }
        neo_value value = {0};
        if (quoted) {
            value.kind = NEO_TEXT;
            value.text = token;
        } else if (strcmp(token, "true") == 0 || strcmp(token, "false") == 0) {
            value.kind = NEO_BOOLEAN;
            value.boolean = strcmp(token, "true") == 0;
        } else {
            char *end = NULL;
            errno = 0;
            intmax_t integer = strtoimax(token, &end, 10);
            if (end == token || *end != '\0' || errno != 0 || integer < INT64_MIN || integer > INT64_MAX) {
                return neo_parse_fail(reader, NEO_PARSE_ERROR, "expected integer, boolean, or quoted text literal");
            }
            value.kind = NEO_INTEGER;
            value.integer = (int64_t)integer;
        }
        status = neo_value_copy(reader->vm, value, &node->value);
        if (status != NEO_OK) { return status; }
    }
    if (!tagged && node->value.kind == NEO_OBJECT && neo_primitive_known(name)) {
        status = neo_value_copy(reader->vm,
            (neo_value){.kind = NEO_PRIMITIVE, .text = name}, &node->value);
        if (status != NEO_OK) { return status; }
    }
    for (;;) {
        neo_space(reader);
        if (*reader->cursor == ')') {
            (void)neo_advance(reader);
            *out_node = node;
            return NEO_OK;
        }
        if (*reader->cursor == '!') {
            status = neo_read_role(reader, entry);
        } else if (*reader->cursor == '@') {
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
            (void)snprintf(out_error->message, sizeof(out_error->message), "source exceeds profile limit");
        }
        return NEO_LIMIT;
    }
    neo_reader reader = {.vm = vm, .cursor = source, .line = 1, .column = 1, .error = out_error};
    reader.names=neo_alloc(vm,3u*NEO_INDEX_BUCKETS*sizeof(*reader.names));
    if (reader.names == NULL) { return neo_parse_fail(&reader,NEO_OUT_OF_MEMORY,"out of memory"); }
    reader.labels=reader.names+NEO_INDEX_BUCKETS;
    reader.ids=reader.labels+NEO_INDEX_BUCKETS;
    neo_object *root = NULL;
    neo_status status = NEO_OK;
    neo_space(&reader);
    if (*reader.cursor == '!') {
        char marker[NEO_TOKEN_LIMIT + 1];
        status = neo_bare_token(&reader, marker);
        if (status == NEO_OK && strcmp(marker, "!neo-checkpoint") != 0) {
            status = neo_parse_fail(&reader, NEO_PARSE_ERROR, "expected !neo-checkpoint version");
        }
        uint64_t version = 0;
        if (status == NEO_OK) { status = neo_runtime_number(&reader, &version); }
        if (status == NEO_OK && version != 1) {
            status = neo_parse_fail(&reader, NEO_UNSUPPORTED, "unsupported checkpoint version");
        }
    }
    if (status == NEO_OK) { status = neo_read_node(&reader, NULL, 1, &root); }
    neo_space(&reader);
    if (status == NEO_OK && *reader.cursor != '\0') {
        status = neo_parse_fail(&reader, NEO_PARSE_ERROR, "unexpected trailing input");
    }
    for (neo_load_edge *pending = reader.edges; status == NEO_OK && pending != NULL; pending = pending->next) {
        neo_object *target = neo_load_label(&reader,pending->label);
        if (target == NULL) {
            status = neo_parse_fail(&reader, NEO_UNAVAILABLE, "unresolved image-local label");
        } else {
            pending->edge->target = target->id;
        }
    }
    if (status == NEO_OK) { status = neo_restore_roles(&reader, root); }
    if (status == NEO_OK) {
        status = neo_issue(vm, root->id, NEO_ALL, out_root);
    }
    /* All allocations and validation precede publication. */
    while (reader.nodes != NULL) {
        neo_load_node *entry = reader.nodes;
        reader.nodes = entry->next;
        if (status == NEO_OK) {
            neo_node_publish(vm, entry->object);
            neo_message *message = entry->object->message;
            if (message != NULL) {
                message->next = vm->messages; vm->messages = message;
                if (message->submission > vm->next_submission) { vm->next_submission = message->submission; }
            }
        } else {
            neo_message *message = entry->object->message;
            if (message != NULL) {
                while (message->grants != NULL) {
                    neo_message_grant_entry *next = message->grants->next;
                    neo_free(vm, message->grants); message->grants = next;
                }
                neo_free(vm, message);
            }
            neo_object_free(vm, entry->object);
        }
        neo_free(vm, entry->label);
        neo_free(vm, entry->sender); neo_free(vm, entry->receiver);
        while (entry->grants != NULL) {
            neo_load_grant *next = entry->grants->next;
            neo_free(vm, entry->grants->label); neo_free(vm, entry->grants); entry->grants = next;
        }
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
    neo_free(vm,reader.names);
    return status;
}

typedef struct neo_writer {
    neo_vm *vm;
    char *text;
    size_t length;
    size_t capacity;
    size_t nodes;
    bool checkpoint;
    bool editable;
    neo_object_id *labels;
    size_t label_count;
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

/* Host names that need quoting have no spelling in this image syntax. */
static neo_status neo_write_bare(neo_writer *writer, const char *text) {
    size_t length = strlen(text);
    if (length == 0) { return NEO_UNSUPPORTED; }
    if (length > NEO_TOKEN_LIMIT) { return NEO_LIMIT; }
    for (const unsigned char *p = (const unsigned char *)text; *p != 0; ++p) {
        if (isspace(*p) || *p < 32u || *p == '(' || *p == ')' || *p == '[' || *p == ']' || *p == '"' ||
            (*p == '/' && p[1] == '/')) {
            return NEO_UNSUPPORTED;
        }
    }
    return neo_append(writer, text);
}

#define NEO_WRITE(expression) do { neo_status write_status = (expression); \
    if (write_status != NEO_OK) { return write_status; } } while (0)

static neo_status neo_write_node(neo_writer *writer, neo_object *node, size_t depth) {
    if (depth > NEO_DEPTH_LIMIT || writer->nodes++ == NEO_NODE_LIMIT) {
        return NEO_LIMIT;
    }
    /* A live edit can turn an ordered sequence into ordinary data. Reject an
     * ambiguous ordinary namespace instead of writing a file we cannot reload. */
    if (!(node->value.kind == NEO_PRIMITIVE && strcmp(node->value.text, "do") == 0)) {
        /* Uniqueness needs no ordered pair traversal. Repeated child_after
         * inside both loops made a wide namespace cubic to encode. */
        for (neo_object *child = writer->vm->parent_index[node->id % NEO_INDEX_BUCKETS]; child != NULL;
             child = child->parent_next) {
            if (child->parent == node->id && neo_child_named(writer->vm, node->id, child->name) != child) {
                return NEO_CONFLICT;
            }
        }
    }
    if (node->stream != NULL || node->display != NULL || neo_scheduler_contains(writer->vm, node->id) ||
        (!writer->checkpoint && (node->ether || node->message != NULL || node->inbox_first != NULL ||
        node->active_message != NULL || neo_actor_has_messages(writer->vm, node->id)))) {
        return NEO_UNSUPPORTED;
    }
    for (size_t i = 1; i < depth; ++i) {
        NEO_WRITE(neo_append(writer, "  "));
    }
    NEO_WRITE(neo_append(writer, "("));
    NEO_WRITE(neo_write_bare(writer, node->name));
    char buffer[96];
    bool labelled=!writer->editable;
    for (size_t i=0; writer->editable && i<writer->label_count; ++i) { if (writer->labels[i] == node->id) { labelled=true; break; } }
    if (labelled) {
        (void)snprintf(buffer, sizeof(buffer), " #n%" PRIu64, node->id);
        NEO_WRITE(neo_append(writer, buffer));
    }
    switch (node->value.kind) {
        case NEO_REFERENCE: return NEO_UNSUPPORTED;
        case NEO_OBJECT:
            /* Preserve data objects whose names also name built-in operations. */
            if (neo_primitive_known(node->name)) {
                NEO_WRITE(neo_append(writer, " :object"));
            }
            break;
        case NEO_INTEGERS:
            NEO_WRITE(neo_append(writer, " ["));
            for (size_t i = 0; i < node->value.count; ++i) {
                (void)snprintf(buffer, sizeof(buffer), "%s%" PRId64, i == 0 ? "" : " ", node->value.integers[i]);
                NEO_WRITE(neo_append(writer, buffer));
            }
            NEO_WRITE(neo_append(writer, "]")); break;
        case NEO_INTEGER:
            (void)snprintf(buffer, sizeof(buffer), " %" PRId64, node->value.integer);
            NEO_WRITE(neo_append(writer, buffer)); break;
        case NEO_BOOLEAN:
            NEO_WRITE(neo_append(writer, node->value.boolean ? " true" : " false")); break;
        case NEO_TEXT:
            NEO_WRITE(neo_append(writer, " "));
            NEO_WRITE(neo_quote(writer, node->value.text)); break;
        case NEO_PRIMITIVE:
            if (strcmp(node->name, node->value.text) == 0 && neo_primitive_known(node->name)) {
                break;
            }
            NEO_WRITE(neo_append(writer, " :primitive"));
            if (strcmp(node->name, node->value.text) != 0) {
                /* Preserve host-created objects with distinct operation bindings. */
                NEO_WRITE(neo_append(writer, " "));
                NEO_WRITE(neo_write_bare(writer, node->value.text));
            }
            break;
    }
    if (writer->checkpoint && node->ether) { NEO_WRITE(neo_append(writer, " !ether")); }
    if (writer->checkpoint && node->message != NULL) {
        neo_message *message = node->message;
        neo_object *sender = neo_lookup(writer->vm, message->sender);
        neo_object *receiver = neo_lookup(writer->vm, message->receiver);
        if (sender == NULL || receiver == NULL) { return NEO_UNAVAILABLE; }
        if (sender->image != node->image || receiver->image != node->image) { return NEO_WRONG_IMAGE; }
        uint64_t position = 0;
        if (message->state == NEO_MESSAGE_ACCEPTED) {
            neo_message *queued = receiver->inbox_first;
            while (queued != NULL && queued != message && position <= NEO_NODE_LIMIT) { ++position; queued = queued->queue_next; }
            if (queued != message || position > NEO_NODE_LIMIT) { return NEO_BAD_STATE; }
            ++position;
        }
        char role[192];
        (void)snprintf(role, sizeof(role), " !message #n%" PRIu64 " #n%" PRIu64 " %s %" PRIu64 " %" PRIu64,
                       message->sender, message->receiver, neo_message_state_name(message->state), message->submission, position);
        NEO_WRITE(neo_append(writer, role));
        for (neo_message_grant_entry *grant = message->grants; grant != NULL; grant = grant->next) {
            neo_object *actor = neo_lookup(writer->vm, grant->actor);
            if (actor == NULL) { return NEO_UNAVAILABLE; }
            if (actor->image != node->image) { return NEO_WRONG_IMAGE; }
            (void)snprintf(role, sizeof(role), " !grant #n%" PRIu64 " %u", grant->actor, grant->rights);
            NEO_WRITE(neo_append(writer, role));
        }
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
        NEO_WRITE(neo_write_bare(writer, edge->name));
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

static neo_status neo_image_encode(neo_vm *vm, const neo_capability *root, bool checkpoint, char **out_text) {
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
    if (checkpoint && vm->active_invocations != 0) { return NEO_BUSY; }
    neo_writer writer = {.vm = vm, .checkpoint = checkpoint};
    if (checkpoint) { status = neo_append(&writer, "!neo-checkpoint 1\n"); }
    if (status == NEO_OK) { status = neo_write_node(&writer, node, 1); }
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

neo_status neo_image_format(neo_vm *vm, const neo_capability *root, char **out_text) {
    return neo_image_encode(vm, root, false, out_text);
}
neo_status neo_image_checkpoint(neo_vm *vm, const neo_capability *root, char **out_text) {
    return neo_image_encode(vm, root, true, out_text);
}

neo_status neo_image_source(neo_vm *vm, const neo_capability *target, char **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out=NULL; neo_object *root=NULL;
    neo_status status=neo_resolve(vm,target,NEO_READ,&root);
    if (status != NEO_OK) { return status; }
    size_t edges=0;
    for (neo_object *node=vm->objects; node != NULL; node=node->next) {
        if (!neo_inside(vm,node,root->id)) { continue; }
        for (neo_edge *edge=node->edges; edge != NULL; edge=edge->next) {
            neo_object *destination=neo_lookup(vm,edge->target);
            if (destination == NULL || !neo_inside(vm,destination,root->id)) { return NEO_UNSUPPORTED; }
            ++edges;
        }
    }
    if (edges > SIZE_MAX/sizeof(neo_object_id)) { return NEO_LIMIT; }
    neo_writer writer={.vm=vm,.editable=true};
    if (edges != 0) {
        writer.labels=neo_alloc(vm,edges*sizeof(*writer.labels));
        if (writer.labels == NULL) { return NEO_OUT_OF_MEMORY; }
        for (neo_object *node=vm->objects; node != NULL; node=node->next) {
            if (!neo_inside(vm,node,root->id)) { continue; }
            for (neo_edge *edge=node->edges; edge != NULL; edge=edge->next) { writer.labels[writer.label_count++]=edge->target; }
        }
    }
    status=neo_write_node(&writer,root,1);
    if (status == NEO_OK) { status=neo_append(&writer,"\n"); }
    neo_free(vm,writer.labels);
    if (status == NEO_OK) { *out=writer.text; } else { neo_free(vm,writer.text); }
    return status;
}

neo_status neo_image_apply_source(neo_vm *vm, const neo_capability *target, const char *source,
                                  const char *expected, neo_diagnostic *error) {
    if (source == NULL || expected == NULL) { return NEO_INVALID; }
    neo_object *old=NULL;
    neo_status status=neo_resolve(vm,target,NEO_READ|NEO_WRITE|NEO_INSERT|NEO_DELETE,&old);
    char *current=NULL;
    if (status == NEO_OK) { status=neo_image_source(vm,target,&current); }
    if (status == NEO_OK && strcmp(current,expected) != 0) { status=NEO_CONFLICT; }
    neo_free(vm,current);
    if (status != NEO_OK) { return status; }
    const neo_capability *staged=NULL;
    status=neo_image_parse(vm,source,&staged,error);
    if (status != NEO_OK) { return status; }
    neo_object *replacement=neo_lookup(vm,staged->target);
    if (strcmp(old->name,replacement->name) != 0) { status=NEO_INVALID; }
    for (neo_object *node=vm->objects; status == NEO_OK && node != NULL; node=node->next) {
        if (node->image != replacement->image && !neo_inside(vm,node,old->id)) { continue; }
        if (node->stream != NULL || node->display != NULL || node->message != NULL || node->ether || node->inbox_first != NULL ||
            node->active_message != NULL || neo_actor_has_messages(vm,node->id) || neo_scheduler_contains(vm,node->id)) { status=NEO_UNSUPPORTED; }
    }
    if (status == NEO_OK) {
        /* No allocation or behavior runs after this point. Staged identities are
         * not exposed to image behavior before adoption. */
        neo_object *child=NULL;
        while ((child=neo_child_after(vm,old->id,0)) != NULL) { neo_delete_region(vm,child->id); }
        neo_value_free(vm,old->value); old->value=replacement->value; replacement->value=(neo_value){0};
        while (old->edges != NULL) { neo_edge *next=old->edges->next; neo_free(vm,old->edges->name); neo_free(vm,old->edges); old->edges=next; }
        old->edges=replacement->edges; replacement->edges=NULL;
        neo_object_id staged_image=replacement->image;
        for (neo_object *node=vm->objects; node != NULL; node=node->next) {
            if (node != replacement && node->image != staged_image) { continue; }
            if (node != replacement) { neo_node_reparent_staged(vm,node,node->parent == replacement->id ? old->id : node->parent,old->image); }
            neo_edge *edges=node == replacement ? old->edges : node->edges;
            for (neo_edge *edge=edges; edge != NULL; edge=edge->next) { if (edge->target == replacement->id) { edge->target=old->id; } }
        }
    }
    (void)neo_image_unload(vm,staged);
    return status;
}
