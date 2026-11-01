# Authenticated Windows networking

[VM documentation](../README.md) · [Transport](network-interface.md) ·
[Scheduled sessions](../../neo/packages/session.md)

Networking checkpoint N6 now supplies pinned mutual TLS for Windows x86-64.
Encryption, handshake signatures and record integrity use Windows Schannel;
neo implements nonblocking progress, bounded buffers and explicit grants. The
first supported subset is TLS 1.2 with at least 128-bit negotiated cipher strength.
Other versions, renegotiation and plaintext fallback are rejected. Linux networking
remains explicitly stubbed.

The peer identity is the SHA-256 fingerprint of the complete leaf certificate,
verified after a successful TLS handshake. Both sides require a certificate,
check its exact configured fingerprint, validity at handshake time, and applicable
client/server enhanced key usage. This is direct pinned-certificate trust, not
hostname/CA discovery, trust-on-first-use or an online revocation service. A
certificate reissue changes its fingerprint and requires a new trust grant.

The TLS client/server roles describe connection direction, not object ownership
or publication authority. Authenticating a credential does not identify a unique
VM instance if multiple instances deliberately share it. Publication origin/name
fields remain application assertions. The native peer query identifies the
immediate connected peer; it does not prove the original author of forwarded
snapshots. No signed-publication or multi-hop provenance protocol was added.

## Explicit trust grants

The local identity must already be in the Windows `CurrentUser/MY` certificate
store, with access to its matching private key. Production code opens this store
read-only and never generates/imports a key, selects a default certificate,
exports private key material or adds a trusted root. The selected key is acquired
silently through Windows CryptoAPI/CNG; unsupported or inaccessible credentials
fail. Private keys and Schannel handles never enter the image graph/checkpoint.

The trusted live operator can grant:

```text
net-grant-tls OWNER listen|connect ADDRESS PORT LOCAL_SHA256 PEER_SHA256
```

Both fingerprints are exactly 64 hexadecimal digits, not the usual 40-digit
SHA-1 thumbprint shown by some Windows tools. Certificate pins, endpoint address,
port and direction are fixed in native grant state. Editing an image payload
cannot broaden or downgrade that grant. Creation validates access to the local
identity but opens no socket. A missing identity, bad pin or failed handshake
never falls back to plaintext. `net-grant` remains an explicit plain-TCP option.

Host embedding API:

```c
neo_network_endpoint_secure_create(vm, parent, name, address, port, listening,
                                   local_sha256, peer_sha256, &endpoint);
```

Existing endpoint open/poll/close operations apply. A connector is ready only when
TCP and TLS authentication finish. A listener's ready flag still means listening;
`network-stream` progresses the accepted peer's TLS handshake and returns false
until authenticated. It publishes no peer stream before verification succeeds.
The listener holds at most one pending accepted socket. Publication-allocation
failure retains that socket for retry; endpoint close/deletion frees it.

A TLS grant itself conveys no permission to read or mutate an exposed scope.
Existing stream READ/WRITE, endpoint ACT, destination INSERT and source COPY
checks remain in force. TLS streams and endpoint grants remain noncopyable live
resources and cannot be restored from a checkpoint.

## Verified-peer inspection in neo

```neo
(authenticated-peer (target (child (target (self)) (name "peer"))))
```

This READ-protected native query returns the verified peer fingerprint as 64
lowercase hex digits. A plaintext stream returns unavailable, and arbitrary
objects cannot manufacture a successful query by storing a fingerprint field.
A revoked/stale or insufficient capability still fails. Backend providers without
this capability report unsupported.

`neo.network.session` is version 0.7.0. Its new `authenticate` handler runs
before creating a replica channel, stores a diagnostic `peer-identity`, and
honors `require-authentication`. Set that field true when plaintext must be
refused. It defaults false for compatibility with explicit plain-TCP sessions.
A TLS endpoint always enforces TLS regardless of that field. Cleanup clears the
diagnostic; a stored string is ordinary editable data, so query the current live
stream when an authority decision needs verified transport identity.

## Provisioning example

An operator may use an existing suitable certificate. For a deliberately created
local development identity, Windows PowerShell can create one without exporting
its private key:

```powershell
$identity = New-SelfSignedCertificate -Type Custom -Subject 'CN=neo node' `
  -Provider 'Microsoft Software Key Storage Provider' `
  -KeyAlgorithm RSA -KeyLength 2048 -KeyExportPolicy NonExportable `
  -CertStoreLocation 'Cert:\CurrentUser\My' `
  -KeyUsage DigitalSignature,KeyEncipherment `
  -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.1,1.3.6.1.5.5.7.3.2')
