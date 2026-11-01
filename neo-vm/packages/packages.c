#include "internal.h"
#include "packages/packages.h"
#include <string.h>

typedef struct neo_package_view {
    neo_object *root, *manifest, *contents, *dependencies, *exports, *imports, *bindings;
    const char *id;
} neo_package_view;

static neo_object *neo_pkg_child(neo_vm *vm, neo_object *node, const char *name) {
    return node == NULL ? NULL : neo_child_named(vm, node->id, name);
}
static const char *neo_pkg_text(neo_vm *vm, neo_object *node, const char *name) {
    neo_object *field = neo_pkg_child(vm, node, name);
    return field != NULL && field->value.kind == NEO_TEXT ? field->value.text : NULL;
}
static bool neo_pkg_integer(neo_vm *vm, neo_object *node, const char *name, int64_t *out) {
    neo_object *field = neo_pkg_child(vm, node, name);
    if (field == NULL || field->value.kind != NEO_INTEGER || field->value.integer < 0) { return false; }
    *out = field->value.integer; return true;
}
static bool neo_pkg_shape(neo_vm *vm, neo_object *node, const char *const *fields, size_t count) {
    if (node == NULL || node->value.kind != NEO_OBJECT || node->edges != NULL) { return false; }
    size_t actual = 0;
    for (neo_object *child = neo_child_after(vm, node->id, 0); child != NULL; child = neo_child_after(vm, node->id, child->order)) { ++actual; }
    if (actual != count) { return false; }
    for (size_t i = 0; i < count; ++i) { if (neo_pkg_child(vm, node, fields[i]) == NULL) { return false; } }
    return true;
}
static bool neo_pkg_name(const char *name, bool identity) {
    bool first = true;
    if (name == NULL || *name == '\0') { return false; }
    for (const char *p = name; *p != '\0'; ++p) {
        if (identity && *p == '.') { if (first) { return false; } first = true; continue; }
        bool letter = (*p >= 'a' && *p <= 'z') || (!identity && *p >= 'A' && *p <= 'Z');
        if (first ? !(letter || (!identity && *p == '_')) : !(letter || (*p >= '0' && *p <= '9') || *p == '-' || (!identity && *p == '_'))) { return false; }
        first = false;
    }
    return !first;
}
static bool neo_pkg_version(neo_vm *vm, neo_object *node, int64_t out[3]) {
    static const char *const fields[] = {"major", "minor", "patch"};
    if (!neo_pkg_shape(vm, node, fields, 3)) { return false; }
    for (size_t i = 0; i < 3; ++i) { if (!neo_pkg_integer(vm, node, fields[i], &out[i])) { return false; } }
    return true;
}
static neo_object *neo_pkg_path(neo_vm *vm, neo_object *root, const char *path) {
    if (path == NULL || strncmp(path, "contents/", 9) != 0) { return NULL; }
    neo_object *node = neo_pkg_child(vm, root, "contents");
    const char *p = path + 9;
    while (*p != '\0') {
        const char *end = strchr(p, '/');
        size_t length = end == NULL ? strlen(p) : (size_t)(end-p);
        if (length == 0 || length > 4096) { return NULL; }
        char segment[4097]; memcpy(segment, p, length); segment[length] = '\0';
        if (!neo_pkg_name(segment, false) && !neo_primitive_known(segment)) { return NULL; }
        node = neo_pkg_child(vm, node, segment);
        if (node == NULL) { return NULL; }
        if (end == NULL) { return node; }
        p = end + 1;
    }
    return NULL;
}
static neo_edge *neo_pkg_edge(neo_object *node, const char *name) {
    for (neo_edge *edge = node->edges; edge != NULL; edge = edge->next) { if (strcmp(edge->name, name) == 0) { return edge; } }
    return NULL;
}
static neo_object *neo_pkg_binding(neo_vm *vm, neo_package_view *view, neo_object *owner, const char *name) {
    for (neo_object *b = neo_child_after(vm, view->bindings->id, 0); b != NULL; b = neo_child_after(vm, view->bindings->id, b->order)) {
        if (neo_pkg_path(vm, view->root, neo_pkg_text(vm, b, "path")) == owner && strcmp(neo_pkg_text(vm, b, "name"), name) == 0) { return b; }
    }
    return NULL;
}

