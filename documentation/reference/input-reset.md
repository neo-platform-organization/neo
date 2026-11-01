# Focus, pointer capture and input reset

[Window contract](window-interface.md) · [Headless provider](headless-display.md)

Milestone10 checkpoint: explicit pointer capture, observable focus/capture state,
and input epochs. This supplies platform mechanisms; held-key/button state,
dragging, selection and cancellation policy belong to image objects. No second
input world, VM or source-module runtime is created.

## Image interface and authority

```neo
(window-poll (target "window"))
(window-state (target "window") (field "focused"))
(window-state (target "window") (field "captured"))
(window-state (target "window") (field "input-epoch"))
(window-capture (target "window") (value true))
(window-capture (target "window") (value false))
(window-next-event (target "window"))
(window-event (target "window") (field "epoch"))
```

Targets are explicitly granted connection names. Capture requires WRITE on window;
poll requires WRITE, state/snapshot inspection READ, event consumption READ|WRITE.
Capture's value must be boolean; it returns that boolean on success. Enabling
requires a live focused resource; unavailable focus/window returns UNAVAILABLE.
A missing backend callback returns UNSUPPORTED. Releasing owned capture cancels
input; releasing none is idempotent. Capture calls also refresh the core's cached
state from the successful backend response. Invalid state fields fail explicitly.

Poll before consuming input to collect native events and observe the latest state.
State queries are cached, just like existing width/height inspection. A successful
capture response also refreshes it. Input epochs are available as integer fields;
normalized events now include epoch. No native keycode or OS handle is exposed.

The core rejects a provider state with input_epoch above INT64_MAX or below the
last accepted epoch. Failed state acceptance preserves the previous cached state.

## Reset semantics

Focus loss from a focused/captured window, capture loss, explicit owned capture
release, close and the start of an event-loss episode invalidate held input.
Providers advance input_epoch and clear capture. Native capture is released when
owned; overflow does not imply focus loss. Repeated release without ownership or
focus-loss notification while already unfocused does not advance the epoch.
Win32 cancellation messages also invalidate input.

On observing a changed epoch (or close), the core schedules an unqueued
`input-reset` event with the latest epoch. It is delivered before ordinary queued
events and cannot be displaced by a full queue. Multiple losses before observation
may coalesce into one latest-generation reset. Image controls must cancel dragging
and clear their own held-key/button assumptions on reset; it is cancellation, not
an assertion that the physical keys have been released.

Queued events from older epochs are discarded so a delayed key-down/button-down
cannot restart cancelled state. Overflow reports are retained even when from an
older epoch. Empty queue returns none with the current epoch. A next-event call
skips at most256 stale events before returning LIMIT; the built-in queues hold64.
Close/resize/redraw state remains independently observable through polling even
when old queued notifications are suppressed. Event snapshots remain the last
successfully returned event; reset does not mutate a prior snapshot silently.

The epoch helper stops at INT64_MAX; another reset closes the resource instead
of wrapping into an old generation. Cached closed state cannot reopen and backend
epochs cannot go backward. A backend claiming capture without focus or on a closed
window fails BAD_STATE. No global pressed-input table or fabricated key-up sequence
is added to C. Image controls remain responsible for accepting fresh input only
under their intended focus/interaction policy.

## Providers and limits

Win32 uses SetCapture/GetCapture/ReleaseCapture. Focus loss, WM_CAPTURECHANGED,
WM_CANCELMODE, close/destruction and queue overflow update the shared state model.
Callbacks clear internal capture ownership before releasing OS capture to avoid
reentrant double reset. Losing capture to another window does not release that
other window's capture. No automatic capture-on-button-down policy is imposed.

The headless provider implements acquisition/release and accepts a host-injected
capture-loss notification (CAPTURE with pressed=false). Acquisition cannot be
forged by injecting pressed=true. It supports deterministic focus/overflow/close
reset tests and the same protected core/event path.

X11 tracks focus/reset epochs for its existing events but currently supplies no
capture callback; capture returns UNSUPPORTED. It is an explicit parity gap, not
an emulated successful grab. Linux networking remains intentionally stubbed.
Windows native code is strictly compiled; this checkpoint's behavioral evidence
comes from the headless provider, not automated foreground-window interaction or
X11 runtime testing. Text/IME, scale/resource-loss normalization and native adapter
parity remain milestone10 work.

## Verification

The universal selftest exercises image-owned capture/release and input-reset
handling that writes an actual actor's dragging field false. It checks focus-required
acquisition, denied READ-only capture, focus loss while held input is queued,
synthetic reset priority, stale key/button suppression, monotonic epochs, capture
loss while focus remains, explicit release and repeated-release idempotence.
Existing headless overflow tests now require reset then loss report and suppression
of old queued pointer events, while latest dropped resize/close state survives.
Tests use the single executable and no visible window or separate test binary.

Strict Windows build and full universal selftest pass for this checkpoint. Native
foreground capture/focus behavior and X11 runtime parity remain separate checks.

[Committed text](text-input.md) follows the same epoch filtering. Focus loss
discards queued text and the Win32 decoder's unfinished surrogate pair, preventing
input from an earlier focus lifetime from being inserted after reset.

[Native hidden-window tests](native-display-validation.md) exercise actual Windows
capture/release and capture-loss messages, focus/reset delivery and stale input
suppression. They do not simulate a physical user changing foreground windows.
