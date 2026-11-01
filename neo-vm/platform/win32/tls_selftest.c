#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include "platform/network.h"
#include "platform/win32/tls.h"
#include "image/image.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct neo_tls_test_identity {
    NCRYPT_PROV_HANDLE provider;
    NCRYPT_KEY_HANDLE key;
    HCERTSTORE store;
    PCCERT_CONTEXT certificate;
    char pin[65];
} neo_tls_test_identity;

static bool neo_tls_test_identity_free(neo_tls_test_identity *identity) {
    bool okay = true;
    if (identity->certificate != NULL && !CertDeleteCertificateFromStore(identity->certificate)) { okay = false; }
    if (identity->store != NULL && !CertCloseStore(identity->store, 0)) { okay = false; }
    if (identity->key != 0 && NCryptDeleteKey(identity->key, 0) != ERROR_SUCCESS) {
        (void)NCryptFreeObject(identity->key); okay = false;
    }
    if (identity->provider != 0 && NCryptFreeObject(identity->provider) != ERROR_SUCCESS) { okay = false; }
    *identity = (neo_tls_test_identity){0};
    return okay;
}
static bool neo_tls_test_identity_create(neo_tls_test_identity *identity, unsigned index) {
    wchar_t name[128];
    (void)swprintf(name, sizeof(name) / sizeof(name[0]), L"neo-tls-selftest-%lu-%llu-%u",
        (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64(), index);
    SECURITY_STATUS result = NCryptOpenStorageProvider(&identity->provider, MS_KEY_STORAGE_PROVIDER, 0);
    if (result == ERROR_SUCCESS) { result = NCryptCreatePersistedKey(identity->provider, &identity->key, NCRYPT_RSA_ALGORITHM, name, 0, 0); }
    DWORD bits = 2048;
    if (result == ERROR_SUCCESS) { result = NCryptSetProperty(identity->key, NCRYPT_LENGTH_PROPERTY, (PBYTE)&bits, (DWORD)sizeof(bits), 0); }
    DWORD usage_flags = NCRYPT_ALLOW_DECRYPT_FLAG | NCRYPT_ALLOW_SIGNING_FLAG;
    if (result == ERROR_SUCCESS) { result = NCryptSetProperty(identity->key, NCRYPT_KEY_USAGE_PROPERTY,
        (PBYTE)&usage_flags, (DWORD)sizeof(usage_flags), 0); }
    if (result == ERROR_SUCCESS) { result = NCryptFinalizeKey(identity->key, 0); }
    if (result != ERROR_SUCCESS) { return false; }
    uint8_t subject[256]; DWORD size = (DWORD)sizeof(subject);
    if (!CertStrToNameW(X509_ASN_ENCODING, L"CN=neo temporary TLS selftest", CERT_X500_NAME_STR, NULL, subject, &size, NULL)) { return false; }
    CERT_NAME_BLOB blob = {.cbData=size,.pbData=subject};
    CRYPT_KEY_PROV_INFO info = {0};
    info.pwszContainerName = name; info.pwszProvName = (wchar_t *)MS_KEY_STORAGE_PROVIDER;
    info.dwKeySpec = 0; /* CNG NCryptOpenKey legacy key spec, not a returned handle kind. */
    CRYPT_ALGORITHM_IDENTIFIER algorithm = {.pszObjId=(char *)szOID_RSA_SHA256RSA};
    FILETIME now; GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER end = {.LowPart=now.dwLowDateTime,.HighPart=now.dwHighDateTime};
    end.QuadPart += 24ULL * 60ULL * 60ULL * 10000000ULL;
    FILETIME expiry_time = {.dwLowDateTime=end.LowPart,.dwHighDateTime=end.HighPart};
    SYSTEMTIME expiry;
    if (!FileTimeToSystemTime(&expiry_time, &expiry)) { return false; }
    LPSTR purposes[] = {(LPSTR)szOID_PKIX_KP_SERVER_AUTH,(LPSTR)szOID_PKIX_KP_CLIENT_AUTH};
    CERT_ENHKEY_USAGE purposes_value = {.cUsageIdentifier=2,.rgpszUsageIdentifier=purposes};
    BYTE usage_byte = CERT_DIGITAL_SIGNATURE_KEY_USAGE | CERT_KEY_ENCIPHERMENT_KEY_USAGE;
    CRYPT_BIT_BLOB usage_value = {.cbData=1,.pbData=&usage_byte,.cUnusedBits=5};
    CERT_EXTENSION extensions[2] = {{.pszObjId=(LPSTR)szOID_ENHANCED_KEY_USAGE},
        {.pszObjId=(LPSTR)szOID_KEY_USAGE,.fCritical=TRUE}};
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, &purposes_value,
        CRYPT_ENCODE_ALLOC_FLAG, NULL, &extensions[0].Value.pbData, &extensions[0].Value.cbData)) { return false; }
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_KEY_USAGE, &usage_value,
        CRYPT_ENCODE_ALLOC_FLAG, NULL, &extensions[1].Value.pbData, &extensions[1].Value.cbData)) {
        LocalFree(extensions[0].Value.pbData); return false;
    }
    CERT_EXTENSIONS extension_list = {.cExtension=2,.rgExtension=extensions};
    PCCERT_CONTEXT certificate = CertCreateSelfSignCertificate(identity->key, &blob, 0, &info, &algorithm, NULL, &expiry, &extension_list);
    LocalFree(extensions[0].Value.pbData); LocalFree(extensions[1].Value.pbData);
    if (certificate == NULL) { return false; }
    identity->store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"MY");
    bool okay = identity->store != NULL && CertAddCertificateContextToStore(identity->store, certificate,
        CERT_STORE_ADD_NEW, &identity->certificate) != 0;
    CertFreeCertificateContext(certificate);
    if (!okay) { return false; }
    uint8_t hash[32]; size = (DWORD)sizeof(hash);
    if (!CertGetCertificateContextProperty(identity->certificate, CERT_SHA256_HASH_PROP_ID, hash, &size) || size != sizeof(hash)) { return false; }
    const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) { identity->pin[i * 2] = hex[hash[i] >> 4]; identity->pin[i * 2 + 1] = hex[hash[i] & 15]; }
    identity->pin[64] = '\0';
    return true;
}
static bool neo_tls_test_connection(neo_vm *vm, const neo_tls_test_identity *server_identity,
                                     const neo_tls_test_identity *client_identity, unsigned scenario) {
    const neo_capability *root = NULL, *listener = NULL, *connector = NULL, *server = NULL, *client = NULL;
    neo_diagnostic diagnostic = {0};
    neo_status status = neo_image_parse(vm, "(image)", &root, &diagnostic);
    char wrong[65]; memcpy(wrong, server_identity->pin, sizeof(wrong)); wrong[0] = wrong[0] == '0' ? '1' : '0';
    if (status == NEO_OK) { status = neo_network_endpoint_secure_create(vm, root, "listener", "127.0.0.1", 0, true,
        server_identity->pin, scenario == 2 ? wrong : client_identity->pin, &listener); }
    if (status == NEO_OK) { status = neo_network_endpoint_open(vm, listener); }
    uint16_t port = 0;
    if (status == NEO_OK) { status = neo_network_endpoint_port(vm, listener, &port); }
    if (status == NEO_OK) {
        status = scenario == 3 ? neo_network_endpoint_create(vm, root, "connector", "127.0.0.1", port, false, &connector) :
            neo_network_endpoint_secure_create(vm, root, "connector", "127.0.0.1", port, false,
                client_identity->pin, scenario == 1 ? wrong : server_identity->pin, &connector);
    }
    if (status == NEO_OK) { status = neo_network_endpoint_open(vm, connector); }
    neo_status server_status = NEO_OK, client_status = NEO_OK;
    for (size_t i = 0; status == NEO_OK && i < 3000 && server_status == NEO_OK && client_status == NEO_OK &&
         (server == NULL || client == NULL); ++i) {
        if (client == NULL) { client_status = neo_network_endpoint_stream(vm, connector, root, "client", &client); }
        if (scenario == 3 && client != NULL) {
            const uint8_t plaintext[] = {'n','o','t',' ','T','L','S'}; neo_io_result out = {0};
            status = neo_stream_write(vm, client, plaintext, sizeof(plaintext), &out);
            if (status == NEO_OK) { status = neo_object_delete(vm, client); }
            client = NULL;
        }
        if (server == NULL) { server_status = neo_network_endpoint_stream(vm, listener, root, "server", &server); }
        if (client == NULL || server == NULL) { Sleep(1); }
    }
    if (status == NEO_OK && scenario >= 1 && scenario <= 3) {
        bool denied = scenario == 1 ? client_status != NEO_OK && client == NULL : server_status != NEO_OK && server == NULL;
        if (!denied) { status = NEO_BAD_STATE; }
    } else if (status == NEO_OK) {
        status = server_status != NEO_OK ? server_status : client_status;
        if (status == NEO_OK && (client == NULL || server == NULL)) { status = NEO_BUSY; }
        char peer[65];
        if (status == NEO_OK) { status = neo_network_authenticated_peer(vm, client, peer); }
        if (status == NEO_OK && strcmp(peer, server_identity->pin) != 0) { status = NEO_BAD_STATE; }
        if (status == NEO_OK) { status = neo_network_authenticated_peer(vm, server, peer); }
        if (status == NEO_OK && strcmp(peer, client_identity->pin) != 0) { status = NEO_BAD_STATE; }
        const neo_capability *restricted = NULL;
        if (status == NEO_OK) { status = neo_capability_restrict(vm, client, 0, &restricted); }
        if (status == NEO_OK && neo_network_authenticated_peer(vm, restricted, peer) != NEO_DENIED) { status = NEO_BAD_STATE; }
        uint8_t payload[32768], received[32768];
        for (size_t i = 0; i < sizeof(payload); ++i) { payload[i] = (uint8_t)(i & 255); }
        size_t sent = 0, read = 0;
        for (size_t i = 0; status == NEO_OK && read != sizeof(received) && i < 3000; ++i) {
            neo_io_result out = {0};
            if (sent != sizeof(payload)) {
                status = neo_stream_write(vm, client, payload + sent, sizeof(payload) - sent, &out);
                if (status == NEO_OK) { sent += out.count; }
            }
            if (status == NEO_OK) {
                status = neo_stream_read(vm, server, received + read, sizeof(received) - read, &out);
                if (status == NEO_OK) { read += out.count; }
            }
            if (read != sizeof(received)) { Sleep(1); }
        }
        if (status == NEO_OK && (read != sizeof(received) || memcmp(payload, received, sizeof(payload)) != 0)) { status = NEO_BAD_STATE; }
        if (scenario >= 4) {
            const neo_capability *first = scenario == 4 ? client : server;
            const neo_capability *second = scenario == 4 ? server : client;
            const neo_capability *read_only = NULL;
            bool done = true;
            if (status == NEO_OK) { status = neo_capability_restrict(vm, first, NEO_READ, &read_only); }
            if (status == NEO_OK && (neo_network_finish(vm, read_only, &done) != NEO_DENIED || done)) { status = NEO_BAD_STATE; }
            done = false;
            for (size_t i = 0; status == NEO_OK && !done && i < 1000; ++i) {
                status = neo_network_finish(vm, first, &done);
                if (!done) { Sleep(1); }
            }
            if (status == NEO_OK && !done) { status = NEO_BUSY; }
            if (status == NEO_OK) { status = neo_network_finish(vm, first, &done); }
            if (status == NEO_OK && !done) { status = NEO_BAD_STATE; }
            neo_io_result out = {0};
            if (status == NEO_OK && neo_stream_write(vm, first, payload, 1, &out) != NEO_DENIED) { status = NEO_BAD_STATE; }
            bool eof = false;
            for (size_t i = 0; status == NEO_OK && !eof && i < 1000; ++i) {
                status = neo_stream_read(vm, second, received, 1, &out);
                eof = out.state == NEO_IO_EOF;
                if (!eof) { Sleep(1); }
            }
            if (status == NEO_OK && !eof) { status = NEO_BUSY; }
            /* The remote endpoint explicitly returns its own close-notify. */
            done = false;
            for (size_t i = 0; status == NEO_OK && !done && i < 1000; ++i) {
                status = neo_network_finish(vm, second, &done);
                if (!done) { Sleep(1); }
            }
            if (status == NEO_OK && !done) { status = NEO_BUSY; }
            eof = false;
            for (size_t i = 0; status == NEO_OK && !eof && i < 1000; ++i) {
                status = neo_stream_read(vm, first, received, 1, &out);
                eof = out.state == NEO_IO_EOF;
                if (!eof) { Sleep(1); }
            }
            if (status == NEO_OK && !eof) { status = NEO_BUSY; }
        }
        if (status == NEO_OK && scenario == 0) { status = neo_object_delete(vm, client); client = NULL; }
        neo_status closed = NEO_OK;
        for (size_t i = 0; status == NEO_OK && scenario == 0 && closed == NEO_OK && i < 1000; ++i) {
            neo_io_result out = {0};
            closed = neo_stream_read(vm, server, received, 1, &out);
            if (closed == NEO_OK && out.state == NEO_IO_EOF) { status = NEO_BAD_STATE; }
            if (closed == NEO_OK) { Sleep(1); }
        }
        if (status == NEO_OK && scenario == 0 && closed == NEO_OK) { status = NEO_BAD_STATE; }
    }
    if (status != NEO_OK) { fprintf(stderr, "FAIL TLS scenario %u: %s (server %s, client %s)\n", scenario,
        neo_status_name(status), neo_status_name(server_status), neo_status_name(client_status)); }
    if (root != NULL) { (void)neo_image_unload(vm, root); }
    return status == NEO_OK;
}
bool neo_win32_tls_selftest(neo_vm *vm) {
    neo_tls_test_identity server = {0}, client = {0};
    bool okay = neo_tls_test_identity_create(&server, 0) && neo_tls_test_identity_create(&client, 1);
    if (!okay) { fprintf(stderr, "FAIL temporary TLS test identity: Windows error %lu\n", (unsigned long)GetLastError()); }
    for (unsigned scenario = 0; okay && scenario < 6; ++scenario) {
        okay = neo_tls_test_connection(vm, &server, &client, scenario);
    }
    bool client_cleaned = neo_tls_test_identity_free(&client);
    bool server_cleaned = neo_tls_test_identity_free(&server);
    if (!client_cleaned || !server_cleaned) { fprintf(stderr, "FAIL temporary TLS identity cleanup\n"); }
    return okay && client_cleaned && server_cleaned;
}