static neo_status neo_pkg_schema(neo_vm *vm, neo_object *root, bool bound, neo_package_view *out) {
    static const char *const top[] = {"manifest", "contents"};
    static const char *const fields[] = {"schema", "id", "version", "dependencies", "exports", "imports", "bindings"};
    static const char *const dep_fields[] = {"id", "version"};
    static const char *const export_fields[] = {"path", "mode", "rights"};
    static const char *const import_fields[] = {"dependency", "export", "rights"};
    static const char *const bind_fields[] = {"import", "path", "name"};
    if (!neo_pkg_shape(vm, root, top, 2)) { return NEO_INVALID; }
    neo_object *manifest = neo_pkg_child(vm, root, "manifest");
    if (!neo_pkg_shape(vm, manifest, fields, 7)) { return NEO_INVALID; }
    int64_t schema = 0, version[3];
    if (!neo_pkg_integer(vm, manifest, "schema", &schema)) { return NEO_INVALID; }
    if (schema != 1) { return NEO_UNSUPPORTED; }
    *out = (neo_package_view){.root=root,.manifest=manifest,.contents=neo_pkg_child(vm, root, "contents"),
        .dependencies=neo_pkg_child(vm, manifest, "dependencies"),.exports=neo_pkg_child(vm, manifest, "exports"),
        .imports=neo_pkg_child(vm, manifest, "imports"),.bindings=neo_pkg_child(vm, manifest, "bindings"),.id=neo_pkg_text(vm, manifest, "id")};
    if (!neo_pkg_name(out->id, true) || !neo_pkg_version(vm, neo_pkg_child(vm, manifest, "version"), version)) { return NEO_INVALID; }
    neo_object *containers[] = {out->contents, out->dependencies, out->exports, out->imports, out->bindings};
    for (size_t i = 0; i < 5; ++i) { if (containers[i]->value.kind != NEO_OBJECT || containers[i]->edges != NULL) { return NEO_INVALID; } }
    for (neo_object *d = neo_child_after(vm, out->dependencies->id, 0); d != NULL; d = neo_child_after(vm, out->dependencies->id, d->order)) {
        if (!neo_pkg_shape(vm, d, dep_fields, 2) || !neo_pkg_name(neo_pkg_text(vm, d, "id"), true) ||
            !neo_pkg_version(vm, neo_pkg_child(vm, d, "version"), version)) { return NEO_INVALID; }
        for (neo_object *other = neo_child_after(vm, out->dependencies->id, d->order); other != NULL; other = neo_child_after(vm, out->dependencies->id, other->order)) {
            const char *id = neo_pkg_text(vm, other, "id");
            if (id != NULL && strcmp(id, neo_pkg_text(vm, d, "id")) == 0) { return NEO_CONFLICT; }
        }
    }
    for (neo_object *e = neo_child_after(vm, out->exports->id, 0); e != NULL; e = neo_child_after(vm, out->exports->id, e->order)) {
        int64_t rights = 0;
        if (!neo_pkg_shape(vm, e, export_fields, 3) || !neo_pkg_integer(vm, e, "rights", &rights) || rights > NEO_ALL) { return NEO_INVALID; }
        const char *mode = neo_pkg_text(vm, e, "mode");
        if (mode == NULL || (strcmp(mode, "copy") != 0 && strcmp(mode, "service") != 0)) { return NEO_INVALID; }
        unsigned required = strcmp(mode, "copy") == 0 ? NEO_READ | NEO_COPY : NEO_READ;
        if (((unsigned)rights & required) != required) { return NEO_DENIED; }
        neo_object *target = neo_pkg_path(vm, root, neo_pkg_text(vm, e, "path"));
        if (target == NULL) { return NEO_UNAVAILABLE; }
        for (neo_object *other = neo_child_after(vm, out->exports->id, e->order); other != NULL; other = neo_child_after(vm, out->exports->id, other->order)) {
            neo_object *second = neo_pkg_path(vm, root, neo_pkg_text(vm, other, "path"));
            if (second != NULL && (neo_inside(vm, second, target->id) || neo_inside(vm, target, second->id))) { return NEO_CONFLICT; }
        }
    }
    for (neo_object *i = neo_child_after(vm, out->imports->id, 0); i != NULL; i = neo_child_after(vm, out->imports->id, i->order)) {
        int64_t rights = 0;
        if (!neo_pkg_shape(vm, i, import_fields, 3) || !neo_pkg_integer(vm, i, "rights", &rights) || rights > NEO_ALL ||
            neo_pkg_child(vm, out->dependencies, neo_pkg_text(vm, i, "dependency") == NULL ? "" : neo_pkg_text(vm, i, "dependency")) == NULL ||
            !neo_pkg_name(neo_pkg_text(vm, i, "export"), false)) { return NEO_INVALID; }
        bool used = false;
        for (neo_object *b = neo_child_after(vm, out->bindings->id, 0); b != NULL; b = neo_child_after(vm, out->bindings->id, b->order)) {
            const char *alias = neo_pkg_text(vm, b, "import");
            if (alias != NULL && strcmp(alias, i->name) == 0) { used = true; }
        }
        if (!used) { return NEO_INVALID; }
    }
    for (neo_object *b = neo_child_after(vm, out->bindings->id, 0); b != NULL; b = neo_child_after(vm, out->bindings->id, b->order)) {
        if (!neo_pkg_shape(vm, b, bind_fields, 3)) { return NEO_INVALID; }
        const char *alias = neo_pkg_text(vm, b, "import"), *name = neo_pkg_text(vm, b, "name");
        neo_object *owner = neo_pkg_path(vm, root, neo_pkg_text(vm, b, "path"));
        if (alias == NULL || !neo_pkg_name(name, false) || owner == NULL || neo_pkg_child(vm, out->imports, alias) == NULL) { return NEO_INVALID; }
        if ((neo_pkg_edge(owner, name) != NULL) != bound) { return NEO_CONFLICT; }
        for (neo_object *other = neo_child_after(vm, out->bindings->id, b->order); other != NULL; other = neo_child_after(vm, out->bindings->id, other->order)) {
            const char *other_name = neo_pkg_text(vm, other, "name");
            if (other_name != NULL && strcmp(other_name, name) == 0 && neo_pkg_path(vm, root, neo_pkg_text(vm, other, "path")) == owner) { return NEO_CONFLICT; }
        }
    }
    size_t count = 0;
    for (neo_object *node = vm->objects; node != NULL; node = node->next) {
        if (!neo_inside(vm, node, root->id)) { continue; }
        if (++count > NEO_IMAGE_MAX_OBJECTS) { return NEO_LIMIT; }
        size_t depth = 0;
        for (neo_object *parent = node; parent != NULL && parent != root; parent = neo_lookup(vm, parent->parent)) { if (++depth > NEO_IMAGE_MAX_DEPTH) { return NEO_LIMIT; } }
        if ((!neo_pkg_name(node->name, false) && !(node->value.kind == NEO_PRIMITIVE && neo_primitive_known(node->name))) ||
            node->value.kind == NEO_REFERENCE || node->ether || node->message != NULL || node->stream != NULL || node->display != NULL ||
            neo_actor_has_messages(vm, node->id) || neo_scheduler_contains(vm, node->id)) { return NEO_UNSUPPORTED; }
        if (neo_inside(vm, node, manifest->id) && (node->value.kind == NEO_PRIMITIVE || node->edges != NULL)) { return NEO_INVALID; }
        if (neo_inside(vm, node, manifest->id) && node->value.kind != NEO_OBJECT && neo_child_after(vm, node->id, 0) != NULL) { return NEO_INVALID; }
        for (neo_edge *edge = node->edges; edge != NULL; edge = edge->next) {
            neo_object *target = neo_lookup(vm, edge->target);
            bool imported = neo_pkg_binding(vm, out, node, edge->name) != NULL;
            if (target == NULL) { return NEO_UNAVAILABLE; }
            if (!neo_inside(vm, target, out->contents->id) && !(bound && imported)) { return NEO_WRONG_IMAGE; }
            for (neo_object *e = neo_child_after(vm, out->exports->id, 0); e != NULL; e = neo_child_after(vm, out->exports->id, e->order)) {
                neo_object *exported = neo_pkg_path(vm, root, neo_pkg_text(vm, e, "path"));
                if (strcmp(neo_pkg_text(vm, e, "mode"), "copy") == 0 && neo_inside(vm, node, exported->id) &&
                    !neo_inside(vm, target, exported->id) && !(bound && imported)) { return NEO_WRONG_IMAGE; }
            }
        }
    }
    return NEO_OK;
}

