import re,sys
IMG = open('/tmp/claude-1000/-home-dlynch-dev-mw-recomp-mp/393c7418-d191-49f5-8fba-7ed1a10122a5/scratchpad/mp_image.bin','rb').read()
BASE=0x82000000
pat = re.compile(rb'[\x20-\x7e]{4,}')
needles = [n.encode() for n in sys.argv[1:]]
for m in pat.finditer(IMG):
    s=m.group()
    low=s.lower()
    if any(n in low for n in needles):
        print('%08X: %s' % (BASE+m.start(), s.decode('latin1')))
