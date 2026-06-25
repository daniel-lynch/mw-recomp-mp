#!/usr/bin/env bash
# cod4_mp_nullcall.sh — Wall-1 drain probe for iw3mp. Boots cod4_mp under gdb with a
# pending breakpoint on rex::runtime::InvalidFunctionTrap and prints the missing guest
# target (PPCContext+0x14C) + callsite (ctx.lr, +0x100). Mirrors the SP harness.
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$REPO/out/build/linux-amd64-release"
GAMEDIR="${COD4_GAMEDIR:-/home/dlynch/Games/cod4}"
OUT=/tmp/cod4mpdbg; WAIT="${1:-16}"; export DISPLAY="${DISPLAY:-:1}"
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
restore(){ pkill -9 -x cod4_mp 2>/dev/null; pkill -9 gdb 2>/dev/null; rm -f "$OUT/nc.fifo" /dev/shm/xenia_memory_*; }
trap restore EXIT
rm -f "$OUT/nc.fifo" "$OUT/nc.log" "$OUT/nc.err"; mkfifo "$OUT/nc.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/nc.fifo" > "$OUT/nc.log" 2>&1 ) & GP=$!
exec 7>"$OUT/nc.fifo"
printf 'set pagination off\nset confirm off\nset breakpoint pending on\n' >&7
printf 'handle SIGSEGV nostop noprint pass\nbreak rex::runtime::InvalidFunctionTrap\n' >&7
echo "run $GAMEDIR > $OUT/nc.err 2>&1" >&7
sleep "$WAIT"
printf 'printf "MISSING_TARGET=0x%%08X\\n", *(unsigned int*)((char*)$rdi+0x14C)\n' >&7
printf 'printf "CALLSITE_LR=0x%%08X\\n", *(unsigned int*)((char*)$rdi+0x100)\n' >&7
printf 'bt 10\n' >&7
sleep 2
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null
echo "==== nullcall result ===="
if grep -q "hit Breakpoint 1" "$OUT/nc.log"; then
  grep -E "MISSING_TARGET|CALLSITE_LR" "$OUT/nc.log"
  echo "-- trap callstack --"; grep -E "^#[0-9]+ " "$OUT/nc.log" | head -10
else
  echo "NO TRAP within ${WAIT}s — Wall 1 drained, or hung/other exit."
  echo "last stderr:"; tail -6 "$OUT/nc.err"
fi