neo_status neo_package_validate(neo_vm *vm, const neo_capability *bundle) {
    neo_object *root = NULL;
    neo_status status = neo_resolve(vm, bundle, NEO_READ, &root);
    neo_package_view view;
    return status == NEO_OK ? neo_pkg_schema(vm, root, false, &view) : status;
}

typedef struct neo_pkg_pair { neo_object *baseline, *live; } neo_pkg_pair;
static bool neo_pkg_pairs(neo_vm *vm, neo_object *base, neo_object *live, neo_pkg_pair *pairs, size_t *count) {
    if (*count >= NEO_IMAGE_MAX_OBJECTS || base->value.kind != live->value.kind) { return false; }
    neo_value a = base->value, b = live->value;
    if ((a.kind == NEO_INTEGER && a.integer != b.integer) || (a.kind == NEO_BOOLEAN && a.boolean != b.boolean) ||
        ((a.kind == NEO_TEXT || a.kind == NEO_PRIMITIVE) && strcmp(a.text, b.text) != 0) ||
        (a.kind == NEO_INTEGERS && (a.count != b.count || (a.count != 0 && memcmp(a.integers, b.integers, a.count * sizeof(int64_t)) != 0)))) { return false; }
    pairs[(*count)++] = (neo_pkg_pair){base,live};
    neo_object *x = neo_child_after(vm, base->id, 0), *y = neo_child_after(vm, live->id, 0);
    while (x != NULL && y != NULL) {
        if (strcmp(x->name, y->name) != 0 || !neo_pkg_pairs(vm, x, y, pairs, count)) { return false; }
        x = neo_child_after(vm, base->id, x->order); y = neo_child_after(vm, live->id, y->order);
    }
    return x == NULL && y == NULL;
}
static neo_status neo_pkg_record(neo_vm *vm, neo_object *record, neo_package_view *out) {
    static const char *const fields[] = {"bundle", "baseline"};
    if (!neo_pkg_shape(vm, record, fields, 2)) { return NEO_INVALID; }
    neo_package_view base;
    neo_status status = neo_pkg_schema(vm, neo_pkg_child(vm, record, "baseline"), false, &base);
    if (status == NEO_OK) { status = neo_pkg_schema(vm, neo_pkg_child(vm, record, "bundle"), true, out); }
    if (status != NEO_OK) { return status; }
    if (strcmp(out->id, record->name) != 0) { return NEO_CONFLICT; }
    neo_pkg_pair *pairs = neo_alloc(vm, NEO_IMAGE_MAX_OBJECTS * sizeof(*pairs));
    if (pairs == NULL) { return NEO_OUT_OF_MEMORY; }
    size_t count = 0;
    bool equal = neo_pkg_pairs(vm, base.root, out->root, pairs, &count);
    for (size_t i = 0; equal && i < count; ++i) {
        for (neo_edge *edge = pairs[i].baseline->edges; equal && edge != NULL; edge = edge->next) {
            neo_edge *live = neo_pkg_edge(pairs[i].live, edge->name);
            neo_object_id target = 0;
            for (size_t j = 0; j < count; ++j) { if (pairs[j].baseline->id == edge->target) { target = pairs[j].live->id; break; } }
            equal = live != NULL && live->target == target && live->rights == edge->rights;
        }
        for (neo_edge *edge = pairs[i].live->edges; equal && edge != NULL; edge = edge->next) {
            equal = neo_pkg_edge(pairs[i].baseline, edge->name) != NULL || neo_pkg_binding(vm, out, pairs[i].live, edge->name) != NULL;
        }
    }
    neo_free(vm, pairs);
    return equal ? NEO_OK : NEO_CONFLICT;
}
static neo_status neo_pkg_export_target(neo_vm *vm, neo_object *catalogue, const char *id, const char *name,
                                       unsigned rights, neo_object **out) {
    neo_package_view view;
    neo_object *record = neo_pkg_child(vm, catalogue, id);
    if (record == NULL) { return NEO_UNAVAILABLE; }
    neo_status status = neo_pkg_record(vm, record, &view);
    if (status != NEO_OK) { return status; }
    neo_object *entry = neo_pkg_child(vm, view.exports, name);
    if (entry == NULL) { return NEO_UNAVAILABLE; }
    int64_t ceiling = 0;
    if (!neo_pkg_integer(vm, entry, "rights", &ceiling) || (rights & ~(unsigned)ceiling) != 0) { return NEO_DENIED; }
    *out = neo_pkg_path(vm, view.root, neo_pkg_text(vm, entry, "path"));
    return *out == NULL ? NEO_UNAVAILABLE : NEO_OK;
}
static neo_status neo_pkg_bind(neo_vm *vm, const neo_capability *catalogue_cap, neo_object *catalogue,
                                neo_package_view *view, bool create) {
    for (neo_object *b = neo_child_after(vm, view->bindings->id, 0); b != NULL; b = neo_child_after(vm, view->bindings->id, b->order)) {
        neo_object *import = neo_pkg_child(vm, view->imports, neo_pkg_text(vm, b, "import"));
        neo_object *dependency = neo_pkg_child(vm, view->dependencies, neo_pkg_text(vm, import, "dependency"));
        int64_t mask = 0;
        if (!neo_pkg_integer(vm, import, "rights", &mask)) { return NEO_INVALID; }
        unsigned rights = (unsigned)mask;
        if (create && ((catalogue_cap->rights & NEO_DELEGATE) == 0 || (rights & ~catalogue_cap->rights) != 0)) { return NEO_DENIED; }
        neo_object *target = NULL;
        neo_status status = neo_pkg_export_target(vm, catalogue, neo_pkg_text(vm, dependency, "id"), neo_pkg_text(vm, import, "export"), rights, &target);
        if (status != NEO_OK) { return status; }
        neo_object *owner = neo_pkg_path(vm, view->root, neo_pkg_text(vm, b, "path"));
        const char *name = neo_pkg_text(vm, b, "name");
        if (!create) {
            neo_edge *edge = neo_pkg_edge(owner, name);
            if (edge == NULL || edge->target != target->id || edge->rights != rights) { return NEO_CONFLICT; }
        } else {
            const neo_capability *owner_cap = NULL, *target_cap = NULL;
            status = neo_issue(vm, owner->id, catalogue_cap->rights, &owner_cap);
            if (status == NEO_OK) { status = neo_issue(vm, target->id, catalogue_cap->rights, &target_cap); }
            if (status == NEO_OK) { status = neo_object_connect(vm, owner_cap, name, target_cap, rights); }
            if (status != NEO_OK) { return status; }
        }
    }
    return NEO_OK;
}
static neo_status neo_pkg_unused(neo_vm *vm, neo_object *record) {
    for (neo_object *node = vm->objects; node != NULL; node = node->next) {
        bool inside = neo_inside(vm, node, record->id);
        if (inside && (node->stream != NULL || node->display != NULL || node->ether || node->message != NULL ||
            neo_actor_has_messages(vm, node->id) || neo_scheduler_contains(vm, node->id))) { return NEO_BUSY; }
        for (neo_edge *edge = node->edges; !inside && edge != NULL; edge = edge->next) {
            neo_object *target = neo_lookup(vm, edge->target);
            if (target != NULL && neo_inside(vm, target, record->id)) { return NEO_BUSY; }
        }
    }
    return NEO_OK;
}

