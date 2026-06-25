#!/usr/bin/env bash
# At the Start-Match hang, climb out of the innermost frame with repeated `finish`
# on the Main XThread, printing a short bt after each, to find which frame loops
# back (i.e. where `finish` returns but execution re-enters the same chain).
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
OUT=/tmp/cod4mpdbg; export DISPLAY=:1
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
printf 'log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n' > "$BUILD/cod4_mp.toml"
rm -f "$OUT/l.fifo" "$OUT/l.log" "$OUT/l.err"; mkfifo "$OUT/l.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/l.fifo" > "$OUT/l.log" 2>&1 ) & GP=$!
exec 7>"$OUT/l.fifo"
printf 'set pagination off\nset confirm off\nhandle SIGSEGV nostop noprint pass\nhandle SIGINT stop\n' >&7
echo "run --game_data_root=/home/dlynch/Games/cod4 > $OUT/l.err 2>&1" >&7
python3 /home/dlynch/dev/mw-recomp-mp/tools/cod4_mp_pad_only.py 22 d a s2 a s2 a s2 a s45 >/dev/null 2>&1
PID=$(pgrep -x cod4_mp | head -1); echo "inferior pid=$PID"
kill -INT "$PID" 2>/dev/null; sleep 3
# Make sure we're on the Main XThread, then finish-ladder.
printf 'thread find Main XThread\n' >&7; sleep 1
# Switch to the Main XThread (its id printed above as "Thread N has target name");
# gdb leaves current thread = the one that got SIGINT, which is Main XThread here.
for i in 1 2 3 4 5 6 7 8; do
  printf 'echo \\n==FINISH %d==\\n\nbt 4\nfinish\n' "$i" >&7
  sleep 1
done
printf 'echo \\n==AFTER==\\n\nbt 6\n' >&7; sleep 1
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*
