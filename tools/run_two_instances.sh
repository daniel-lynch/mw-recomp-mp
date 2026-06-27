#!/usr/bin/env bash
# Two-instance test harness for friend/P2P work. Spawns two independently-driven virtual pads (distinct
# product ids) and two cod4_mp instances, each filtered to ONLY its own pad via SDL's ignore-devices hint —
# so one controller no longer drives both. Instance A = HOST (Find-Match + bots); B = JOINER (plain client,
# separate data dir, will `connect` to A). Drive them with:  echo 'd d a' > /tmp/padA.fifo   (A)
#                                                            echo 'd d a' > /tmp/padB.fifo   (B)
set -u
REPO=/home/dlynch/dev/mw-recomp-mp
BIN_DIR=$REPO/out/build/linux-amd64-release
export LD_LIBRARY_PATH=/home/dlynch/dev/rexglue-sdk/out/install/linux-amd64/lib:${LD_LIBRARY_PATH:-}

pkill -9 -x cod4_mp 2>/dev/null; pkill -9 -f pad_daemon 2>/dev/null; sleep 1
for f in /dev/shm/xenia_memory_*; do rm -f "$f"; done 2>/dev/null
mkdir -p /tmp/cod4_p2_data

# pads first (pad-before-game = reliable bind); distinct products 0x028e (A) / 0x028f (B)
setsid python3 "$REPO/tools/pad_daemon.py" --product 0x028e --fifo /tmp/padA.fifo >/tmp/padA.log 2>&1 < /dev/null &
setsid python3 "$REPO/tools/pad_daemon.py" --product 0x028f --fifo /tmp/padB.fifo >/tmp/padB.log 2>&1 < /dev/null &
sleep 2

cd "$BIN_DIR"
# Instance A — HOST (sees only pad A)
setsid env SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x045e/0x028e" \
  COD4_LIVE=1 COD4_PLAYLIST=1 COD4_MMHOST=1 COD4_MAXCLIENTS=12 COD4_MM_SEARCH_DELAY_MS=6000 \
  COD4_MM_BROKER=1 COD4_MM_REGISTRY=/tmp/cod4_mp_sessions \
  COD4_GSCINJECT=1 COD4_BOTSPAWN=1 COD4_BOTAI=1 COD4_BOTSETTLE=120 COD4_BOTNAMES=1 COD4_STATS=1 \
  DISPLAY=:1 ./cod4_mp --game_data_root=/home/dlynch/Games/cod4 >/tmp/cod4_A.log 2>&1 < /dev/null &
sleep 14
# Instance B — JOINER (sees only pad B; separate writable data dir so profiles don't collide).
# Primary path: matchmaking broker — B runs Find Match, its XSessionSearch reads the shared registry,
# discovers A's published session and JOINS it through the title's Live-join (XNet set up by the join).
# So B gets COD4_MM_BROKER + the same Find-Match playlist config, but does NOT host (no MMHOST/delay).
# Fallback path (plan-B direct connect): set COD4_HOST_IP=<ip:port> when invoking to instead have B
# issue `connect <ip>` from its menu via COD4_AUTOCONNECT (bypasses matchmaking; XNet may reject it).
setsid env SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x045e/0x028f" XDG_DATA_HOME=/tmp/cod4_p2_data \
  COD4_LIVE=1 COD4_PLAYLIST=1 COD4_MM_BROKER=1 COD4_MM_REGISTRY=/tmp/cod4_mp_sessions \
  ${COD4_HOST_IP:+COD4_AUTOCONNECT="$COD4_HOST_IP"} \
  DISPLAY=:1 ./cod4_mp --game_data_root=/home/dlynch/Games/cod4 >/tmp/cod4_B.log 2>&1 < /dev/null &
sleep 14

# place both windows side by side on monitor 3 (DP-2 @ x=5120, 2560 wide); first listed = host A (left)
mapfile -t WIDS < <(DISPLAY=:1 wmctrl -l 2>/dev/null | awk '/cod4_mp/{print $1}')
if [ -n "${WIDS[0]:-}" ]; then
  DISPLAY=:1 wmctrl -ir "${WIDS[0]}" -b remove,maximized_vert,maximized_horz 2>/dev/null
  DISPLAY=:1 wmctrl -ir "${WIDS[0]}" -e 0,5120,200,1280,720 2>/dev/null
fi
if [ -n "${WIDS[1]:-}" ]; then
  DISPLAY=:1 wmctrl -ir "${WIDS[1]}" -b remove,maximized_vert,maximized_horz 2>/dev/null
  DISPLAY=:1 wmctrl -ir "${WIDS[1]}" -e 0,6400,200,1280,720 2>/dev/null
fi

echo "cod4_mp instances alive: $(pgrep -c -x cod4_mp)"
echo "pad daemons: $(pgrep -fc pad_daemon)"
echo "drive: echo 'd d a' > /tmp/padA.fifo (host)  |  echo 'd d a' > /tmp/padB.fifo (joiner)"
DISPLAY=:1 wmctrl -lG 2>/dev/null | grep -i cod4_mp
