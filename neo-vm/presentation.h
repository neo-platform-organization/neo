#ifndef NEO_PRESENTATION_H
#define NEO_PRESENTATION_H
#include "services.h"
#include "jit.h"
typedef struct { neo_services *services; neo_handle surface; } neo_buffer_grant;
const char *neo_buffer_apply(void *, const neo_buffer_command *, size_t);
const char *neo_surface_configure(neo_services *, neo_handle, neo_session *, const char *, uint32_t, uint32_t, uint32_t,
                                   const neo_code_platform *);
int neo_surface_host(const char *, const char *, neo_renderer, const neo_code_platform *);
const char *neo_surface_input(neo_session *, const char *, const neo_input *, const neo_code_platform *);
bool neo_presentation_selftest(const char *, const neo_code_platform *);
bool neo_network_image_selftest(const neo_code_platform *);
#endif
