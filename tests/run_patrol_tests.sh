#!/usr/bin/env bash
# Host tests for the Patrol app: SHA-256/HMAC against Python, and the
# companion report's verifier against logs signed by the app's C code.
set -euo pipefail
cd "$(dirname "$0")/.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cc -O2 -Wall -Wextra -Werror tests/patrol_test_hmac.c patrol/patrol_hmac.c -o "$tmp/test_hmac"
"$tmp/test_hmac" > "$tmp/hmac.txt"
python3 - "$tmp/hmac.txt" <<'PY'
import hashlib, hmac, sys
key = bytes((i * 7 + 3) & 255 for i in range(200))
data = bytes((i * 13 + 1) & 255 for i in range(300))
cases = 0
for line in open(sys.argv[1]):
    k, d, mac = line.split()
    assert hmac.new(key[:int(k)], data[:int(d)], hashlib.sha256).hexdigest() == mac, line
    cases += 1
print(f"ok  HMAC-SHA-256 matches Python in {cases} cases")
PY

cc -O2 -Wall -Wextra -Werror tests/patrol_make_log.c patrol/patrol_hmac.c -o "$tmp/make_log"
"$tmp/make_log" > "$tmp/log.csv" 2> "$tmp/receipt.txt"
receipt="$(awk '{print $2}' "$tmp/receipt.txt")"
node tests/patrol_verify_test.js "$tmp/log.csv" "$receipt" "$tmp/make_log"
echo "All Patrol tests passed"
