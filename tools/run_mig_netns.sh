#!/usr/bin/env bash
# Two-instance Find-Match harness, NETNS edition: host A in ns_a (10.0.0.1), joiner B in ns_b (10.0.0.2),
# so the two instances behave like two real machines (each binds the REAL Xbox ports, no same-box NAT).
# Prereq (one-time, as root):  sudo bash tools/netns_setup.sh
#
# COD4_LOCAL_IP=<veth ip> turns on netns mode in the SDK (real-port bind + peer reached at its real IP, the
# 127.0.0.x port-NAT/GAMEROUTE bypassed). Each game is launched inside its namespace via
# `sudo ip netns exec ns_X runuser -u <user>` (passwordless rule installed by netns_setup.sh), dropping back
# to the user so X11/GPU/pads work normally (netns isolates only the network).
#
# Per-instance extra env via MIG_A_EXTRA / MIG_B_EXTRA. Drive after launch with:
#   printf 'a s3 a s3 a\n' > "$(cat /tmp/padA.cur)"      # host A
#   printf 'a s3 a s3 a\n' > "$(cat /tmp/padB.cur)"      # joiner B
set -u
REPO=/home/dlynch/dev/mw-recomp-mp
BIN_DIR=$REPO/out/build/linux-amd64-release
USER_NAME="${USER:-dlynch}"
LDP=/home/dlynch/dev/rexglue-sdk/out/install/linux-amd64/lib
A_EXTRA="${MIG_A_EXTRA:-}"
B_EXTRA="${MIG_B_EXTRA:-}"
mkdir -p /tmp/cod4_p2_data

# sanity: namespaces present?
if ! ip netns list 2>/dev/null | grep -q ns_a; then
  echo "!! ns_a/ns_b not found — run:  sudo bash tools/netns_setup.sh  first"; exit 1
fi

mm_clean() {
  for p in $(pgrep -x cod4_mp); do kill -9 "$p" 2>/dev/null; done
  pkill -9 -f 'pad_daemon\.py' 2>/dev/null
  rm -f /tmp/padA.*.fifo /tmp/padB.*.fifo 2>/dev/null
  for f in /dev/shm/xenia_memory_*; do rm -f "$f"; done 2>/dev/null
  rm -f /tmp/cod4_mp_sessions/* 2>/dev/null
  sleep 2
}

launch_pads() {
  local ts; ts=$(date +%s%N)
  local fa=/tmp/padA.$ts.fifo fb=/tmp/padB.$ts.fifo
  echo "$fa" > /tmp/padA.cur; echo "$fb" > /tmp/padB.cur
  setsid python3 "$REPO/tools/pad_daemon.py" --product 0x028e --fifo "$fa" >/tmp/padA.log 2>&1 < /dev/null &
  setsid python3 "$REPO/tools/pad_daemon.py" --product 0x028f --fifo "$fb" >/tmp/padB.log 2>&1 < /dev/null &
  sleep 2
}

launch_games() {
  # A — HOST in ns_a (10.0.0.1). $A_EXTRA intentionally unquoted (word-split into env args).
  setsid sudo ip netns exec ns_a runuser -u "$USER_NAME" -- \
    env HOME=/home/dlynch DISPLAY=:1 LD_LIBRARY_PATH="$LDP" \
        SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x045e/0x028e" \
        COD4_LIVE=1 COD4_PLAYLIST=1 COD4_MMHOST=1 COD4_MAXCLIENTS=12 COD4_MM_SEARCH_DELAY_MS=6000 \
        COD4_MM_BROKER=1 COD4_MM_REGISTRY=/tmp/cod4_mp_sessions \
        COD4_MM_NETLOG_DUMP=1 COD4_MM_NETLOG_BUDGET=200000 COD4_MM_STARTTRACE=1 \
        COD4_LOCAL_IP=10.0.0.1 \
        $A_EXTRA \
        "$BIN_DIR/cod4_mp" --game_data_root=/home/dlynch/Games/cod4 >/tmp/cod4_A.log 2>&1 < /dev/null &
  sleep 14
  # B — JOINER in ns_b (10.0.0.2); separate writable data dir.
  setsid sudo ip netns exec ns_b runuser -u "$USER_NAME" -- \
    env HOME=/home/dlynch DISPLAY=:1 LD_LIBRARY_PATH="$LDP" \
        SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x045e/0x028f" XDG_DATA_HOME=/tmp/cod4_p2_data \
        COD4_LIVE=1 COD4_PLAYLIST=1 COD4_MM_BROKER=1 COD4_MM_REGISTRY=/tmp/cod4_mp_sessions \
        COD4_MM_NETLOG_DUMP=1 COD4_MM_NETLOG_BUDGET=200000 COD4_MM_STARTTRACE=1 \
        COD4_MM_NOKICK=1 COD4_MM_SPAWNB=1 COD4_MM_SVPROBE=1 \
        COD4_LOCAL_IP=10.0.0.2 \
        $B_EXTRA \
        "$BIN_DIR/cod4_mp" --game_data_root=/home/dlynch/Games/cod4 >/tmp/cod4_B.log 2>&1 < /dev/null &
  sleep 16
}

ok=0
for attempt in 1 2 3; do
  mm_clean
  launch_pads
  launch_games
  alive=$(pgrep -c -x cod4_mp)
  aL=$(wc -l </tmp/cod4_A.log 2>/dev/null || echo 0); bL=$(wc -l </tmp/cod4_B.log 2>/dev/null || echo 0)
  if [ "${alive:-0}" -ge 2 ]; then ok=1; echo "attempt $attempt: BOTH UP (A=$aL B=$bL)"; break; fi
  echo "attempt $attempt: only ${alive:-0} alive (A=$aL B=$bL) — retrying"
done
[ "$ok" = 1 ] || echo "!! FAILED to boot both instances after 3 attempts (check /tmp/cod4_A.log /tmp/cod4_B.log) !!"

mapfile -t WIDS < <(DISPLAY=:1 wmctrl -l 2>/dev/null | awk '/cod4_mp/{print $1}')
[ -n "${WIDS[0]:-}" ] && DISPLAY=:1 wmctrl -ir "${WIDS[0]}" -e 0,5120,200,1280,720 2>/dev/null
[ -n "${WIDS[1]:-}" ] && DISPLAY=:1 wmctrl -ir "${WIDS[1]}" -e 0,6400,200,1280,720 2>/dev/null
echo "cod4_mp alive: $(pgrep -c -x cod4_mp)"
DISPLAY=:1 wmctrl -lG 2>/dev/null | grep -i cod4_mp
