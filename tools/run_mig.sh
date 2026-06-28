#!/usr/bin/env bash
# Two-instance Find-Match harness: host A + joiner B, each with the packet-payload tracer
# (COD4_MM_NETLOG_DUMP) + connstate timeline (COD4_MM_STARTTRACE). Robust against the transient two-instance
# boot crash via a launch-verify-RETRY loop, and against phantom pad input via per-run UNIQUE fifo paths +
# a full daemon kill each attempt.
#
# Extra per-instance env (space-separated KEY=VAL) is injected via MIG_A_EXTRA / MIG_B_EXTRA, so variants
# (bots, probes) are thin wrappers instead of fragile copies. Drive after launch with:
#   printf 'a s3 a s3 a\n' > "$(cat /tmp/padA.cur)"      # host A: Live -> Find Match -> commit
#   printf 'a s3 a s3 a\n' > "$(cat /tmp/padB.cur)"      # joiner B
set -u
REPO=/home/dlynch/dev/mw-recomp-mp
BIN_DIR=$REPO/out/build/linux-amd64-release
export LD_LIBRARY_PATH=/home/dlynch/dev/rexglue-sdk/out/install/linux-amd64/lib:${LD_LIBRARY_PATH:-}
A_EXTRA="${MIG_A_EXTRA:-}"
B_EXTRA="${MIG_B_EXTRA:-}"
mkdir -p /tmp/cod4_p2_data

mm_clean() {
  # cod4 by exact name; pad daemons by cmdline (pkill -f DOES reach setsid/systemd-reparented daemons).
  for p in $(pgrep -x cod4_mp); do kill -9 "$p" 2>/dev/null; done
  pkill -9 -f 'pad_daemon\.py' 2>/dev/null
  # Old per-run fifos: a stale blocked `printf > fifo` writer can only reach its OWN (old) path, so a fresh
  # unique path makes leftover writers harmless; still remove the old fifo files to keep /tmp tidy.
  rm -f /tmp/padA.*.fifo /tmp/padB.*.fifo 2>/dev/null
  for f in /dev/shm/xenia_memory_*; do rm -f "$f"; done 2>/dev/null
  rm -f /tmp/cod4_mp_sessions/* 2>/dev/null
  sleep 2
}

launch_pads() {
  local ts; ts=$(date +%s%N)                       # nanosecond-unique so paths never collide across runs
  local fa=/tmp/padA.$ts.fifo fb=/tmp/padB.$ts.fifo
  echo "$fa" > /tmp/padA.cur; echo "$fb" > /tmp/padB.cur
  setsid python3 "$REPO/tools/pad_daemon.py" --product 0x028e --fifo "$fa" >/tmp/padA.log 2>&1 < /dev/null &
  setsid python3 "$REPO/tools/pad_daemon.py" --product 0x028f --fifo "$fb" >/tmp/padB.log 2>&1 < /dev/null &
  sleep 2
}

launch_games() {
  cd "$BIN_DIR"
  # A — HOST (sees only pad A). $A_EXTRA is intentionally unquoted (word-split into env args).
  setsid env SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x045e/0x028e" \
    COD4_LIVE=1 COD4_PLAYLIST=1 COD4_MMHOST=1 COD4_MAXCLIENTS=12 COD4_MM_SEARCH_DELAY_MS=6000 \
    COD4_MM_BROKER=1 COD4_MM_REGISTRY=/tmp/cod4_mp_sessions \
    COD4_MM_NETLOG_DUMP=1 COD4_MM_NETLOG_BUDGET=200000 COD4_MM_STARTTRACE=1 \
    $A_EXTRA \
    DISPLAY=:1 ./cod4_mp --game_data_root=/home/dlynch/Games/cod4 >/tmp/cod4_A.log 2>&1 < /dev/null &
  sleep 14
  # B — JOINER (sees only pad B; separate writable data dir).
  setsid env SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x045e/0x028f" XDG_DATA_HOME=/tmp/cod4_p2_data \
    COD4_LIVE=1 COD4_PLAYLIST=1 COD4_MM_BROKER=1 COD4_MM_REGISTRY=/tmp/cod4_mp_sessions \
    COD4_MM_NETLOG_DUMP=1 COD4_MM_NETLOG_BUDGET=200000 COD4_MM_STARTTRACE=1 \
    COD4_MM_GAMEROUTE=1 COD4_MM_NOKICK=1 COD4_MM_SPAWNB=1 COD4_MM_SVPROBE=1 \
    $B_EXTRA \
    DISPLAY=:1 ./cod4_mp --game_data_root=/home/dlynch/Games/cod4 >/tmp/cod4_B.log 2>&1 < /dev/null &
  sleep 16
  cd "$REPO"
}

ok=0
for attempt in 1 2 3; do
  mm_clean
  launch_pads
  launch_games
  alive=$(pgrep -c -x cod4_mp)
  aL=$(wc -l </tmp/cod4_A.log 2>/dev/null || echo 0); bL=$(wc -l </tmp/cod4_B.log 2>/dev/null || echo 0)
  if [ "${alive:-0}" -ge 2 ]; then ok=1; echo "attempt $attempt: BOTH UP (A=$aL B=$bL)"; break; fi
  echo "attempt $attempt: only ${alive:-0} alive (A=$aL B=$bL) — transient boot crash, retrying"
done
[ "$ok" = 1 ] || echo "!! FAILED to boot both instances after 3 attempts !!"

# side-by-side window placement
mapfile -t WIDS < <(DISPLAY=:1 wmctrl -l 2>/dev/null | awk '/cod4_mp/{print $1}')
[ -n "${WIDS[0]:-}" ] && DISPLAY=:1 wmctrl -ir "${WIDS[0]}" -e 0,5120,200,1280,720 2>/dev/null
[ -n "${WIDS[1]:-}" ] && DISPLAY=:1 wmctrl -ir "${WIDS[1]}" -e 0,6400,200,1280,720 2>/dev/null

echo "cod4_mp alive: $(pgrep -c -x cod4_mp) | pads READY: $(grep -c READY /tmp/padA.log /tmp/padB.log 2>/dev/null | awk -F: '{s+=$2}END{print s}') | phantom: A=$(grep -vc READY /tmp/padA.log 2>/dev/null) B=$(grep -vc READY /tmp/padB.log 2>/dev/null)"
DISPLAY=:1 wmctrl -lG 2>/dev/null | grep -i cod4_mp
