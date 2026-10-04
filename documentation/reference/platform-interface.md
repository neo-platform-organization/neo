# Platform interface

[VM documentation](../README.md)

[platform.h](../../neo-vm/platform/platform.h) defines portable descriptors and
service callbacks. [platform.c](../../neo-vm/platform/platform.c) stores and
validates them. The Linux adapter supplies the native implementation.

| Metadata | Current Linux result |
| --- | --- |
| `environment` | `hosted` |
| `host-os` | `linux` |
| `architecture` | `x86_64` |
| `virtualization` | `unknown` |
| `backend` | `linux-x86_64` |

Hosted/bare-metal and physical/virtualized are independent facts. No virtualization
probe or inferred hardware policy is implemented. Target stubs implement neither
platform metadata nor native services.

## Service contract

| Callback / API | Mechanism |
| --- | --- |
| `read_source` / `neo_platform_load_image` | Bounded inert image loading, maximum 1 MiB; embedded NUL rejected. |
| `open_stream` / `neo_platform_open_stream` | Standard byte-stream creation. |
| `open_window` / `neo_platform_open_window` | Pixel-window creation. |
| `wait` / `neo_platform_wait` | Host millisecond pacing. |

Install once before allocating image objects with `neo_vm_install_platform`.
The descriptor/table are copied; the callback context is borrowed and must outlive
the VM. Resource contexts have their own cleanup callbacks. Providers are trusted
host code; callbacks must not reenter neo, retain borrowed arguments, or leak failed
partial resources. Flags must match callbacks; input events annotate the window
provider. Metadata-only configuration does not install providers.

Unconfigured services return unavailable; absent providers return unsupported.
Opening resources checks parent INSERT permission. Availability does not grant
objects access: the host must explicitly delegate protected connections afterward.
`neo platform` reports compiled providers; image metadata reflects those selected
for that invocation. A compiled window provider does not guarantee a live display.

Sources, streams, waits, and optional X11 opening live under the native platform
layer. Generic VM resource code contains no X11/Win32 types. Waiting is host pacing,
not an image scheduling rule. Loading, current streams, and presentation are
synchronous; no graph-resident completion mechanism has been implemented.
