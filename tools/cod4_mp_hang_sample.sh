#!/usr/bin/env bash
# Reproduce the bot-spawn hang under gdb, then sample the spinning Server thread several times:
#  - deep backtrace (bt 40) to see which GSC operation/builtin drives the loop
#  - 4 quick samples of frame #0 + key regs to tell CYCLE (stuck) vs DEGENERATE (advancing)
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
OUT=/tmp/cod4mpdbg; export DISPLAY=:1
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
printf 'log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n' > "$BUILD/cod4_mp.toml"
rm -f "$OUT/s.fifo" "$OUT/s.log" "$OUT/s.err"; mkfifo "$OUT/s.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/s.fifo" > "$OUT/s.log" 2>&1 ) & GP=$!
exec 7>"$OUT/s.fifo"
printf 'set pagination off\nset confirm off\nhandle SIGSEGV nostop noprint pass\n' >&7
echo "run --game_data_root=/home/dlynch/Games/cod4 > $OUT/s.err 2>&1" >&7
python3 /home/dlynch/dev/mw-recomp-mp/tools/cod4_mp_pad_only.py 22 d a s2 a s2 a s2 a s45 >/dev/null 2>&1
PID=$(pgrep -x cod4_mp | head -1); echo "inferior pid=$PID alive=$(kill -0 $PID 2>/dev/null && echo y || echo n)"
# Find the spinning Server thread (not in a futex/Wait), dump deep bt + sample it.
kill -INT "$PID" 2>/dev/null; sleep 3
printf 'echo \\n==FULLBT==\\n\nthread apply all bt 40\n' >&7
sleep 3
# Sample the Server thread (named "Server (...)") 4x: PC + the chain regs r29(next) r5 r3.
for i in 1 2 3 4; do
  printf 'echo \\n==SAMPLE %d==\\n\nthread apply all where 1\n' "$i" >&7
  printf 'continue\n' >&7    # let it run a moment
  sleep 1
  kill -INT "$PID" 2>/dev/null; sleep 1
done
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*
