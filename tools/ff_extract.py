#!/usr/bin/env python3
"""CoD4 (IW3) 360 fastfile -> decompressed zone extractor.

The 360 .ff files are SIGNED (IWff0100 wrapper + IWffs100 signed-fastfile magic) but the
zone payload is plain zlib starting after the ~16KB signature block. We locate the zlib
stream (0x78 ...) and inflate it. The result is the raw zone blob: a binary image of asset
structs with all menu/uiscript/dvar/string text embedded as plain ASCII, so it greps cleanly.

Usage:
  ff_extract.py <file.ff> [out.zone]     # inflate one fastfile
  ff_extract.py --all <gamedir> <outdir> # inflate every .ff in a dir
"""
import sys, os, zlib, glob


def inflate_ff(path):
    d = open(path, "rb").read()
    if d[:4] != b"IWff":
        raise ValueError(f"{path}: not an IWff fastfile (magic={d[:8]!r})")
    # find the first usable zlib stream (signed-ff signature block precedes it, ~0x400C)
    for off in range(0, len(d) - 2):
        if d[off] == 0x78 and d[off + 1] in (0x01, 0x5E, 0x9C, 0xDA):
            try:
                out = zlib.decompressobj().decompress(d[off:], 0)
                if len(out) > 100000:
                    return off, out
            except Exception:
                pass
    raise ValueError(f"{path}: no zlib zone stream found (encrypted?)")


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 1
    if a[0] == "--all":
        gamedir, outdir = a[1], a[2]
        os.makedirs(outdir, exist_ok=True)
        for ff in sorted(glob.glob(os.path.join(gamedir, "*.ff"))):
            try:
                off, out = inflate_ff(ff)
                op = os.path.join(outdir, os.path.basename(ff)[:-3] + ".zone")
                open(op, "wb").write(out)
                print(f"{os.path.basename(ff):28} zlib@{off:#08x} -> {len(out):>10} bytes  {op}")
            except Exception as e:
                print(f"{os.path.basename(ff):28} SKIP: {e}")
        return 0
    path = a[0]
    out_path = a[1] if len(a) > 1 else path[:-3] + ".zone" if path.endswith(".ff") else path + ".zone"
    off, out = inflate_ff(path)
    open(out_path, "wb").write(out)
    print(f"inflated {path} (zlib@{off:#x}) -> {out_path} ({len(out)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
