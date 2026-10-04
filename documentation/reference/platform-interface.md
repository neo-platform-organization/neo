# Platform description and host boundary

[Documentation index](../README.md) · [OS sessions](../planning/os-sessions.md)

## Implemented scope

The portable VM knows a platform descriptor, not Linux descriptors, X11 windows,
Win32 handles, or GPU APIs. `neo_platform.h` is the common descriptor contract;
`platform.c` validates and stores it. `platform_linux.c` is the only native adapter,
selected for hosted Linux x86_64. It is linked into runners separately from the
portable `libneo.a`. Other build targets are unsupported.

Sessions 1 and 2 are implemented. The unified executable calls a common
host-service interface for loading image source, opening standard streams,
opening pixel windows, and waiting. Linux-specific code lives in
`platform_linux.c`, `io_posix.c`, and optional `window_x11.c`. Runners no longer
call `fopen`, `nanosleep`, POSIX descriptor APIs, or X11 creation directly.

Existing `neo_stream_backend` and `neo_window_backend` still handle transfers,
presentation, and normalized events after opening. Rendering remains neo code.
No new executable, GUI, filesystem primitive, or scheduler policy was introduced.
CLI output formatting still uses C stdio; the unified launch interface is implemented.

## Independent facts

| Field | Meaning | Linux adapter |
| --- | --- | --- |
| `environment` | Execution relies on a host OS, or a future bare-metal backend | `hosted` |
| `host-os` | Host OS identifier; `none` for a future bare-metal backend | `linux` |
| `architecture` | Backend target architecture, not a claim about physical silicon | `x86_64` |
| `virtualization` | Underlying physical/virtual status when known | `unknown` |
| `backend` | Informational backend identifier | `linux-x86_64` |

Hosted does not mean virtualized; bare-metal does not mean physical. A future
bare-metal backend could run under a hypervisor. This adapter does not probe CPU,
firmware, environment variables, or host files to guess virtualization. `unknown`
is a real result, never shorthand for physical. Backend identifiers should be
used for diagnostics; portable image behavior should inspect services and use
resource capabilities rather than branch on an OS name.

## Service reporting is not authority

The trusted bootstrap requests providers; the factory and installer validate the callback table against these flags:

| Query | Provider contract | Current runners reporting true |
| --- | --- | --- |
| `host-files` | Bounded host image-source loading | all image-loading commands |
| `byte-streams` | Backend-neutral byte streams | CLI mode |
| `pixel-windows` | Window presenting an RGBA pixel buffer | GUI mode |
| `input-events` | Normalized input events | GUI mode |
| `wait` | Host-controlled millisecond wait | GUI mode |

These flags describe providers selected by bootstrap, not OS-wide detection,
permissions, or currently open devices. A compiled window provider may still fail
to connect to a display. `host-files = true` does not supply an image-level
filesystem primitive. Applications still need explicit capabilities for each
resource. An object named `window` or `stdin` does not become a native resource.
No GUI mode, GPU backend, or renderer-selection policy is introduced here.

## C setup

```c
neo_platform_services services;
neo_status status = neo_platform_native_services(NEO_PLATFORM_HOST_FILES, &services);
if (status == NEO_OK) {
    status = neo_vm_install_platform(vm, &services);
}
```

Configuration is host-only, once per VM, before allocating image objects. The VM
copies fixed-size strings, flags, and the callback table. The opaque callback
context remains host-owned and must outlive the VM; the VM does not free it. Invalid descriptors return `NEO_INVALID`; reconfiguration or setup
after object-identity allocation returns `NEO_BUSY`. An attempted load can consume
identities even when loading later fails. Resource operations and grants are not
performed by configuration. Plain `neo_vm_create` remains unconfigured, useful
for embedding and parser-only tools.

`neo_vm_platform_info` copies the descriptor out, or returns `NEO_UNAVAILABLE` if
unconfigured. Its output is cleared on failure. Platform metadata is not serialized
as part of the image: each process configures its own real backend. A field inside
an image cannot override it. Synthetic test descriptors exercise the generic
contract; they are not implementations of other platforms.

