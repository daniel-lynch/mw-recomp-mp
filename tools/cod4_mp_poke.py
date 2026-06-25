#!/usr/bin/env python3
# Raw guest-VA peek/poke for cod4_mp. The xenia shm maps guest VA == file offset
# (the dvar tool relies on the same fact), so a guest VA can be used directly as an
# mmap offset. All values are big-endian u32 (the guest is PPC/BE).
#
# Usage:
#   cod4_mp_poke.py read   VA                 -> print *(u32*)VA
#   cod4_mp_poke.py write  VA VAL             -> *(u32*)VA = VAL
#   cod4_mp_poke.py dread  BASEVA OFF         -> p=*(u32*)BASEVA; print *(u32*)(p+OFF)
#   cod4_mp_poke.py dwrite BASEVA OFF VAL     -> p=*(u32*)BASEVA; *(u32*)(p+OFF) = VAL
# VA/VAL/OFF accept hex (0x..) or decimal.
import sys, struct, mmap, glob
SHM = glob.glob("/dev/shm/xenia_memory_*")[0]
def num(s): return int(s, 0)
def rd(mm, va): return struct.unpack(">I", mm[va:va+4])[0]
def wr(mm, va, v): mm[va:va+4] = struct.pack(">I", v)
def main():
    a = sys.argv[1:]
    mode = a[0]
    with open(SHM, "r+b") as f:
        mm = mmap.mmap(f.fileno(), 0)
        if mode == "read":
            va = num(a[1]); print(f"*{va:08X} = {rd(mm,va):08X} ({rd(mm,va)})")
        elif mode == "write":
            va, v = num(a[1]), num(a[2]); wr(mm, va, v); mm.flush()
            print(f"*{va:08X} := {v:08X} ({v})  [now {rd(mm,va):08X}]")
        elif mode == "dread":
            base, off = num(a[1]), num(a[2]); p = rd(mm, base); va = p + off
            print(f"*{base:08X}=ptr {p:08X}; *(ptr+{off})=*{va:08X} = {rd(mm,va):08X} ({rd(mm,va)})")
        elif mode == "dwrite":
            base, off, v = num(a[1]), num(a[2]), num(a[3]); p = rd(mm, base); va = p + off
            wr(mm, va, v); mm.flush()
            print(f"*{base:08X}=ptr {p:08X}; *{va:08X} := {v:08X}  [now {rd(mm,va):08X}]")
        else:
            print(f"unknown mode {mode}"); sys.exit(2)
main()
