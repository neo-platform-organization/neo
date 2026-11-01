# Display scale and presentation availability

The window state separates drawable dimensions, layout scale, input state and
presentation availability. Scale is a positive rational number of drawable
pixels per layout unit. A ratio of 3/2 means 150%. Buffer dimensions and pointer
coordinates remain drawable pixels with top-left origin. Neither scale changes
nor loss resize buffers, transform coordinates or run layout behavior.

After `window-poll`, `window-state` exposes these READ-protected fields:

| Field | Value |
| --- | --- |
| `scale-numerator`, `scale-denominator` | Positive integer ratio; default 1/1 |
| `presentation-lost` | Recoverable presentation unavailability, separate from close |
| `surface-epoch` | Monotonic generation for scale/availability transitions |

Providers call `neo_window_surface_change` to update the ratio and availability
atomically. It reduces equivalent fractions, increments the generation only on
change, and requests redraw. At INT64_MAX another change returns LIMIT without
mutation. Legacy providers reporting 0/0 are interpreted as 1/1; a half-zero ratio,
backwards generation or changed scale/availability without a new generation is
rejected by the core. Cached state remains unchanged on rejection.

A successful poll observing a new generation schedules one synthetic `surface`
event. It is delivered after `input-reset`, before queued provider input, and
cannot be dropped by queue overflow. Multiple transitions coalesce; inspect the
latest window state rather than treating the event as a historical snapshot.
The generation also reveals transitions ending at the original state. Poll is
explicit: presentation attempts do not implicitly refresh cached state.

Presentation may be retried while lost. Status returns still report the immediate
attempt; cached availability updates on the next poll. The image chooses when to
retry and redraw. This contract neither recreates windows nor implements GPU
device restoration. Closure remains sticky and prevents presentation.

The headless provider adds `neo_headless_scale` and uses its existing
`neo_headless_present_status` control for availability. Loss retains the last
successful captured frame; restoration permits presentation of the same buffer.
Changes survive a full input queue. Host controls are never graph payloads.

The Windows provider creates windows with scoped per-monitor DPI awareness,
reports DPI/96, and applies the suggested rectangle on `WM_DPICHANGED`. GDI
presentation failures mark availability lost; a successful paint/present restores
it. A failure can be transient and does not imply permanent hardware loss.
X11 currently reports the legacy 1/1 fallback and has no corresponding native
loss normalization. Linux networking remains deliberately stubbed.

Universal tests exercise actual image event/state reads, ratio reduction and
idempotence, invalid ratios, overflow ordering/coalescing, denied poll authority,
loss/recovery with unchanged buffers and generation exhaustion. The Windows
adapter is compiled; multi-monitor transitions and native failure recovery have
not been interactively verified. Milestone10 remains partial pending native
parity and interaction validation.

[Native validation](native-display-validation.md) now also checks synthetic
WM_DPICHANGED notifications through a hidden real window and GDI colour output
through an off-screen bitmap. Physical monitor transitions and induced native
failure recovery remain unverified.
