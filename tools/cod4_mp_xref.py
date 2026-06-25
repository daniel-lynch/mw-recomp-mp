#!/usr/bin/env python3
# Find guest functions that load a given data VA, by matching `lis rX,A` followed by
# `addi rX,rX,B` / `ori rX,rX,B` on the SAME register (within a small window) and
# computing the resulting address. Reports file:line + enclosing DEFINE_REX_FUNC.
# Usage: cod4_mp_xref.py 0x820BC400 [0x...]
import sys, re, glob
GEN = "/home/dlynch/dev/mw-recomp-mp/generated"
targets = [int(x, 16) for x in sys.argv[1:]]
lis_re  = re.compile(r'//\s*lis\s+(r\d+),(-?\d+)')
add_re  = re.compile(r'//\s*addi\s+(r\d+),(r\d+),(-?\d+)')
ori_re  = re.compile(r'//\s*ori\s+(r\d+),(r\d+),(-?\d+)')
def u32(v): return v & 0xFFFFFFFF
for f in sorted(glob.glob(f"{GEN}/cod4_mp_recomp.*.cpp")):
    lines = open(f).read().split("\n")
    high = {}           # reg -> (value, line_idx)
    func, funcline = "?", 0
    for i, ln in enumerate(lines):
        if ln.startswith("DEFINE_REX_FUNC("):
            func = ln[ln.find("(")+1:ln.find(")")]; funcline = i+1
        m = lis_re.search(ln)
        if m:
            high[m.group(1)] = (u32(int(m.group(2)) << 16), i); continue
        for rx, isori in ((add_re, False), (ori_re, True)):
            m = rx.search(ln)
            if not m: continue
            dst, src, imm = m.group(1), m.group(2), int(m.group(3))
            if src in high and (i - high[src][1]) < 40:
                base = high[src][0]
                va = u32(base | (imm & 0xFFFF)) if isori else u32(base + imm)
                if va in targets:
                    print(f"{f.split('/')[-1]}:{i+1}  VA=0x{va:08X}  in {func} (def line {funcline})")
