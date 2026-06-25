#!/usr/bin/env python3
# Robust MP dvar reader/poker. dvar_t: +0x00 name ptr(BE VA), +0x08 flags(BE), +0x0C value(BE).
# Scans the whole shm for a struct whose +0x00 points at the name string (no region assumptions).
import sys, struct, mmap, glob
SHM = glob.glob("/dev/shm/xenia_memory_*")[0]
IMG_LO, IMG_HI = 0x82000000, 0x85400000   # MP image 0x82000000..+0x33D0000
def find_cstr(mm, name):
    pat = name.encode()+b"\x00"; off = mm.find(pat, IMG_LO, IMG_HI); return off
def find_struct(mm, sva):
    beptr = struct.pack(">I", sva); pos = IMG_LO
    while True:
        i = mm.find(beptr, pos, 0x86000000)
        if i < 0: return -1
        # heuristic: a dvar struct's name ptr is at +0x00; sanity check flags region readable
        return i
def main():
    mode = "read"
    args = sys.argv[1:]
    if args and args[0] == "set": mode="set"; args=args[1:]
    with open(SHM, "r+b") as f:
        mm = mmap.mmap(f.fileno(), 0)
        if mode=="read":
            for n in args:
                s=find_cstr(mm,n)
                if s<0: print(f"{n}: <name not found in image>"); continue
                b=find_struct(mm,s)
                if b<0: print(f"{n}: name@{s:08X} but no struct points at it"); continue
                flags=struct.unpack(">I",mm[b+0x08:b+0x0C])[0]
                raw=mm[b+0x0C:b+0x10]; fval=struct.unpack(">f",raw)[0]; ival=struct.unpack(">I",raw)[0]
                isf=bool(flags&0x100)
                print(f"{n}: {'%.4f'%fval if isf else ival}  (struct@{b:08X} name@{s:08X} flags={flags:08X} raw={ival:08X})")
        else:
            n,val=args[0],int(args[1]); s=find_cstr(mm,n); b=find_struct(mm,s)
            if b<0: print(f"{n}: not found"); return
            mm[b+0x0C:b+0x10]=struct.pack(">I",val); mm.flush()
            print(f"{n}: SET to {val} at struct@{b:08X}")
main()
