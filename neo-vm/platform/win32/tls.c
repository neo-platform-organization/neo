#define WIN32_LEAN_AND_MEAN
#define SECURITY_WIN32
#define SCHANNEL_USE_BLACKLISTS
#include <winsock2.h>
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include "platform/win32/tls.h"
#include <stdlib.h>
#include <string.h>

#define NEO_TLS_BUFFER 65536u
#define NEO_TLS_PLAIN 16384u

struct neo_win32_tls {
    SOCKET socket;
    CredHandle credential;
    CtxtHandle context;
    bool has_credential, has_context, server, started, handshake_done, authenticated, failed, eof;
    bool finishing, notify_done, finished;
    PCCERT_CONTEXT local;
    uint8_t expected[32];
    uint8_t incoming[NEO_TLS_BUFFER];
    size_t used;
    uint8_t outgoing[NEO_TLS_BUFFER];
    size_t output_size, output_offset;
    uint8_t plain[NEO_TLS_PLAIN];
    size_t plain_size, plain_offset;
    uint8_t write_plain[NEO_TLS_PLAIN];
    size_t write_count;
    SecPkgContext_StreamSizes sizes;
};

static PCCERT_CONTEXT neo_tls_certificate(const uint8_t pin[32]) {
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
        CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG, L"MY");
    if (store == NULL) { return NULL; }
    PCCERT_CONTEXT found = NULL, current = NULL;
    while ((current = CertEnumCertificatesInStore(store, current)) != NULL) {
        uint8_t hash[32]; DWORD size = (DWORD)sizeof(hash);
        if (CertGetCertificateContextProperty(current, CERT_SHA256_HASH_PROP_ID, hash, &size) &&
            size == sizeof(hash) && memcmp(hash, pin, sizeof(hash)) == 0) {
            found = CertDuplicateCertificateContext(current);
            CertFreeCertificateContext(current); break;
        }
    }
    CertCloseStore(store, 0);
    return found;
}

