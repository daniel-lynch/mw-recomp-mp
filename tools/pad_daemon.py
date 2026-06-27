#!/usr/bin/env python3
"""Long-lived virtual X360 pad driven by a FIFO. Create it BEFORE launching the game and filter the game to
this pad's VID:PID (SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x045e/0x<product>") so that game binds ONLY
this pad as player 1. Two daemons with distinct --product values give two independently-driven instances.

Drive it by writing whitespace-separated tokens to the FIFO:
  u d l r   = dpad ; a b x y start back = buttons ; sN = sleep N seconds
  echo 'd d a' > <fifo>
"""
import argparse, os, time
from evdev import UInput, ecodes as e, AbsInfo

BTN = {"a": e.BTN_A, "b": e.BTN_B, "x": e.BTN_X, "y": e.BTN_Y, "start": e.BTN_START, "back": e.BTN_SELECT}

def make_pad(product):
    st = AbsInfo(0, -32768, 32767, 16, 128, 0); tr = AbsInfo(0, 0, 255, 0, 0, 0); h = AbsInfo(0, -1, 1, 0, 0, 0)
    caps = {e.EV_KEY: list(BTN.values()) + [e.BTN_TL, e.BTN_TR, e.BTN_THUMBL, e.BTN_THUMBR, e.BTN_MODE],
            e.EV_ABS: [(e.ABS_X, st), (e.ABS_Y, st), (e.ABS_RX, st), (e.ABS_RY, st),
                       (e.ABS_Z, tr), (e.ABS_RZ, tr), (e.ABS_HAT0X, h), (e.ABS_HAT0Y, h)]}
    return UInput(caps, name="Microsoft X-Box 360 pad", vendor=0x045e, product=product,
                  version=0x0114, bustype=e.BUS_USB)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--product", default="0x028e")
    ap.add_argument("--fifo", required=True)
    a = ap.parse_args()
    product = int(a.product, 16)
    ui = make_pad(product)
    if os.path.exists(a.fifo):
        os.remove(a.fifo)
    os.mkfifo(a.fifo)
    print(f"[pad {product:#06x}] {ui.device.path}  fifo={a.fifo}  READY", flush=True)

    def tap(c):
        ui.write(e.EV_KEY, c, 1); ui.syn(); time.sleep(0.12)
        ui.write(e.EV_KEY, c, 0); ui.syn(); time.sleep(0.35)

    def hat(ax, v):
        ui.write(e.EV_ABS, ax, v); ui.syn(); time.sleep(0.12)
        ui.write(e.EV_ABS, ax, 0); ui.syn(); time.sleep(0.35)

    while True:
        with open(a.fifo) as f:          # reopen each burst (blocks until a writer connects)
            for line in f:
                for tok in line.split():
                    if tok in ("u", "d", "l", "r"):
                        hat(e.ABS_HAT0Y if tok in ("u", "d") else e.ABS_HAT0X, -1 if tok in ("u", "l") else 1)
                    elif tok in BTN:
                        tap(BTN[tok])
                    elif tok.startswith("s") and tok[1:].isdigit():
                        time.sleep(int(tok[1:]))
                    print(f"[pad {product:#06x}] {tok}", flush=True)

if __name__ == "__main__":
    main()
