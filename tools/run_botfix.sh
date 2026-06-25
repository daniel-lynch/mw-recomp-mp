#!/usr/bin/env bash
# Test the SKIPMTF freeze fix: skips the GSC name-table self-adjusting reorder (sub_8221F1F0) that
# grinds the Server thread during multi-bot spawn. Drive normally into a System Link / Split Screen
# match with bots and see if it gets PAST ~4-5 bots without freezing. Output tee'd to a log Claude reads.
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
LOG=/tmp/cod4mp_drive/cod4mp.log
mkdir -p /tmp/cod4mp_drive
pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_* 2>/dev/null; sleep 1
cd "$BUILD"
COD4_BOTPARALLEL=1 COD4_BOTS="${COD4_BOTS:-8}" \
COD4_MAXCLIENTS=12 COD4_BOTSPAWNGAP=30 \
COD4_GSCINJECT=1 COD4_BOTSPAWN=1 COD4_BOTAI=1 \
DISPLAY=:1 \
LD_LIBRARY_PATH=/home/dlynch/dev/rexglue-sdk/out/install/linux-amd64/lib \
./cod4_mp --game_data_root=/home/dlynch/Games/cod4 2>&1 | tee "$LOG"
