#ifndef NEO_LATTICE_H
#define NEO_LATTICE_H

#include "jit.h"

typedef struct neo_lattice_node neo_lattice_node;
typedef struct {
    neo_lattice_node *nodes;
    uint64_t revision;
} neo_lattice;

void neo_lattice_clear(neo_lattice *layout);
const char *neo_lattice_command(neo_lattice *layout, const neo_session *session, char *command,
                                 char *inspection, size_t capacity);
#endif
