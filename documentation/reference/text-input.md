# Committed text input

`window-next-event` returns `"text"` for a committed text event. Read its UTF-8
payload with `(window-event (target "window") (field "text"))`. Other event kinds
expose empty text. The existing protected window connection and event snapshot
rules apply; no new primitive or source-loading mechanism is involved.

Each commit contains 1–63 bytes plus a terminating NUL. The core and headless
provider validate UTF-8: malformed sequences, overlong encodings, surrogates,
values above U+10FFFF, ASCII C0 controls and DEL are rejected. Embedded NUL has no
representation. No normalization, grapheme segmentation or editing policy is
performed. Multiple code points may occur in one event; a consumer must not
assume one event equals one character or one grapheme.

Symbolic key events remain separate. Enter, Tab, Backspace and navigation keys
are commands for image behavior; their key names must not be inserted as text.
Focus loss, capture loss and overflow invalidate queued text through the same
input epoch/reset mechanism as other input. Poll before consuming events, then
cancel image-held input state when receiving `input-reset`.

The Windows provider translates `WM_CHAR` UTF-16 and `WM_UNICHAR` scalar commits.
It assembles surrogate pairs and replaces malformed surrogate sequences with
U+FFFD. Input reset discards an unfinished pair. Repeat delivery is bounded by
the event queue and contributes to its normal overflow/loss reporting. Native
control characters do not generate text events.

The portable headless provider accepts validated text for deterministic tests.
Universal selftests cover Unicode boundaries, malformed sequences, surrogate
decoding, image behavior delivery, the byte bound and stale text suppression.
Windows builds compile the native adapter; foreground typing and IME behavior
have not been interactively verified. IME preedit/composition, selection and
caret placement remain pending. X11 currently emits symbolic keys only and does
not implement committed text input.

[Hidden Win32 checks](native-display-validation.md) additionally exercise posted
WM_CHAR messages, surrogate decoding, WM_UNICHAR, repeat counts and control
filtering through the production adapter. Physical typing and IME remain outside
that automated validation.