static bool neo_tls_usage(PCCERT_CONTEXT certificate, bool server) {
    if (CertVerifyTimeValidity(NULL, certificate->pCertInfo) != 0) { return false; }
    DWORD size = 0;
    if (!CertGetEnhancedKeyUsage(certificate, 0, NULL, &size)) { return GetLastError() == (DWORD)CRYPT_E_NOT_FOUND; }
    PCERT_ENHKEY_USAGE usage = malloc(size);
    if (usage == NULL) { return false; }
    bool okay = CertGetEnhancedKeyUsage(certificate, 0, usage, &size) != 0;
    if (okay && usage->cUsageIdentifier != 0) {
        okay = false;
        const char *required = server ? szOID_PKIX_KP_SERVER_AUTH : szOID_PKIX_KP_CLIENT_AUTH;
        for (DWORD i = 0; i < usage->cUsageIdentifier; ++i) {
            if (strcmp(usage->rgpszUsageIdentifier[i], required) == 0 ||
                strcmp(usage->rgpszUsageIdentifier[i], szOID_ANY_ENHANCED_KEY_USAGE) == 0) { okay = true; }
        }
    }
    free(usage);
    return okay;
}
neo_status neo_win32_tls_identity(const uint8_t local[32]) {
    PCCERT_CONTEXT certificate = neo_tls_certificate(local);
    if (certificate == NULL) { return NEO_UNAVAILABLE; }
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0; DWORD kind = 0; BOOL release = FALSE;
    bool okay = CryptAcquireCertificatePrivateKey(certificate,
        CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_COMPARE_KEY_FLAG, NULL, &key, &kind, &release) != 0;
    if (okay && release) {
        if (kind == CERT_NCRYPT_KEY_SPEC) { (void)NCryptFreeObject((NCRYPT_HANDLE)key); }
        else { (void)CryptReleaseContext((HCRYPTPROV)key, 0); }
    }
    CertFreeCertificateContext(certificate);
    return okay ? NEO_OK : NEO_DENIED;
}
void neo_win32_tls_destroy(neo_win32_tls *tls) {
    if (tls == NULL) { return; }
    if (tls->has_context) { (void)DeleteSecurityContext(&tls->context); }
    if (tls->has_credential) { (void)FreeCredentialsHandle(&tls->credential); }
    if (tls->local != NULL) { CertFreeCertificateContext(tls->local); }
    SecureZeroMemory(tls, sizeof(*tls));
    free(tls);
}
neo_status neo_win32_tls_create(uintptr_t socket, bool server, const uint8_t local[32],
                                const uint8_t peer[32], neo_win32_tls **out) {
    *out = NULL;
    neo_win32_tls *tls = calloc(1, sizeof(*tls));
    if (tls == NULL) { return NEO_OUT_OF_MEMORY; }
    SecInvalidateHandle(&tls->credential);
    SecInvalidateHandle(&tls->context);
    tls->socket = (SOCKET)socket; tls->server = server;
    memcpy(tls->expected, peer, sizeof(tls->expected));
    tls->local = neo_tls_certificate(local);
    if (tls->local == NULL || !neo_tls_usage(tls->local, server)) {
        neo_win32_tls_destroy(tls); return NEO_DENIED;
    }
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0; DWORD key_kind = 0; BOOL release_key = FALSE;
    if (!CryptAcquireCertificatePrivateKey(tls->local, CRYPT_ACQUIRE_CACHE_FLAG | CRYPT_ACQUIRE_SILENT_FLAG |
        CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_COMPARE_KEY_FLAG, NULL, &key, &key_kind, &release_key)) {
        neo_win32_tls_destroy(tls); return NEO_DENIED;
    }
    if (release_key) {
        if (key_kind == CERT_NCRYPT_KEY_SPEC) { (void)NCryptFreeObject((NCRYPT_HANDLE)key); }
        else { (void)CryptReleaseContext((HCRYPTPROV)key, 0); }
    }
    TLS_PARAMETERS parameters = {0};
    /* Initial bounded implementation uses TLS 1.2 only, never weaker fallback. */
    parameters.grbitDisabledProtocols = SP_PROT_PCT1 | SP_PROT_SSL2 | SP_PROT_SSL3 | SP_PROT_TLS1_0 | SP_PROT_TLS1_1 | SP_PROT_TLS1_3;
    SCH_CREDENTIALS credentials = {0};
    credentials.dwVersion = SCH_CREDENTIALS_VERSION;
    credentials.cCreds = 1; credentials.paCred = &tls->local;
    credentials.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO |
        SCH_CRED_CACHE_ONLY_URL_RETRIEVAL | SCH_CRED_CACHE_ONLY_URL_RETRIEVAL_ON_CREATE |
        (server ? SCH_CRED_NO_SYSTEM_MAPPER : 0u);
    credentials.cTlsParameters = 1; credentials.pTlsParameters = &parameters;
    TimeStamp expiry;
    SECURITY_STATUS result = AcquireCredentialsHandleA(NULL, UNISP_NAME_A,
        server ? SECPKG_CRED_INBOUND : SECPKG_CRED_OUTBOUND, NULL, &credentials,
        NULL, NULL, &tls->credential, &expiry);
    if (result != SEC_E_OK) { neo_win32_tls_destroy(tls); return NEO_DENIED; }
    tls->has_credential = true;
    *out = tls;
    return NEO_OK;
}
static neo_status neo_tls_flush(neo_win32_tls *tls, bool *done) {
    *done = tls->output_offset == tls->output_size;
    if (*done) { return NEO_OK; }
    size_t count = tls->output_size - tls->output_offset;
    int sent = send(tls->socket, (const char *)tls->outgoing + tls->output_offset, (int)count, 0);
    if (sent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) { return NEO_OK; }
    if (sent <= 0) { return NEO_IO_ERROR; }
    tls->output_offset += (size_t)sent;
    *done = tls->output_offset == tls->output_size;
    if (*done) { tls->output_offset = 0; tls->output_size = 0; }
    return NEO_OK;
}
static neo_status neo_tls_fill(neo_win32_tls *tls, bool *progress) {
    *progress = false;
    if (tls->used == sizeof(tls->incoming)) { return NEO_LIMIT; }
    int received = recv(tls->socket, (char *)tls->incoming + tls->used,
        (int)(sizeof(tls->incoming) - tls->used), 0);
    if (received == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) { return NEO_OK; }
    /* Bare TCP EOF is a TLS truncation, never authenticated EOF. */
    if (received <= 0) { return NEO_IO_ERROR; }
    tls->used += (size_t)received; *progress = true;
    return NEO_OK;
}
static neo_status neo_tls_verify(neo_win32_tls *tls) {
    PCCERT_CONTEXT peer = NULL;
    if (QueryContextAttributesA(&tls->context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &peer) != SEC_E_OK || peer == NULL) {
        return NEO_DENIED;
    }
    uint8_t hash[32]; DWORD size = (DWORD)sizeof(hash);
    bool okay = CertGetCertificateContextProperty(peer, CERT_SHA256_HASH_PROP_ID, hash, &size) != 0 &&
        size == sizeof(hash) && memcmp(hash, tls->expected, sizeof(hash)) == 0 && neo_tls_usage(peer, !tls->server);
    CertFreeCertificateContext(peer);
    if (!okay) { return NEO_DENIED; }
    SecPkgContext_ConnectionInfo connection = {0};
    if (QueryContextAttributesA(&tls->context, SECPKG_ATTR_CONNECTION_INFO, &connection) != SEC_E_OK ||
        (connection.dwProtocol != SP_PROT_TLS1_2_CLIENT && connection.dwProtocol != SP_PROT_TLS1_2_SERVER) ||
        connection.dwCipherStrength < 128) { return NEO_DENIED; }
    if (QueryContextAttributesA(&tls->context, SECPKG_ATTR_STREAM_SIZES, &tls->sizes) != SEC_E_OK ||
        tls->sizes.cbMaximumMessage == 0 || tls->sizes.cbMaximumMessage > NEO_TLS_PLAIN ||
        (size_t)tls->sizes.cbHeader + tls->sizes.cbTrailer > NEO_TLS_BUFFER - NEO_TLS_PLAIN) { return NEO_UNSUPPORTED; }
    tls->authenticated = true;
    return NEO_OK;
}
static neo_status neo_tls_step(neo_win32_tls *tls, bool *ready) {
    *ready = false;
    if (tls->failed) { return NEO_DENIED; }
    bool flushed = false;
    neo_status status = neo_tls_flush(tls, &flushed);
    if (status != NEO_OK || !flushed) { return status; }
    if (tls->handshake_done) {
        if (!tls->authenticated) { status = neo_tls_verify(tls); }
        *ready = status == NEO_OK;
        return status;
    }
    bool initial = !tls->server && !tls->started;
    if (!initial) {
        bool progress = false;
        status = neo_tls_fill(tls, &progress);
        if (status != NEO_OK) { return status; }
        if (!progress && tls->used == 0) { return NEO_OK; }
    }
    SecBuffer input[2] = {{(unsigned long)tls->used,SECBUFFER_TOKEN,tls->incoming},{0,SECBUFFER_EMPTY,NULL}};
    SecBufferDesc in = {SECBUFFER_VERSION,2,input};
    SecBuffer output = {0,SECBUFFER_TOKEN,NULL};
    SecBufferDesc out = {SECBUFFER_VERSION,1,&output};
    unsigned long attributes = 0;
    TimeStamp expiry;
    SECURITY_STATUS result;
    if (tls->server) {
        unsigned long flags = ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM | ASC_REQ_CONFIDENTIALITY |
            ASC_REQ_INTEGRITY | ASC_REQ_REPLAY_DETECT | ASC_REQ_SEQUENCE_DETECT | ASC_REQ_MUTUAL_AUTH;
        result = AcceptSecurityContext(&tls->credential, tls->has_context ? &tls->context : NULL,
            &in, flags, SECURITY_NATIVE_DREP, &tls->context, &out, &attributes, &expiry);
    } else {
        unsigned long flags = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_CONFIDENTIALITY |
            ISC_REQ_INTEGRITY | ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT |
            ISC_REQ_MANUAL_CRED_VALIDATION | ISC_REQ_USE_SUPPLIED_CREDS;
        result = InitializeSecurityContextA(&tls->credential, tls->has_context ? &tls->context : NULL,
            NULL, flags, 0, SECURITY_NATIVE_DREP, initial ? NULL : &in, 0,
            &tls->context, &out, &attributes, &expiry);
    }
    tls->has_context = SecIsValidHandle(&tls->context);
    if (result == SEC_E_OK || result == SEC_I_CONTINUE_NEEDED) { tls->started = true; }
    if (output.pvBuffer != NULL) {
        if (output.cbBuffer > sizeof(tls->outgoing)) { status = NEO_LIMIT; }
        else {
            memcpy(tls->outgoing, output.pvBuffer, output.cbBuffer);
            tls->output_size = output.cbBuffer; tls->output_offset = 0;
        }
        (void)FreeContextBuffer(output.pvBuffer);
    }
    if (status != NEO_OK) { return status; }
    if (result == SEC_E_INCOMPLETE_MESSAGE) { return NEO_OK; }
    if (result != SEC_E_OK && result != SEC_I_CONTINUE_NEEDED) { return NEO_DENIED; }
    if (!initial) {
        size_t extra = input[1].BufferType == SECBUFFER_EXTRA ? input[1].cbBuffer : 0;
        if (extra > tls->used) { return NEO_IO_ERROR; }
        if (extra != 0) { memmove(tls->incoming, tls->incoming + tls->used - extra, extra); }
        tls->used = extra;
    }
    tls->handshake_done = result == SEC_E_OK;
    /* Output flush/verification occur on a subsequent bounded step. */
    return NEO_OK;
}
neo_status neo_win32_tls_step(neo_win32_tls *tls, bool *ready) {
    neo_status status = neo_tls_step(tls, ready);
    if (status != NEO_OK) { tls->failed = true; }
    return status;
}
neo_status neo_win32_tls_read(neo_win32_tls *tls, uint8_t *bytes, size_t capacity, neo_io_result *out) {
    *out = (neo_io_result){NEO_IO_WOULD_BLOCK,0};
    if (!tls->authenticated || tls->failed) { return NEO_DENIED; }
    if (tls->eof) { out->state = NEO_IO_EOF; return NEO_OK; }
    if (tls->plain_offset == tls->plain_size) {
        bool progress = false;
        bool filled = tls->used == 0;
        neo_status status = filled ? neo_tls_fill(tls, &progress) : NEO_OK;
        if (status != NEO_OK) { tls->failed = true; return status; }
        if (tls->used == 0) { return NEO_OK; }
        SecBuffer buffers[4] = {{(unsigned long)tls->used,SECBUFFER_DATA,tls->incoming},
            {0,SECBUFFER_EMPTY,NULL},{0,SECBUFFER_EMPTY,NULL},{0,SECBUFFER_EMPTY,NULL}};
        SecBufferDesc message = {SECBUFFER_VERSION,4,buffers};
        SECURITY_STATUS result = DecryptMessage(&tls->context, &message, 0, NULL);
        if (result == SEC_E_INCOMPLETE_MESSAGE) {
            if (filled) { return NEO_OK; }
            status = neo_tls_fill(tls, &progress);
            if (status != NEO_OK) { tls->failed = true; }
            return status;
        }
        if (result == SEC_I_CONTEXT_EXPIRED) { tls->eof = true; out->state = NEO_IO_EOF; return NEO_OK; }
        if (result != SEC_E_OK) { tls->failed = true; return NEO_DENIED; }
        size_t extra = 0;
        tls->plain_size = 0; tls->plain_offset = 0;
        for (size_t i = 0; i < 4; ++i) {
            if (buffers[i].BufferType == SECBUFFER_DATA) {
                if (buffers[i].cbBuffer > sizeof(tls->plain)) { tls->failed = true; return NEO_LIMIT; }
                memcpy(tls->plain, buffers[i].pvBuffer, buffers[i].cbBuffer); tls->plain_size = buffers[i].cbBuffer;
            } else if (buffers[i].BufferType == SECBUFFER_EXTRA) { extra = buffers[i].cbBuffer; }
        }
        if (extra > tls->used) { tls->failed = true; return NEO_IO_ERROR; }
        if (extra != 0) { memmove(tls->incoming, tls->incoming + tls->used - extra, extra); }
        tls->used = extra;
    }
    size_t count = tls->plain_size - tls->plain_offset;
    if (count > capacity) { count = capacity; }
    if (count == 0) { return NEO_OK; }
    memcpy(bytes, tls->plain + tls->plain_offset, count); tls->plain_offset += count;
    *out = (neo_io_result){NEO_IO_TRANSFERRED,count}; return NEO_OK;
}
neo_status neo_win32_tls_write(neo_win32_tls *tls, const uint8_t *bytes, size_t count, neo_io_result *out) {
    *out = (neo_io_result){NEO_IO_WOULD_BLOCK,0};
    if (!tls->authenticated || tls->failed || tls->eof || tls->finishing) { return NEO_DENIED; }
    if (tls->write_count == 0) {
        size_t plain = count > tls->sizes.cbMaximumMessage ? tls->sizes.cbMaximumMessage : count;
        memcpy(tls->write_plain, bytes, plain);
        memcpy(tls->outgoing + tls->sizes.cbHeader, bytes, plain);
        SecBuffer buffers[4] = {{tls->sizes.cbHeader,SECBUFFER_STREAM_HEADER,tls->outgoing},
            {(unsigned long)plain,SECBUFFER_DATA,tls->outgoing + tls->sizes.cbHeader},
            {tls->sizes.cbTrailer,SECBUFFER_STREAM_TRAILER,tls->outgoing + tls->sizes.cbHeader + plain},
            {0,SECBUFFER_EMPTY,NULL}};
        SecBufferDesc message = {SECBUFFER_VERSION,4,buffers};
        if (EncryptMessage(&tls->context, 0, &message, 0) != SEC_E_OK) { tls->failed = true; return NEO_IO_ERROR; }
        tls->output_size = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
        tls->output_offset = 0; tls->write_count = plain;
    } else if (count < tls->write_count || memcmp(bytes, tls->write_plain, tls->write_count) != 0) {
        tls->failed = true; return NEO_INVALID;
    }
    bool done = false;
    neo_status status = neo_tls_flush(tls, &done);
    if (status != NEO_OK) { tls->failed = true; return status; }
    if (done) {
        *out = (neo_io_result){NEO_IO_TRANSFERRED,tls->write_count}; tls->write_count = 0;
    }
    return NEO_OK;
}
neo_status neo_win32_tls_peer(neo_win32_tls *tls, uint8_t out[32]) {
    if (tls == NULL || !tls->authenticated || tls->failed) { return NEO_UNAVAILABLE; }
    memcpy(out, tls->expected, 32);
    return NEO_OK;
}

