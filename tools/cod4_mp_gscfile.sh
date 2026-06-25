#!/usr/bin/env bash
# Catch the GSC compiler at error time. Break at Start Match (sub_82200598, rsi=guest base),
# read the codePos struct = *(base + *(base+0x82000764)), watchpoint its +16 codePos field,
# and bt on each write — the LAST write before CompileError (sub_82271428) is the parser at
# the failing file. Stop at sub_82271428.
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
OUT=/tmp/cod4mpdbg; export DISPLAY=:1
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
printf 'log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n' > "$BUILD/cod4_mp.toml"
rm -f "$OUT/gf.fifo" "$OUT/gf.log" "$OUT/gf.err"; mkfifo "$OUT/gf.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/gf.fifo" > "$OUT/gf.log" 2>&1 ) & GP=$!
exec 7>"$OUT/gf.fifo"
printf 'set pagination off\nset confirm off\nhandle SIGSEGV nostop noprint pass\n' >&7
printf 'break *sub_82200598\n' >&7          # bp1 entry: rsi=base
printf 'break sub_82271428\n' >&7           # bp2 CompileError reporter (stop here at end)
echo "run --game_data_root=/home/dlynch/Games/cod4 > $OUT/gf.err 2>&1" >&7
python3 /home/dlynch/dev/mw-recomp-mp/tools/cod4_mp_pad_only.py 22 d a s2 a s2 a s2 a s8 >/dev/null 2>&1
sleep 1
# At bp1 (Start Match). Set up the codePos watchpoint.
cat >&7 <<'GDB'
set $base = $rsi
set $raw = *(unsigned int*)($base + 0x82000764)
set $struct = (($raw>>24)&0xff) | (($raw>>8)&0xff00) | (($raw<<8)&0xff0000) | (($raw<<24)&0xff000000)
set $cpaddr = $base + (unsigned long)$struct + 16
set $cpraw = *(unsigned int*)$cpaddr
printf "BASE=%#lx STRUCT=%#x CODEPOSnow=%#x\n", $base, $struct, (($cpraw>>24)&0xff)|(($cpraw>>8)&0xff00)|(($cpraw<<8)&0xff0000)|(($cpraw<<24)&0xff000000)
watch *(int*)$cpaddr
commands
silent
set $v = *(unsigned int*)$cpaddr
printf "CODEPOS=%#x\n", (($v>>24)&0xff)|(($v>>8)&0xff00)|(($v<<8)&0xff0000)|(($v<<24)&0xff000000)
bt 6
printf "----\n"
continue
end
delete 1
continue
GDB
sleep 8
# Should now be stopped at bp2 (CompileError). Dump final.
printf 'echo \\n==AT COMPILEERROR==\\n\nbt 10\n' >&7
sleep 2
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*
