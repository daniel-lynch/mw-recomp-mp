import sys
IMG = open('/tmp/claude-1000/-home-dlynch-dev-mw-recomp-mp/393c7418-d191-49f5-8fba-7ed1a10122a5/scratchpad/mp_image.bin','rb').read()
BASE=0x82000000
def rdstr(va, maxlen=200):
    off = va - BASE
    if off<0 or off>=len(IMG): return None
    end = IMG.find(b'\x00', off)
    if end<0 or end-off>maxlen: end=off+maxlen
    return IMG[off:end]
for a in sys.argv[1:]:
    va=int(a,16)
    print('%08X: %r' % (va, rdstr(va)))
