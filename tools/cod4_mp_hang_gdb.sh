#!/usr/bin/env bash
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
OUT=/tmp/cod4mpdbg; export DISPLAY=:1
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
printf 'log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n' > "$BUILD/cod4_mp.toml"
rm -f "$OUT/h.fifo" "$OUT/h.log" "$OUT/h.err"; mkfifo "$OUT/h.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/h.fifo" > "$OUT/h.log" 2>&1 ) & GP=$!
exec 7>"$OUT/h.fifo"
printf 'set pagination off\nset confirm off\nhandle SIGSEGV nostop noprint pass\n' >&7
echo "run --game_data_root=/home/dlynch/Games/cod4 > $OUT/h.err 2>&1" >&7
python3 /home/dlynch/dev/mw-recomp-mp/tools/cod4_mp_pad_only.py 22 d a s2 a s2 a s2 a s45 >/dev/null 2>&1   # reach 100% hang
PID=$(pgrep -x cod4_mp | head -1); echo "inferior pid=$PID alive=$(kill -0 $PID 2>/dev/null && echo y || echo n)"
kill -INT "$PID" 2>/dev/null; sleep 3                                       # SIGINT inferior -> gdb stops
printf 'thread apply all bt 10\n' >&7
sleep 4
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*
