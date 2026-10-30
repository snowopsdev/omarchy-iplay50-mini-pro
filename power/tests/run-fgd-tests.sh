#!/bin/sh
# Host-side protocol tests for fgd-kernel-mode: builds a host (glibc) test binary of ../src/fgd-kernel-mode.c with
# -DFGD_TEST_UNIX (AF_UNIX transport instead of NETLINK_FGD) and runs it against fake-kernel.py. Needs cc + python3.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); B=$HERE/..
TMP=$(mktemp -d); trap 'kill $KP 2>/dev/null; rm -rf "$TMP"' EXIT
T=$TMP/fgd-kernel-mode-test
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DFGD_TEST_UNIX -o "$T" "$B/src/fgd-kernel-mode.c" || exit 1
fail=0; ok() { echo "PASS: $*"; }; bad() { echo "FAIL: $*"; fail=1; }
mk() { mkdir -p "$TMP/ps/battery"; printf 'Battery\n' >"$TMP/ps/battery/type"; echo "$1" >"$TMP/ps/battery/capacity"; echo "$2" >"$TMP/ps/battery/init_done"; echo "${3:-0}" >"$TMP/ps/battery/disable"; }
run() { "$T" --psy-root "$TMP/ps" --wait 0 --verify 2 "$@"; }
export FGD_TEST_SOCK=fgd-test-$$

# 1 dry-run sends nothing and exits 0 even without a kernel
mk -1 0; run -n >"$TMP/o1" 2>&1; [ $? -eq 0 ] && grep -q "nothing sent" "$TMP/o1" && ok "dry-run" || bad "dry-run"
grep -q "msg1 0010: 00 00 00 00 01 00 00 00" "$TMP/o1" && ok "dry-run hexdump shows cmd=1 header" || bad "hexdump"
# 2 normal handshake against fake kernel
python3 -I "$HERE/fake-kernel.py" "$TMP/ps" battery normal >"$TMP/k" 2>&1 & KP=$!
for i in 1 2 3 4 5 6 7 8 9 10; do grep -q READY "$TMP/k" && break; sleep 0.2; done
run -v >"$TMP/o2" 2>&1; rc=$?
[ $rc -eq 0 ] && ok "handshake exit 0" || bad "handshake exit $rc ($(cat "$TMP/o2"))"
grep -q "enable Kernel mode Gauge" "$TMP/k" && ok "fake kernel entered kernel mode" || bad "kernel mode not entered"
grep -q "SET_DAEMON_PID" "$TMP/k" && ! grep -q "drop rev\|version error\|vc size\|OVERFLOW" "$TMP/k" && ok "no protocol complaints" || bad "protocol complaints: $(cat "$TMP/k")"
# 3 idempotence: second run does nothing
run -v >"$TMP/o3" 2>&1; rc=$?; grep -q "nothing to do" "$TMP/o3" && [ $rc -eq 0 ] && ok "idempotent re-run" || bad "re-run"
kill $KP; wait $KP 2>/dev/null
# 4 refusals
mk 50 0 1; run >"$TMP/o4" 2>&1; [ $? -eq 2 ] && ok "refuse when disable=1" || bad "disable=1 not refused"
mk 50 1 0; run >"$TMP/o5" 2>&1; [ $? -eq 0 ] && grep -q "nothing to do" "$TMP/o5" && ok "capacity valid => no-op" || bad "valid capacity not no-op"
mk -1 1 0; run >"$TMP/o6" 2>&1; [ $? -eq 2 ] && ok "refuse init_done=1,capacity=-1 without --force" || bad "init_done=1 not refused"
rm -rf "$TMP/ps/battery/disable"; mk -1 0; rm "$TMP/ps/battery/init_done"; run >"$TMP/o7" 2>&1; [ $? -eq 2 ] && ok "refuse non-MTK driver" || bad "non-MTK not refused"
# 5 version match keeps daemon mode => exit 4 (simulate a kernel whose numbers match ours: never in practice)
mk -1 0; python3 -I "$HERE/fake-kernel.py" "$TMP/ps" battery match >"$TMP/k2" 2>&1 & KP=$!
for i in 1 2 3 4 5 6 7 8 9 10; do grep -q READY "$TMP/k2" && break; sleep 0.2; done
run --verify 1 >"$TMP/o8" 2>&1; [ $? -eq 4 ] && ok "exit 4 when capacity stays -1" || bad "exit4 ($(cat "$TMP/o8"))"
kill $KP; wait $KP 2>/dev/null
# 6 no kernel listening
mk -1 0; run --verify 1 >"$TMP/o9" 2>&1; [ $? -eq 3 ] && ok "exit 3 when kernel does not answer" || bad "exit3"
exit $fail