static neo_status neo_tls_finish(neo_win32_tls *tls, bool *out) {
    *out = false;
    if (!tls->authenticated || tls->failed) { return NEO_DENIED; }
    if (tls->finished) { *out = true; return NEO_OK; }
    /* The caller must complete its pending write with the same bytes first. */
    if (tls->write_count != 0) { return NEO_BUSY; }
    bool flushed = false;
    neo_status status = neo_tls_flush(tls, &flushed);
    if (status != NEO_OK || !flushed) { return status; }
    if (tls->notify_done) {
        if (shutdown(tls->socket, SD_SEND) != 0) { return NEO_IO_ERROR; }
        tls->finished = true; *out = true;
        return NEO_OK;
    }
    if (!tls->finishing) {
        DWORD token = SCHANNEL_SHUTDOWN;
        SecBuffer buffer = {sizeof(token),SECBUFFER_TOKEN,&token};
        SecBufferDesc control = {SECBUFFER_VERSION,1,&buffer};
        if (ApplyControlToken(&tls->context, &control) != SEC_E_OK) { return NEO_IO_ERROR; }
        tls->finishing = true;
    }
    SecBuffer buffer = {0,SECBUFFER_TOKEN,NULL};
    SecBufferDesc message = {SECBUFFER_VERSION,1,&buffer};
    unsigned long attributes = 0;
    TimeStamp expiry;
    SECURITY_STATUS result;
    if (tls->server) {
        result = AcceptSecurityContext(&tls->credential, &tls->context, NULL,
            ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM | ASC_REQ_CONFIDENTIALITY | ASC_REQ_INTEGRITY |
            ASC_REQ_REPLAY_DETECT | ASC_REQ_SEQUENCE_DETECT | ASC_REQ_MUTUAL_AUTH,
            SECURITY_NATIVE_DREP, &tls->context, &message, &attributes, &expiry);
    } else {
        result = InitializeSecurityContextA(&tls->credential, &tls->context, NULL,
            ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_CONFIDENTIALITY | ISC_REQ_INTEGRITY |
            ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_MANUAL_CRED_VALIDATION |
            ISC_REQ_USE_SUPPLIED_CREDS, 0, SECURITY_NATIVE_DREP, NULL, 0,
            &tls->context, &message, &attributes, &expiry);
    }
    tls->has_context = SecIsValidHandle(&tls->context);
    if (buffer.pvBuffer != NULL) {
        if (buffer.cbBuffer > sizeof(tls->outgoing)) { status = NEO_LIMIT; }
        else {
            memcpy(tls->outgoing, buffer.pvBuffer, buffer.cbBuffer);
            tls->output_size = buffer.cbBuffer; tls->output_offset = 0;
        }
        (void)FreeContextBuffer(buffer.pvBuffer);
    }
    if (status != NEO_OK) { return status; }
    if (result != SEC_E_OK && result != SEC_I_CONTEXT_EXPIRED && result != SEC_I_CONTINUE_NEEDED) { return NEO_IO_ERROR; }
    tls->notify_done = result != SEC_I_CONTINUE_NEEDED;
    /* Flush the token on later polls; no blocking loop or implicit peer wait. */
    return NEO_OK;
}
neo_status neo_win32_tls_finish(neo_win32_tls *tls, bool *out) {
    neo_status status = neo_tls_finish(tls, out);
    if (status != NEO_OK && status != NEO_BUSY) { tls->failed = true; }
    return status;
}