$identity.GetCertHashString([System.Security.Cryptography.HashAlgorithmName]::SHA256)
```

Provision each node's identity separately, exchange the public fingerprints
through an already trusted route, and deliberately configure the expected peer.
Certificate subject text alone is not that trust route. neo does not execute this
provisioning example on ordinary launches.

For the [two-live-image fixtures](../../neo/packages/session.md), replace their
plain grant commands with these, using the actual fingerprints:

```text
net-grant-tls /session listen 127.0.0.1 9000 RECEIVER_SHA256 SENDER_SHA256
set /session/require-authentication true
```

On the sender:

```text
net-grant-tls /session connect 127.0.0.1 9000 SENDER_SHA256 RECEIVER_SHA256
set /session/require-authentication true
```

Then use the documented scheduler registration/start/drive commands. Inspect
`show /session/peer-identity`; receipt and acceptance remain distinct. Trust
rotation/revocation requires stopping the connection, removing the old grant,
and creating a new one. Removing a certificate alone does not terminate an
already established TLS session. Validity is checked at handshake, not repeatedly
on every application byte.

## Bounds and lifecycle

TLS state uses fixed-size bounded buffers: 64 KiB ciphertext/handshake buffers and
16 KiB plaintext records. Each step makes bounded socket/Schannel attempts with
retained offsets; there is no hidden blocking handshake/send loop or DNS/CA
network lookup in the trust check. Plaintext writes are acknowledged only after
the corresponding ciphertext record is flushed; on would-block the caller must
retry the same plaintext, as replica channels already do. Fragmented/coalesced
records retain unconsumed ciphertext and decoded plaintext correctly.

A bad record makes the TLS context unusable; no unauthenticated plaintext reaches
the replica parser. Authenticated close-notify is recognized as EOF; a bare TCP
close is an I/O/truncation failure. Local deletion aborts the transport. Explicit `network-finish` instead sends
authenticated close-notify before local write shutdown; image sessions expose
this through their `finish` handler. Reconnection therefore
uses the existing discard-and-full-snapshot policy. An incomplete received
snapshot is never imported.

Elapsed-time handshake deadlines, idle heartbeats, fairness between pending peers
and delivery/acceptance acknowledgements remain unfinished. Optional image poll
budgets can close stalled connection attempts; native users must provide their
own cancellation policy.
There is no automatic certificate renewal, CA enrollment or key-discovery service.

## Validation and SDK references

The universal Windows selftest creates two temporary RSA identities in the user
certificate/key stores and checks their cleanup. It tests mutual TLS, binary data
across multiple records, both pin-mismatch directions, plaintext rejection,
READ denial on verified-peer inspection and bare-TCP truncation. Scheduler tests
also reject plaintext sessions with authentication required before channel creation.

An optional reproducible integration test uses two live neo processes and a relay
that fragments traffic into 37-byte writes, then corrupts an encrypted application
record. It verifies native peer identities, snapshot transfer, preservation of the
accepted revision and empty candidate inbox after tampering, and rejection of a
TLS client with no certificate. Temporary test certificates and private keys are
removed in a finally block:

```powershell
./neo-vm/platform/win32/tests/tls_live.ps1 -Python python
```

Run from the repository root after the Windows build. The test uses Python's
standard library; Python is not a runtime dependency of neo. Tests need local
socket and current-user test-key/certificate permissions. No public endpoint or
trusted-root-store change is used. Native TLS source is in `platform/win32/tls.c`;
no independent test executable is built.

The implementation follows the SDK's
[Schannel authentication model](https://learn.microsoft.com/en-us/windows/win32/secauthn/performing-authentication-using-schannel),
[context establishment](https://learn.microsoft.com/en-us/windows/win32/secauthn/initializesecuritycontext--schannel),
[remote certificate inspection](https://learn.microsoft.com/en-us/windows/win32/secauthn/manually-validating-schannel-credentials),
and [record decryption](https://learn.microsoft.com/en-us/windows/win32/secauthn/decryptmessage--schannel).
Our explicit direct-pin trust check replaces automatic CA/hostname trust; it does
not skip TLS proof of private-key possession or record integrity. CNG provider
metadata uses the documented
[CRYPT_KEY_PROV_INFO layout](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/ns-wincrypt-crypt_key_prov_info).


Session 0.3.0 optionally bounds the entire connect/accept/TLS attempt with
`connection-limit` unsuccessful scheduler polls (default zero disables it).
Exhaustion closes the pending handshake without publishing a stream. This also
bounds waiting for an incoming peer; it is not elapsed time or active-connection
liveness. Native endpoint users still supply their own cancellation policy. See
[session budgets](../../neo/packages/session.md#connection-attempt-budget-030).
The optional live test covers a silent TCP peer and rearming after retry.


Session 0.4.0 adds explicit graceful local finish and an optional close/drain poll
budget. A complete local close proves neither remote receipt nor acceptance.
Native `neo_network_finish` requires WRITE on an actual network stream; it is
nonblocking and idempotent after completion. `NEO_BUSY` leaves an incomplete TLS
write intact for the original writer to retry. A context generating close-notify
rejects new writes and retains partial shutdown output for later finish polls.
The implementation follows the SDK's
[Schannel shutdown procedure](https://learn.microsoft.com/en-us/windows/win32/secauthn/shutting-down-an-schannel-connection).
Native tests exercise both initiating roles and reciprocal authenticated EOF.
See [image finish policy](../../neo/packages/session.md#graceful-local-finish-040)
for stop/cancellation and retained-candidate behavior.


Session 0.5.0 observes verified TLS EOF at snapshot boundaries and follows its
reciprocal close/stopped path. Partial-frame EOF and bare TCP truncation remain
failures. A candidate awaiting explicit acceptance/rejection defers close reads.
The live test checks this transition with automatic retry enabled; clean close
does not trigger retry or change accepted state. Publisher idle input polling is
still unfinished. See [remote close](../../neo/packages/session.md#remote-close-observation-050).


Exposure 0.2.0/session 0.6.0 optionally send reverse-direction receipt, accepted and
rejected envelopes. They correlate the exact outstanding publication revision;
local send completion is independent. This is ordinary image policy, using the
existing framing and authority. Replies assert the immediate peer's observation,
not original authorship, ownership or proof of application execution. See
[reply protocol](../../neo/packages/exposure.md#opt-in-receipt-and-decision-replies-020).


Exposure0.3/session0.7 optionally multiplex sequenced probe/alive envelopes through
the same snapshot channel. Exact echo checks, bounded reconnect staging and idle
work policy remain in the image; no new native operation or grant was added.
Heartbeats never imply publication acceptance. See [liveness](network-liveness.md).
