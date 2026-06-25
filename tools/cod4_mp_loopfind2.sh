#!/usr/bin/env bash
# Pin the spin-loop frame in the Start-Match hang by BREAKPOINT HIT COUNTS.
# At the hang we are already inside the loop. Set auto-continuing breakpoints on
# every function in the spin chain; let the loop iterate ~2s; read `info breakpoints`.
# Frames at/below the loop body get re-hit each iteration; the loop frame itself and
# everything above it are NOT re-hit. => loop frame = (highest re-hit frame) + 1, i.e.
# the function that calls the highest re-hit function.
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
OUT=/tmp/cod4mpdbg; export DISPLAY=:1
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
printf 'log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n' > "$BUILD/cod4_mp.toml"
rm -f "$OUT/l2.fifo" "$OUT/l2.log" "$OUT/l2.err"; mkfifo "$OUT/l2.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/l2.fifo" > "$OUT/l2.log" 2>&1 ) & GP=$!
exec 7>"$OUT/l2.fifo"
printf 'set pagination off\nset confirm off\nset breakpoint pending on\nhandle SIGSEGV nostop noprint pass\n' >&7
echo "run --game_data_root=/home/dlynch/Games/cod4 > $OUT/l2.err 2>&1" >&7
python3 /home/dlynch/dev/mw-recomp-mp/tools/cod4_mp_pad_only.py 22 d a s2 a s2 a s2 a s45 >/dev/null 2>&1
PID=$(pgrep -x cod4_mp | head -1); echo "inferior pid=$PID"
kill -INT "$PID" 2>/dev/null; sleep 3
# Auto-continuing breakpoints on the whole spin chain (#0..#5 plus the Start-Match-side fns).
for fn in sub_82209F20 sub_8220A068 sub_8220B328 sub_8220CF20 sub_82220518 sub_82220780 sub_82263760 sub_82271428; do
  printf 'break %s\ncommands\nsilent\ncontinue\nend\n' "$fn" >&7
done
sleep 1
printf 'continue\n' >&7
sleep 3
kill -INT "$PID" 2>/dev/null; sleep 2
printf 'info breakpoints\n' >&7
sleep 2
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*
