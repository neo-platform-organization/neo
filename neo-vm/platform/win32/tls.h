#ifndef NEO_WIN32_TLS_H
#define NEO_WIN32_TLS_H
#include "objects/objects.h"
#include "io/io.h"
typedef struct neo_win32_tls neo_win32_tls;
neo_status neo_win32_tls_identity(const uint8_t local[32]);
neo_status neo_win32_tls_create(uintptr_t socket, bool server, const uint8_t local[32],
                                const uint8_t peer[32], neo_win32_tls **out);
void neo_win32_tls_destroy(neo_win32_tls *tls);
neo_status neo_win32_tls_step(neo_win32_tls *tls, bool *ready);
neo_status neo_win32_tls_read(neo_win32_tls *tls, uint8_t *bytes, size_t capacity, neo_io_result *out);
neo_status neo_win32_tls_write(neo_win32_tls *tls, const uint8_t *bytes, size_t count, neo_io_result *out);
neo_status neo_win32_tls_peer(neo_win32_tls *tls, uint8_t out[32]);
neo_status neo_win32_tls_finish(neo_win32_tls *tls, bool *out);
bool neo_win32_tls_selftest(neo_vm *vm);
#endif
