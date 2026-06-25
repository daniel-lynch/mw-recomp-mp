#!/usr/bin/env bash
# At the Start-Match hang, read the printf format-spec pointers (guest r22=spec start,
# r29=scan ptr) and the format string itself out of the shared PPCContext, to see WHICH
# string + %-spec drives the negative copy length. ctx=$rbx, base=$r14 at the pinned PC.
# PPCRegister=8 bytes; offsets: r5=0x28 r22=0xB0 r23=0xB8 r29=0xE8 r4=0x20.
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
OUT=/tmp/cod4mpdbg; export DISPLAY=:1
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
printf 'log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n' > "$BUILD/cod4_mp.toml"
rm -f "$OUT/sd.fifo" "$OUT/sd.log" "$OUT/sd.err"; mkfifo "$OUT/sd.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/sd.fifo" > "$OUT/sd.log" 2>&1 ) & GP=$!
exec 7>"$OUT/sd.fifo"
printf 'set pagination off\nset confirm off\nhandle SIGSEGV nostop noprint pass\n' >&7
echo "run --game_data_root=/home/dlynch/Games/cod4 > $OUT/sd.err 2>&1" >&7
python3 /home/dlynch/dev/mw-recomp-mp/tools/cod4_mp_pad_only.py 22 d a s2 a s2 a s2 a s45 >/dev/null 2>&1
PID=$(pgrep -x cod4_mp | head -1); echo "inferior pid=$PID"
kill -INT "$PID" 2>/dev/null; sleep 3
printf 'python\nfor t in gdb.selected_inferior().threads():\n    if t.name and "Main XThread" in t.name:\n        t.switch(); print("SWITCHED", t.num); break\nend\n' >&7
sleep 1
cat >&7 <<'GDB'
set $ctx = $rbx
set $base = $r14
set $r5  = *(unsigned int*)($ctx+0x28)
set $r22 = *(unsigned int*)($ctx+0xB0)
set $r23 = *(unsigned int*)($ctx+0xB8)
set $r29 = *(unsigned int*)($ctx+0xE8)
echo \n==REGS==\n
printf "base=%#lx ctx=%#lx\n", $base, $ctx
printf "r5(src) =%#x\n", $r5
printf "r22(spec_start)=%#x\n", $r22
printf "r23(fmtctx)    =%#x\n", $r23
printf "r29(scan_ptr)  =%#x\n", $r29
printf "speclen r29-r22 = %d\n", (int)($r29 - $r22)
printf "field *(r23+8412) = %d\n", (int)*(int*)($base + (unsigned long)$r23 + 8412)
echo \n==BYTES @ r22-48 (160) [format string context]==\n
x/160bc ($base + (unsigned long)$r22 - 48)
echo \n==BYTES @ r29-16 (48) [around scan ptr]==\n
x/48bc ($base + (unsigned long)$r29 - 16)
echo \n==BYTES @ r5 (48) [copy source]==\n
x/48bc ($base + (unsigned long)$r5)
GDB
sleep 3
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*
