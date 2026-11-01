# Windows display validation

The single executable's `selftest` command now runs hidden-window checks through
the real Win32 provider. The public window opener retains its normal visible
behavior; only the internal test path creates a hidden native window. No native
handle is exposed to image code, serialized, or added to the language primitives.

Tests send messages to that owned window and consume them through the protected
window interface. Posted character messages pass through the ordinary poll loop;
other messages enter the same registered window procedure synchronously. They
exercise these mechanisms:

| Mechanism | Native checks |
| --- | --- |
| Symbolic keys and text | Key name/pressed state stays separate from UTF-8 text; surrogate pair, scalar commit, repeats and control filtering |
| Focus/reset | Stale key discarded; unfinished surrogate discarded across focus loss; lone low surrogate becomes U+FFFD |
| Capture | Actual SetCapture/GetCapture/ReleaseCapture, explicit release and native capture-loss notification |
| Pointer/button | Signed coordinates and primary-button position/pressed state |
| Overflow | Real adapter's 64-event queue, reset priority, stale suppression and exact loss count |
| DPI | Synthetic WM_DPICHANGED reaches scale state and a coalesced surface notification |
| Pixels | Protected RGBA buffer converted and painted through GDI into an off-screen DIB; red/blue ordering and black uncovered area checked |
| Lifetime | Close state/reset/event and HWND destruction on image unload |

These complement the portable headless tests, which exercise image behavior,
authority, deterministic failure injection, frame preservation and surface
generation limits. The GDI bitmap check shares the actual native paint function;
it does not depend on a hidden window having a visible drawable region.

This is automated native-message verification, not a physical keyboard/mouse or
monitor-move test. Actual keyboard layout/dead-key/IME input, multi-monitor DPI
transitions, desktop occlusion and induced native presentation failure/recovery
remain unverified. X11 capture/text/scale/loss parity remains incomplete. Linux
networking remains deliberately stubbed. Milestone10 is still partial; those
limitations must be preserved when assessing its exit gate.

Run `./build-windows.ps1 -Compiler C:/msys64/ucrt64/bin/gcc.exe -Test` from the
repository root. The Windows suite also uses temporary TLS identities and local
loopback networking. There is no separate C test executable or visible GUI test.
