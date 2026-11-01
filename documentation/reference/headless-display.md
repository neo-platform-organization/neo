# Deterministic headless presentation and input

[Window contract](window-interface.md) · [I/O](io-interface.md)

Milestone10 first checkpoint: a portable host/test window backend through the
existing neo_window_backend API. It opens no OS window and creates no second VM,
image or event loop. Source geometry, renderer policy, controls and application
behavior remain in the same live image. The backend exposes pixels/events only;
no renderer or source-module runtime is added.

## Host API

`display/headless.h` declares neo_headless_open, push, frame, present_status and
release. Open creates an ordinary protected window resource plus an opaque host
controller. Creation requires INSERT on the parent; no image connection/grant is
automatic. The controller is a trusted test/embedding handle, not an image payload
or primitive. Both Makefile and the Windows build include this portable provider.
There is no new --headless launcher flag or default platform-provider selection yet.

The host retains one controller reference and the window another. Release the
host reference exactly once; the window can outlive it. If the window/image is
destroyed first, a retained controller remains safe to query (UNAVAILABLE), then
release. Storage is independent of VM lifetime; no native pointer is serialized.
All calls run on the creating thread and must obey the existing no-reentry rule.
Live window resources still block image formatting/copying just like native ones.

## Pixel presentation

Existing image behavior calls buffer-fill/write and window-present through explicit
capabilities. Each successful presentation makes an independent copy of the exact
RGBA buffer and increments a frame counter. Host frame copies those bytes into
caller storage; it never exposes a borrowed mutable pixel pointer. Too-small output
capacity returns LIMIT with zero outputs and no partial bytes. Before first frame
or after destruction, frame returns UNAVAILABLE. Last successful frame remains
inspectable while a closed window/controller is still attached.

Drawable width/height from poll remain independent of the presented buffer's
width/height. Resize does not resize, crop, scale or modify the saved frame bytes.
Zero-size drawable states represent minimization; initial open dimensions must be
positive and fit event dimensions. Buffer captures retain the core64MiB limit.
The snapshot records the submitted buffer, not a simulated compositor screenshot.

present_status can force UNAVAILABLE and later restore OK. Failed presentation
preserves the last successful frame/count; backend allocation failure likewise
retains the old snapshot. It does not invent a native resource-loss event or
recreate resources. Close permanently prevents further presentation/injection.

## Deterministic input

push accepts normalized close/resize/expose/focus/pointer/button/key/text events.
NONE, OVERFLOW and INPUT_RESET are provider-generated. Invalid kinds, unterminated/empty key
names, negative resize dimensions and invalid button numbers fail before mutation.
Pointer coordinates may be negative. Key events remain symbolic key transitions;
[committed text](text-input.md) carries validated UTF-8 separately. IME preedit
and composition remain pending.

A64-event FIFO retains the oldest events. When full, the newest injected event is
dropped, the lost counter saturates at UINT64_MAX, and push returns LIMIT. next_event
reports one OVERFLOW with accumulated loss before draining retained FIFO events.
Resize/close state is still updated even when that event is dropped. Poll therefore
reports the latest size and sticky close state; an overflow cannot hide closure.
Expose/resize set redraw; one poll consumes this flag. Poll does not consume events.

[Focus/capture and input-reset epochs](input-reset.md) are now implemented.
Epoch changes prioritize a synthetic reset and suppress older queued events,
including retained pre-overflow input. Image objects clear held-state/dragging
policy; the backend does not fabricate a complete physical-key state.

Consumption uses existing READ|WRITE window authority, inspection READ, presentation
WRITE on window and READ on buffer. next_event/get_event and image window-event
snapshots use exactly the same core path as native adapters. No rendering or GUI
policy is pushed into this backend.

## Verification and current scope

The universal selftest renders bytes via literal image behavior into this provider,
checks copied pixel independence, frame count, failed/recovered presentation,
denied grants, event consumption through image behavior, FIFO/snapshot semantics,
resize versus buffer geometry, redraw consumption, overflow before retained events,
close/resize state despite dropped events, unsupported persistence, creation failure
and both ownership-destruction orders. No external test binary or visible window
is launched. Strict Windows build and full universal selftest pass, including these new checks.

Current adapter inventory:

| Mechanism | Existing native layer | This checkpoint |
| --- | --- | --- |
| RGBA present and window state | Win32 GDI and optional X11; fixed pixel-buffer semantics | Deterministic byte capture through same callbacks |
| Close/resize/expose/focus/pointer/button/key/overflow | Existing normalized event kinds and bounded native queues | Host injection and deterministic overflow/state tests |
| Focus/capture/input reset | Win32 mechanism; X11 capture unsupported | Deterministic epochs, loss and cancellation tests |
| Committed text | Win32 UTF-16 to UTF-8; X11 pending | Validated injection and image delivery tests |
| Display scale | Win32 DPI ratio/change handling; X11 fallback | Rational scale injection; coalesced surface events |
| IME preedit | Incomplete contract | Still pending |
| Presentation loss/restoration | Win32 GDI availability; native parity unverified | Availability generations and buffer-preserving recovery |
| GUI/layout/render policy | Future image work | Unchanged |

This is milestone10 progress, not completion or a claim of native Win32/X11
behavioral parity. Linux networking remains deliberately stubbed. Next checkpoint:
native adapter parity and interaction checks. Renderer11 and GUI12–16 remain separate.

[Surface state](surface-state.md) defines rational scale and recoverable
presentation loss. `neo_headless_scale` sets the ratio without resizing drawable
or buffer; `neo_headless_present_status` updates availability generation. Poll
observes changes and schedules an unqueued surface event even when input is full.
