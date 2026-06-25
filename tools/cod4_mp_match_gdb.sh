#!/usr/bin/env bash
set -u
BUILD=/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release
OUT=/tmp/cod4mpdbg; export DISPLAY=:1
mkdir -p "$OUT"; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*; sleep 1
printf 'log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n' > "$BUILD/cod4_mp.toml"
rm -f "$OUT/m.fifo" "$OUT/m.log" "$OUT/m.err"; mkfifo "$OUT/m.fifo"
( cd "$BUILD" && gdb -q ./cod4_mp < "$OUT/m.fifo" > "$OUT/m.log" 2>&1 ) & GP=$!
exec 7>"$OUT/m.fifo"
printf 'set pagination off\nset confirm off\nset breakpoint pending on\n' >&7
printf 'handle SIGSEGV nostop noprint pass\nbreak rex::runtime::InvalidFunctionTrap\n' >&7
echo "run --game_data_root=/home/dlynch/Games/cod4 > $OUT/m.err 2>&1" >&7
# drive Start Match: predelay 20s (boot under gdb), then d a a a a
python3 /home/dlynch/dev/mw-recomp-mp/tools/cod4_mp_pad_only.py 22 d a s2 a s2 a s2 a s35 >/dev/null 2>&1
sleep 5
printf 'printf "MISSING_TARGET=0x%%08X\\n", *(unsigned int*)((char*)$rdi+0x14C)\n' >&7
printf 'printf "CALLSITE_LR=0x%%08X\\n", *(unsigned int*)((char*)$rdi+0x100)\n' >&7
printf 'bt 12\n' >&7
sleep 2
echo 'quit' >&7; sleep 1; kill -9 "$GP" 2>/dev/null; pkill -9 -x cod4_mp 2>/dev/null; rm -f /dev/shm/xenia_memory_*