neo_status neo_package_install(neo_vm *vm, const neo_capability *catalogue_cap,
                               const neo_capability *bundle_cap, bool replacing, const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    neo_object *catalogue = NULL, *bundle = NULL;
    neo_status status = neo_resolve(vm, catalogue_cap, NEO_READ | NEO_WRITE | NEO_INSERT | NEO_DELETE, &catalogue);
    if (status == NEO_OK) { status = neo_resolve(vm, bundle_cap, NEO_READ | NEO_COPY, &bundle); }
    neo_package_view view;
    if (status == NEO_OK) { status = neo_pkg_schema(vm, bundle, false, &view); }
    if (status != NEO_OK) { return status; }
    if (catalogue->value.kind != NEO_OBJECT || catalogue->edges != NULL || neo_inside(vm, catalogue, bundle->id)) { return NEO_INVALID; }
    neo_object *old = neo_pkg_child(vm, catalogue, view.id);
    if ((old != NULL) != replacing) { return replacing ? NEO_UNAVAILABLE : NEO_CONFLICT; }
    if (old != NULL) {
        neo_package_view previous;
        status = neo_pkg_record(vm, old, &previous);
        if (status != NEO_OK) { return status; }
        status = neo_pkg_unused(vm, old);
        if (status != NEO_OK) { return status; }
    }
    if (neo_pkg_child(vm, catalogue, "_pending") != NULL) { return NEO_BUSY; }
    char *name = neo_string(vm, view.id);
    if (name == NULL) { return NEO_OUT_OF_MEMORY; }
    const neo_capability *record = NULL, *copy = NULL, *owned = NULL;
    status = neo_object_create(vm, catalogue_cap, "_pending", (neo_value){0}, &record);
    if (status == NEO_OK) { status = neo_object_import(vm, bundle_cap, record, "bundle", &copy); }
    if (status == NEO_OK) { status = neo_object_import(vm, bundle_cap, record, "baseline", &copy); }
    if (status == NEO_OK) { status = neo_object_child(vm, record, "bundle", &owned); }
    neo_object *prepared = NULL;
    if (status == NEO_OK) { status = neo_resolve(vm, owned, NEO_READ, &prepared); }
    if (status == NEO_OK) { status = neo_pkg_schema(vm, prepared, false, &view); }
    if (status == NEO_OK) { status = neo_pkg_bind(vm, catalogue_cap, catalogue, &view, true); }
    if (status == NEO_OK) {
        size_t nodes = 0;
        for (neo_object *node = vm->objects; node != NULL; node = node->next) {
            if (node->image != catalogue->image || (old != NULL && neo_inside(vm, node, old->id))) { continue; }
            if (++nodes > NEO_IMAGE_MAX_OBJECTS) { status = NEO_LIMIT; break; }
            size_t depth = 0;
            for (neo_object *parent = node; parent != NULL; parent = neo_lookup(vm, parent->parent)) { if (++depth > NEO_IMAGE_MAX_DEPTH) { status = NEO_LIMIT; break; } }
            if (status != NEO_OK) { break; }
        }
    }
    if (status != NEO_OK) {
        if (record != NULL) { neo_delete_region(vm, record->target); }
        neo_free(vm, name); return status;
    }
    /* All checks, allocation and grants precede the nonfallible commit. No
     * package behavior or observer executes during this synchronous operation. */
    neo_object *published = neo_lookup(vm, record->target);
    if (old != NULL) { neo_delete_region(vm, old->id); }
    neo_node_rename_owned(vm,published,name);
    *out = record;
    return NEO_OK;
}

