import sys, re, glob, subprocess
GEN="/home/dlynch/dev/mw-recomp-mp/generated"
T=int(sys.argv[1],16)
init=open(f"{GEN}/cod4_mp_init.cpp").read()
ents=sorted(int(x,16) for x in re.findall(r'\{ (0x[0-9A-Fa-f]+),', init))
exact = T in ents
below=max([e for e in ents if e<=T], default=0)
above=min([e for e in ents if e>T], default=0)
print(f"target=0x{T:X} exact_entry={exact}")
print(f"container_entry=0x{below:X} next_entry=0x{above:X} span={above-below}bytes target_off={T-below}bytes")
# scan container body for a return before target's offset, and whether target is a branch label
fn=f"sub_{below:X}".lower(); fn=f"sub_{below:X}"
for f in glob.glob(f"{GEN}/cod4_mp_recomp.*.cpp"):
    txt=open(f).read()
    m=re.search(rf'DEFINE_REX_FUNC\(sub_{below:X}\)\s*\{{(.*?)\n\}}', txt, re.S)
    if m:
        body=m.group(1)
        has_label = f"loc_{T:X}" in body
        # count returns/blr before we'd reach target — rough: any 'return;' before a label at/after target
        rets=body.count("return;"); blrs=body.count("// blr")
        print(f"body: returns={rets} blr_comments={blrs} has_loc_{T:X}={has_label}")
        print("HINT:", "standalone (separate merged fn — multiple returns, no label at target)" if (not has_label and rets>1) else "chunk of parent (shares body/loop)")
        break
else:
    print("container body not found")
