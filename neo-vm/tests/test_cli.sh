#!/bin/sh
# Integration checks: executable, real image file, exit statuses and roundtrip.
set -eu
vm=$1
workspace=$(mktemp -d)
trap 'rm -rf "$workspace"' EXIT HUP INT TERM

"$vm" check neo/image.neo neo/image.neo > "$workspace/check"
"$vm" format neo/image.neo > "$workspace/formatted.neo"
"$vm" check "$workspace/formatted.neo" > /dev/null
"$vm" clone neo/image.neo > "$workspace/clone.neo"
"$vm" check "$workspace/clone.neo" > /dev/null
result=$("$vm" run "$workspace/clone.neo" counter increment)
case "$result" in
    *'result = 1'*'counter.count = 1'*) ;;
    *) printf '%s\n' "Unexpected run result: $result" >&2; exit 1 ;;
esac
result=$("$vm" tick neo/image.neo 4)
case "$result" in
    *'tick 2: 3 turns'*'counter.count = 3'*'sender.sent = true'*) ;;
    *) printf '%s\n' "Unexpected tick result: $result" >&2; exit 1 ;;
esac
printf '(image (unfinished' > "$workspace/bad.neo"
if "$vm" check "$workspace/bad.neo" > /dev/null 2>&1; then
    echo 'Malformed input unexpectedly succeeded' >&2
    exit 1
fi
if "$vm" run neo/image.neo counter increment 1 > /dev/null 2>&1; then
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
