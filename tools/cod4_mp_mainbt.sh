#!/usr/bin/env bash
# Relaunch under gdb, reach the "Awaiting challenge" hang, then capture the FULL
# Main XThread backtrace twice (1s apart) to see the loop root and detect a spin.
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
OUT=/tmp/cod4mpdbg; export DISPLAY=:1
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
printf 'log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n' > "$BUILD/cod4_mp.toml"
rm -f "$OUT/m.fifo" "$OUT/m.log" "$OUT/m.err"; mkfifo "$OUT/m.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/m.fifo" > "$OUT/m.log" 2>&1 ) & GP=$!
exec 7>"$OUT/m.fifo"
printf 'set pagination off\nset confirm off\nhandle SIGSEGV nostop noprint pass\n' >&7
echo "run --game_data_root=/home/dlynch/Games/cod4 > $OUT/m.err 2>&1" >&7
python3 /home/dlynch/dev/mw-recomp-mp/tools/cod4_mp_pad_only.py 22 d a s2 a s2 a s2 a s45 >/dev/null 2>&1
PID=$(pgrep -x cod4_mp | head -1); echo "inferior pid=$PID"
# find the Main XThread gdb thread-id by name
kill -INT "$PID" 2>/dev/null; sleep 3
printf 'thread find Main XThread\n' >&7; sleep 1
printf 'thread apply all where 2\n' >&7; sleep 1   # short, to map ids->names
# Full bt of every thread whose name is Main XThread, sampled twice
printf 'set $i=0\n' >&7
printf 'thread apply all -ascending bt\n' >&7; sleep 2
# resume briefly then re-interrupt to sample again
printf 'continue\n' >&7; sleep 1; kill -INT "$PID" 2>/dev/null; sleep 2
printf 'thread apply all -ascending bt\n' >&7 2>/dev/null; sleep 2
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*
