#!/usr/bin/env bash
# Launch cod4_mp with the GSC var-usage probe on, for manual driving. Tees stderr to a log Claude can read.
# Drive: Main Menu -> down -> A (Split Screen) -> A -> A -> A (Start Match), then let the countdown run.
# When it freezes, just quit/close — the [COD4MP-VARPROBE] lines in the log show the pool fill curve.
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
LOG=/tmp/cod4mp_drive/cod4mp.log
mkdir -p /tmp/cod4mp_drive
pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_* 2>/dev/null; sleep 1
cd "$BUILD"
COD4_VARPROBE=1 \
COD4_BOTS="${COD4_BOTS:-8}" \
COD4_MAXCLIENTS=12 COD4_BOTSPAWNGAP=30 \
COD4_GSCINJECT=1 COD4_BOTSPAWN=1 COD4_BOTAI=1 \
DISPLAY=:1 \
LD_LIBRARY_PATH=/home/dlynch/dev/rexglue-sdk/out/install/linux-amd64/lib \
./cod4_mp --game_data_root=/home/dlynch/Games/cod4 2>&1 | tee "$LOG"
