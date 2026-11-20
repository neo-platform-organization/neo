#include "platform.h"
#include "lattice.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct neo_lattice_axis {
    struct neo_lattice_axis *next;
    int32_t value;
    char name[];
} neo_lattice_axis;

struct neo_lattice_node {
    neo_lattice_node *next;
    neo_lattice_axis *axes;
    char name[];
};

static void neo_lattice_free_node(neo_lattice_node *node) {
    while (node->axes) {
        neo_lattice_axis *axis=node->axes;
        node->axes=axis->next;
        neo_platform_free(axis);
    }
    neo_platform_free(node);
}

void neo_lattice_clear(neo_lattice *layout) {
    while (layout->nodes) {
        neo_lattice_node *node=layout->nodes;
        layout->nodes=node->next;
        neo_lattice_free_node(node);
    }
}

static bool neo_coordinate(const char *text, int32_t *result) {
    if (!text || !*text) { return false; }
    errno=0;
    char *end;
    intmax_t value=strtoimax(text,&end,10);
    if (errno || *end || value<INT32_MIN || value>INT32_MAX) { return false; }
    *result=(int32_t)value;
    return true;
}

static const neo_lattice_axis *neo_find_axis(const neo_lattice_node *node, const char *name) {
    for (const neo_lattice_axis *axis=node->axes; axis; axis=axis->next) {
        if (!strcmp(axis->name,name)) { return axis; }
    }
    return NULL;
}

static const char *neo_place(neo_lattice *layout, const neo_session *session, const char *object,
                             char *inspection, size_t capacity) {
    if (!object) { return "usage: lattice place OBJECT AXIS=INTEGER ..."; }
    const char *failure=neo_session_inspect(session,object,NULL,NEO_INSPECT_WARP,
                                            inspection,capacity);
    if (failure) { return failure; }
    neo_lattice_node *node=neo_platform_malloc(sizeof(*node)+strlen(object)+1);
    if (!node) { return "out of memory"; }
    node->axes=NULL;
    strcpy(node->name,object);
    for (char *part=strtok(NULL," \t"); part; part=strtok(NULL," \t")) {
        char *equals=strchr(part,'=');
        int32_t value;
        if (!equals || equals==part || !neo_coordinate(equals+1,&value)) {
            failure="expected AXIS=INTEGER";
            break;
        }
        *equals=0;
        if (neo_find_axis(node,part)) {
            failure="duplicate layout axis";
            break;
        }
        neo_lattice_axis *axis=neo_platform_malloc(sizeof(*axis)+strlen(part)+1);
        if (!axis) { failure="out of memory"; break; }
        axis->next=node->axes;
        axis->value=value;
        strcpy(axis->name,part);
        node->axes=axis;
    }
    if (!failure && !node->axes) { failure="expected at least one layout axis"; }
    if (failure) {
        neo_lattice_free_node(node);
        return failure;
    }
    neo_lattice_node **link=&layout->nodes;
    while (*link && strcmp((*link)->name,object)) { link=&(*link)->next; }
    neo_lattice_node *previous=*link;
    node->next=previous ? previous->next : NULL;
    *link=node;
    if (previous) { neo_lattice_free_node(previous); }
    printf("placed %s\n",object);
    return NULL;
}

const char *neo_lattice_command(neo_lattice *layout, const neo_session *session, char *command,
                                 char *inspection, size_t capacity) {
    uint64_t revision=neo_session_revision(session);
    if (layout->revision!=revision) {
        neo_lattice_clear(layout);
        layout->revision=revision;
    }
    const char *operation=strtok(command," \t");
    const char *first=strtok(NULL," \t");
    if (operation && !strcmp(operation,"place")) {
        return neo_place(layout,session,first,inspection,capacity);
    }
    const char *second=strtok(NULL," \t");
    bool pick=operation && !strcmp(operation,"pick");
    if ((!pick && (!operation || strcmp(operation,"project"))) || !first || !second) {
        return "usage: lattice place OBJECT AXIS=INTEGER ... | project AXIS AXIS | pick AXIS AXIS X Y";
    }
    if (!strcmp(first,second)) { return "projection requires two distinct axes"; }
    int32_t x=0,y=0;
    if (pick && (!neo_coordinate(strtok(NULL," \t"),&x) ||
                 !neo_coordinate(strtok(NULL," \t"),&y))) {
        return "pick expects two integer coordinates";
    }
    if (strtok(NULL," \t")) { return "unexpected lattice argument"; }
    const char *selected=NULL;
    if (!pick) { printf("projection revision %" PRIu64 " %s %s\n",revision,first,second); }
    for (const neo_lattice_node *node=layout->nodes; node; node=node->next) {
        const neo_lattice_axis *a=neo_find_axis(node,first), *b=neo_find_axis(node,second);
        if (!a || !b) { continue; }
        if (!pick) {
            printf("%s %" PRId32 " %" PRId32 "\n",node->name,a->value,b->value);
        } else if (a->value==x && b->value==y) {
            if (selected) { return "ambiguous projected point"; }
            selected=node->name;
        }
    }
    if (!pick) { return NULL; }
    if (!selected) { return "no node at projected point"; }
    const char *failure=neo_session_inspect(session,selected,NULL,NEO_INSPECT_WARP,
                                            inspection,capacity);
    if (!failure) { fputs(inspection,stdout); }
    return failure;
}
