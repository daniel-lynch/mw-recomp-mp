#!/usr/bin/env bash
# Catch the FAILING call to sub_82209F20 (negative length) live and dump the real
# format-spec state. Gate on Start Match (sub_82200598) so the conditional bp on
# sub_82209F20 is only armed during Start Match (no boot slowdown / driver desync).
# At sub_82209F20 entry: $rdi=&ctx, $rsi=base. r4=0x20 r5=0x28 r22=0xB0 r23=0xB8 r29=0xE8.
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
OUT=/tmp/cod4mpdbg; export DISPLAY=:1
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
printf 'log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n' > "$BUILD/cod4_mp.toml"
rm -f "$OUT/bc.fifo" "$OUT/bc.log" "$OUT/bc.err"; mkfifo "$OUT/bc.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/bc.fifo" > "$OUT/bc.log" 2>&1 ) & GP=$!
exec 7>"$OUT/bc.fifo"
printf 'set pagination off\nset confirm off\nhandle SIGSEGV nostop noprint pass\n' >&7
printf 'break sub_82200598\n' >&7   # Start Match gate
echo "run --game_data_root=/home/dlynch/Games/cod4 > $OUT/bc.err 2>&1" >&7
# Drive menus; the final 'a' presses Start Match -> hits the gate bp.
python3 /home/dlynch/dev/mw-recomp-mp/tools/cod4_mp_pad_only.py 22 d a s2 a s2 a s2 a s10 >/dev/null 2>&1
sleep 1
# Now (expected stopped at Start Match gate) arm the conditional bp on the failing call.
cat >&7 <<'GDB'
delete breakpoints
break *sub_82209F20 if *(int*)($rdi + 0x20) < 0
continue
GDB
sleep 6
cat >&7 <<'GDB'
echo \n==BAD CALL CAUGHT==\n
printf "r4(len)=%d  r5(src)=%#x  r22(spec_start)=%#x  r23(fmtctx)=%#x  r29(scan)=%#x\n", *(int*)($rdi+0x20), *(unsigned int*)($rdi+0x28), *(unsigned int*)($rdi+0xB0), *(unsigned int*)($rdi+0xB8), *(unsigned int*)($rdi+0xE8)
printf "r29-r22=%d   *(r23+8412)=%d (%#x)\n", (int)(*(unsigned int*)($rdi+0xE8) - *(unsigned int*)($rdi+0xB0)), *(int*)($rsi + (unsigned long)*(unsigned int*)($rdi+0xB8) + 8412), *(unsigned int*)($rsi + (unsigned long)*(unsigned int*)($rdi+0xB8) + 8412)
echo \n==format string @ r22-32 (160)==\n
x/160bc ($rsi + (unsigned long)*(unsigned int*)($rdi+0xB0) - 32)
echo \n==stack==\n
bt 14
GDB
sleep 3
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*
