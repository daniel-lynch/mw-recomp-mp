#!/usr/bin/env bash
# At the Start-Match hang, select the Main XThread and disassemble around the pinned
# spin PC inside sub_82209F20 to confirm which internal loop is spinning + its body.
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
OUT=/tmp/cod4mpdbg; export DISPLAY=:1
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
printf 'log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n' > "$BUILD/cod4_mp.toml"
rm -f "$OUT/da.fifo" "$OUT/da.log" "$OUT/da.err"; mkfifo "$OUT/da.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/da.fifo" > "$OUT/da.log" 2>&1 ) & GP=$!
exec 7>"$OUT/da.fifo"
printf 'set pagination off\nset confirm off\nhandle SIGSEGV nostop noprint pass\n' >&7
echo "run --game_data_root=/home/dlynch/Games/cod4 > $OUT/da.err 2>&1" >&7
python3 /home/dlynch/dev/mw-recomp-mp/tools/cod4_mp_pad_only.py 22 d a s2 a s2 a s2 a s45 >/dev/null 2>&1
PID=$(pgrep -x cod4_mp | head -1); echo "inferior pid=$PID"
kill -INT "$PID" 2>/dev/null; sleep 3
# Reliably select the Main XThread regardless of which thread caught SIGINT.
printf 'python\nfor t in gdb.selected_inferior().threads():\n    if t.name and "Main XThread" in t.name:\n        t.switch(); print("SWITCHED to", t.num, t.name); break\nend\n' >&7
sleep 1
printf 'echo \\n==PC==\\n\nx/i $pc\necho \\n==CTX REGS==\\n\ninfo registers rax rbx rcx rdx rsi rdi rbp r8 r9 r10 r11 r12 r13 r14 r15\n' >&7
printf 'echo \\n==DISAS WINDOW==\\n\ndisassemble $pc-96,$pc+48\n' >&7
sleep 2
# sample PC again 1s later to confirm it stays in the same tiny loop
printf 'echo \\n==PC2==\\n\nx/i $pc\n' >&7
sleep 1
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*