The common descriptor admits future environments, architectures, and host names
without importing native headers. Strings are nonempty, NUL-terminated identifiers
of fewer than 32 bytes. Unknown service bits and inconsistent hosted/bare-metal
host-file declarations are rejected. The native adapter is Linux-specific; other Makefile platform selections fail
explicitly. A factory request for a provider not compiled into the runner returns
`NEO_UNSUPPORTED`. X11 is enabled only for window builds via
`NEO_PLATFORM_WITH_X11`; the default CLI and portable archive have no X11 link dependency.

## CLI and neo queries

```sh
make
./build/neo platform
```

No image is loaded by this command. Within a behavior:

```text
(platform-info (field "environment"))
(platform-info (field "byte-streams"))
```

The first returns text; the second a boolean. All fields in the tables above are
supported. Unknown fields return `NEO_INVALID`; nontext fields return
`NEO_WRONG_KIND`; unconfigured VMs return `NEO_UNAVAILABLE`. The primitive observes
normal operand evaluation, return propagation, and execution budgets. It only
reads nonsensitive bootstrap metadata; it cannot manufacture resource authority.

## Service operations and ownership

| Host operation | Contract |
| --- | --- |
| `neo_platform_load_image` | Read at most 1 MiB through `read_source`, reject embedded NUL and excess bytes, parse inertly, return an image capability. |
| `neo_platform_open_stream` | Open input, output, or error by a portable enum. No native descriptor enters runner code. |
| `neo_platform_open_window` | Open a titled pixel window with dimensions; return an ordinary protected resource capability. |
| `neo_platform_wait` | Dispatch a millisecond wait. The existing window runner requests 16 ms after successful frames. |

These are trusted C bootstrap APIs, not neo primitives. Resource opening checks
INSERT on the destination before invoking the provider; use of the returned
resource still requires its own rights and explicit grants. Platform metadata
never grants those rights. A missing setup returns `NEO_UNAVAILABLE`; disabled or
uninstalled services return `NEO_UNSUPPORTED`. Invalid arguments, denied authority,
provider errors, and allocation failures propagate without fabricated success.

`read_source` receives core-owned bounded storage. It must return a complete
source or an error, never successful truncation. The core frees its scratch buffer
on every exit; successful parsing creates independent graph storage. Loading does
not invoke a handler. Native paths are interpreted only by the Linux provider;
other providers can interpret locations differently.

Resource-opening callbacks clean up partial native state on failure and leave
outputs null. After success the resource's existing destroy callback owns cleanup
at object/image/VM deletion. Callback tables are copied, arguments are borrowed for
the duration of the call, and callbacks must not reenter neo execution. Context
ownership is separate from each opened resource's ownership.

The Linux stream provider duplicates standard descriptors without changing their
flags or terminal modes; EOF, short writes, would-block, and broken-pipe handling
remain as documented in the [I/O interface](io-interface.md). Inherited blocking
stdio can still block. The loader is synchronous. The wait provider resumes a
sleep interrupted by a signal and reports other errors; this is host pacing, not
a neo tick rule, resumable invocation, event wait, or cancellation protocol.

`neo_vm_configure_platform` remains a descriptor-only embedding API. It installs
no callbacks; advertised metadata alone cannot dispatch services. Production
runners use `neo_vm_install_platform`, which validates matching callbacks and
flags. Input-event support currently annotates the installed window provider;
a standalone input-opening service is not implemented.

## Validation and next step

`make test` covers descriptors, service dispatch, no implicit grants, fake byte
streams, normalized fake window events, source limits/NUL rejection, parser errors,
allocation-failure cleanup, provider failures, rights, stale parents, and resource
destruction. CLI tests preserve formatting, loading, stream output separation,
and exactly-at-limit versus oversized sources. `test_platform_services` links only
the portable archive; it does not need Linux or X11 callbacks.

Platform descriptor and service ASan/UBSan tests pass with sandbox leak detection
disabled. `make test-window` opens a short-lived real X11 window through the platform
factory and verifies pixels, resizing, input, overflow, close, and cleanup; it passed
for this refactor. No changes to rendering algorithms were needed.

Next: consolidate the three runners into the single `neo` CLI while preserving
headless builds, optional window support, command behavior, and output separation.

`neo platform` reports all compiled providers. Per-image metadata reports the
providers selected for that invocation; neither report grants resource access.
