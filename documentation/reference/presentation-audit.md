# Presentation and interaction acceptance audit

Audit date: 2026-10-23. Scope: milestone10's declared window/buffer/input
mechanisms, the portable headless provider and Windows native adapter. This is
an audit of the current implementation and tests, not a claim that GUI controls,
rendering packages or cross-platform parity are finished.

## Result against the original exit gate

The headless injection/readback and Windows mechanism test requirements have
automated coverage. The original requirement for equivalent Windows and Linux
normalized behavior remains unmet: X11 has no capture callback, emits symbolic
keys without committed text, uses the legacy 1/1 scale fallback and has no native
presentation-loss generation reporting. X11 has not been built or exercised in
this Windows audit. Milestone10 therefore remains partial under its original gate.

Linux networking remains deliberately stubbed and was not changed by this audit.

## Mechanism inventory and evidence

| Contract | Automated evidence | Remaining limit |
| --- | --- | --- |
| Protected buffer/window access | Universal expression and headless tests; READ inspection, WRITE poll/present, READ+WRITE consume, denied grants | No implicit host grant from image shape |
| RGBA pixels and fixed buffers | Headless exact byte/frame capture; native GDI DIB colour conversion/readback | Native desktop visibility/occlusion not exercised |
| Drawable resize/close | Headless latest state even under overflow; native resize via DPI message and close/reset/destruction | Physical window dragging/minimization not exercised |
| Key/text separation | Portable UTF-8/UTF-16 boundary tests and actual image delivery; native posted WM_CHAR, WM_UNICHAR, repeats/control filtering | IME preedit, actual layout/dead-key interaction pending |
| Focus/capture/reset | Literal image drag cancellation; real native capture/release/loss and stale-key/surrogate cancellation | Physical foreground switching not exercised; X11 capture unsupported |
| Overflow and ordering | Headless and native queue overflow, reset priority and stale-event suppression | Loss count/order during real continuous input not measured |
| Rational display scale | Reduction/idempotence and image state reads; native synthetic WM_DPICHANGED | Physical monitor transition not exercised; X11 fallback only |
| Availability and recovery | Headless failure injection preserves frames/buffers; restoration permits present; coalesced state notification survives overflow | Induced native GDI failure/recovery not exercised; no GPU device restoration |
| Ownership and persistence | Creation/allocation failure, either controller/window destruction order, resource save refusal, native HWND cleanup | Native handles are deliberately not checkpointed |
| Provider failure boundary | Invalid scale/generation/capture/text responses and callback errors preserve cached state/event; close cannot reopen | Provider side effects cannot be rolled back by the core |

Primary universal tests live in `neo-vm/cli/selftest.c`:
`neo_test_headless_display`, `neo_test_input_reset`, `neo_test_text_input`,
`neo_test_surface`, and `neo_test_display_provider_boundary`. Windows adds
`neo_win32_window_selftest` in `neo-vm/platform/win32/window.c`. All run through
the same executable's `selftest` command; no separate C test executable is added.

## Audit correction

The core previously accepted a provider's input epoch above INT64_MAX even though
the public image representation and reset helper bound it to that value. State
acceptance now rejects that response before replacing cached state or scheduling
notifications. Surface generations already had the bound.

New provider tests deliberately return invalid half-zero scale, backwards and
excess generations, unversioned scale/availability changes, backwards/excess input
epochs, impossible capture state and poll failure. They also return malformed or
unterminated event text/key data and event callback failure. Each rejected result
must leave the last successful state/event intact and zero the call's output.
Sticky close and exactly-once provider destruction are checked as well.

## Frontend responsibilities

An image frontend polls, consumes reset/surface/input events, and reads cached
state through explicitly granted connections. It owns held input cancellation,
selection, dragging, text editing, layout scaling, retry and redraw policy. Buffer
dimensions and pointer positions remain drawable pixels. Text commits are bounded
UTF-8 and are not assumed to be single graphemes. Surface notifications coalesce
and refer to latest polled state, not a complete transition history.

No second live graph, runtime source-module loader, GUI-specific kernel widget,
renderer algorithm or package policy was introduced. The headless controller is
a trusted host/test API; there is no CLI headless launch mode yet.

## Remaining acceptance work

The original cross-platform gate requires implementing and validating the missing
X11 mechanisms, or an explicit change to the target scope. Windows release
validation still needs physical text/focus/capture/resize interactions, monitor
transitions and induced native presentation recovery. These must not be inferred
from synthetic message checks or portable headless failure injection.

Renderer11 and GUI12–16 are subsequent work. This audit does not silently narrow
milestone10's exit gate or mark it complete.

The verified Windows/headless subset can support bounded reference-renderer work
while the remaining milestone10 gaps stay open. That work must retain the same
portable buffer/window interface and cannot claim broader platform acceptance.

Verification for this checkpoint: strict Windows build and the full universal
selftest suite pass, including provider-boundary, headless and native Windows
checks. Whitespace validation passes.

See [native validation](native-display-validation.md), [headless contracts](headless-display.md),
[input resets](input-reset.md), [text commits](text-input.md), and
[surface state](surface-state.md) for precise interfaces and current limits.
