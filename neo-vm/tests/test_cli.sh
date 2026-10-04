#!/bin/sh
# Integration checks: executable, real image file, exit statuses and roundtrip.
set -eu
vm=$1
workspace=$(mktemp -d)
trap 'rm -rf "$workspace"' EXIT HUP INT TERM

"$vm" check neo-vm/tests/fixtures/counter.neo neo-vm/tests/fixtures/counter.neo > "$workspace/check"
"$vm" format neo-vm/tests/fixtures/counter.neo > "$workspace/formatted.neo"
"$vm" check "$workspace/formatted.neo" > /dev/null
"$vm" clone neo-vm/tests/fixtures/counter.neo > "$workspace/clone.neo"
"$vm" check "$workspace/clone.neo" > /dev/null
result=$("$vm" run "$workspace/clone.neo" counter increment)
case "$result" in
    *'result = 1'*'counter.count = 1'*) ;;
    *) printf '%s\n' "Unexpected run result: $result" >&2; exit 1 ;;
esac
result=$("$vm" tick neo-vm/tests/fixtures/counter.neo 4)
case "$result" in
    *'tick 2: 3 turns'*'counter.count = 3'*'sender.sent = true'*) ;;
    *) printf '%s\n' "Unexpected tick result: $result" >&2; exit 1 ;;
esac
printf '(image (unfinished' > "$workspace/bad.neo"
if "$vm" check "$workspace/bad.neo" > /dev/null 2>&1; then
    echo 'Malformed input unexpectedly succeeded' >&2
    exit 1
fi
if "$vm" run neo-vm/tests/fixtures/counter.neo counter increment 1 > /dev/null 2>&1; then
    echo 'Execution budget was not enforced' >&2
    exit 1
fi
if "$vm" check "$workspace/missing.neo" > /dev/null 2>&1; then
    echo 'Missing file unexpectedly succeeded' >&2
    exit 1
fi
printf '(image)\000garbage' > "$workspace/nul.neo"
if "$vm" check "$workspace/nul.neo" > /dev/null 2>&1; then
    echo 'Embedded NUL was not rejected' >&2
    exit 1
fi
echo 'PASS: CLI loading, execution, tick delivery, formatting, duplication, failure exits'

# Application output is a byte stream, with no CLI result/state banners.
"$vm" --cli neo/terminal.neo terminal greet > "$workspace/greeting" 2> "$workspace/diagnostics"
printf 'hello from neo\n' > "$workspace/expected"
cmp "$workspace/expected" "$workspace/greeting"
test ! -s "$workspace/diagnostics"
echo 'PASS: terminal stream output and diagnostic separation'

# Platform reporting is metadata, independent of grants to a loaded image.
"$vm" platform > "$workspace/platform"
grep -q '^environment = hosted$' "$workspace/platform"
grep -q '^host-os = linux$' "$workspace/platform"
grep -q '^architecture = x86_64$' "$workspace/platform"
grep -q '^virtualization = unknown$' "$workspace/platform"
printf '(image (actor (handlers (probe (body (platform-info (field "host-os")))))))' > "$workspace/platform.neo"
"$vm" run "$workspace/platform.neo" actor probe > "$workspace/platform-result"
grep -q '^result = "linux"$' "$workspace/platform-result"
echo 'PASS: platform CLI and image query'

# The platform file reader accepts exactly the parser limit, rejects truncation,
# and shares the same validation with the stream runner.
printf '(image)' > "$workspace/exact.neo"
dd if=/dev/zero bs=1048569 count=1 2>/dev/null | tr '\000' ' ' >> "$workspace/exact.neo"
"$vm" check "$workspace/exact.neo" > /dev/null
printf ' ' >> "$workspace/exact.neo"
if "$vm" check "$workspace/exact.neo" > /dev/null 2>&1; then
    echo 'Oversized source was silently truncated' >&2; exit 1
fi
if "$vm" --cli "$workspace/nul.neo" terminal greet > /dev/null 2>&1; then
    echo 'Stream runner accepted embedded NUL' >&2; exit 1
fi
echo 'PASS: shared platform source bounds and stream-runner validation'
