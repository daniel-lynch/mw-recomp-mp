#!/usr/bin/env bash
# Break at the GSC error reporter sub_82271428 (rsi=base), read the REAL parser struct
# = *(base + *(base+0x82000764)) and dump its fields byte-swapped, resolving pointer fields
# to guest strings — looking for the failing script's filename / source pointer.
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
OUT=/tmp/cod4mpdbg; export DISPLAY=:1
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
printf 'log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n' > "$BUILD/cod4_mp.toml"
rm -f "$OUT/gs.fifo" "$OUT/gs.log" "$OUT/gs.err"; mkfifo "$OUT/gs.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/gs.fifo" > "$OUT/gs.log" 2>&1 ) & GP=$!
exec 7>"$OUT/gs.fifo"
printf 'set pagination off\nset confirm off\nhandle SIGSEGV nostop noprint pass\n' >&7
printf 'break sub_82271428\n' >&7
echo "run --game_data_root=/home/dlynch/Games/cod4 > $OUT/gs.err 2>&1" >&7
python3 /home/dlynch/dev/mw-recomp-mp/tools/cod4_mp_pad_only.py 22 d a s2 a s2 a s2 a s8 >/dev/null 2>&1
sleep 1
cat >&7 <<'GDB'
define bswap
  set $bs = (($arg0>>24)&0xff)|(($arg0>>8)&0xff00)|(($arg0<<8)&0xff0000)|(($arg0<<24)&0xff000000)
end
set $base = 0x100000000
set $r = *(unsigned int*)($base + 0x82000764)
set $struct = (($r>>24)&0xff)|(($r>>8)&0xff00)|(($r<<8)&0xff0000)|(($r<<24)&0xff000000)
printf "BASE=%#lx  STRUCT=%#x\n", $base, $struct
set $i = 0
while $i < 32
  set $a = $base + (unsigned long)$struct + $i*4
  bswap *(unsigned int*)$a
  set $fv = $bs
  printf "  +%-3d = %#010x", $i*4, $fv
  if $fv > 0x10000 && $fv < 0x86000000
    printf "   '%.48s'", (char*)($base + (unsigned long)$fv)
  end
  printf "\n"
  set $i = $i + 1
end
GDB
sleep 3
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*