neo_status neo_package_remove(neo_vm *vm, const neo_capability *catalogue_cap, const char *id) {
    neo_object *catalogue = NULL;
    neo_status status = neo_resolve(vm, catalogue_cap, NEO_READ | NEO_DELETE, &catalogue);
    if (status != NEO_OK || !neo_pkg_name(id, true)) { return status == NEO_OK ? NEO_INVALID : status; }
    neo_object *record = neo_pkg_child(vm, catalogue, id);
    if (record == NULL) { return NEO_UNAVAILABLE; }
    status = neo_pkg_unused(vm, record);
    if (status == NEO_OK) { neo_delete_region(vm, record->id); }
    return status;
}

neo_status neo_package_resolve(neo_vm *vm, const neo_capability *catalogue_cap, const char *id,
                               const char *export_name, unsigned rights, const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (id == NULL || export_name == NULL || (rights & ~(unsigned)NEO_ALL) != 0) { return NEO_INVALID; }
    neo_object *catalogue = NULL, *target = NULL;
    neo_status status = neo_resolve(vm, catalogue_cap, NEO_READ, &catalogue);
    if (status == NEO_OK && (rights & ~catalogue_cap->rights) != 0) { status = NEO_DENIED; }
    if (status == NEO_OK) { status = neo_pkg_export_target(vm, catalogue, id, export_name, rights, &target); }
    if (status == NEO_OK) {
        neo_package_view view;
        status = neo_pkg_record(vm, neo_pkg_child(vm, catalogue, id), &view);
        if (status == NEO_OK) { status = neo_pkg_bind(vm, catalogue_cap, catalogue, &view, false); }
    }
    return status == NEO_OK ? neo_issue(vm, target->id, rights, out) : status;
}

neo_status neo_package_export(neo_vm *vm, const neo_capability *catalogue_cap, const char *id, const neo_capability **out) {
    if (out == NULL) { return NEO_INVALID; }
    *out = NULL;
    if (id == NULL) { return NEO_INVALID; }
    neo_object *catalogue = NULL;
    neo_status status = neo_resolve(vm, catalogue_cap, NEO_READ | NEO_COPY | NEO_DELEGATE | NEO_DELETE, &catalogue);
    neo_package_view view;
    if (status == NEO_OK) { status = neo_pkg_record(vm, neo_pkg_child(vm, catalogue, id), &view); }
    if (status == NEO_OK) { status = neo_pkg_bind(vm, catalogue_cap, catalogue, &view, false); }
    const neo_capability *baseline = NULL;
    if (status == NEO_OK) { status = neo_issue(vm, neo_pkg_child(vm, neo_pkg_child(vm, catalogue, id), "baseline")->id, catalogue_cap->rights, &baseline); }
    return status == NEO_OK ? neo_object_extract(vm, baseline, out) : status;
}
