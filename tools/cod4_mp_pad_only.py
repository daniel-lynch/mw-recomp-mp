#!/usr/bin/env python3
# Creates a virtual X360 pad and drives a sequence (does NOT launch the game).
# Args: <predelay_sec> <seq...>   seq tokens: u d l r a b x y start back sN
import sys, time
from evdev import UInput, ecodes as e, AbsInfo
BTN={"a":e.BTN_A,"b":e.BTN_B,"x":e.BTN_X,"y":e.BTN_Y,"start":e.BTN_START,"back":e.BTN_SELECT}
st=AbsInfo(0,-32768,32767,16,128,0); tr=AbsInfo(0,0,255,0,0,0); h=AbsInfo(0,-1,1,0,0,0)
caps={e.EV_KEY:list(BTN.values())+[e.BTN_TL,e.BTN_TR,e.BTN_THUMBL,e.BTN_THUMBR,e.BTN_MODE],
      e.EV_ABS:[(e.ABS_X,st),(e.ABS_Y,st),(e.ABS_RX,st),(e.ABS_RY,st),(e.ABS_Z,tr),(e.ABS_RZ,tr),(e.ABS_HAT0X,h),(e.ABS_HAT0Y,h)]}
ui=UInput(caps,name="Microsoft X-Box 360 pad",vendor=0x045e,product=0x028e,version=0x0114,bustype=e.BUS_USB)
def tap(c): ui.write(e.EV_KEY,c,1);ui.syn();time.sleep(0.12);ui.write(e.EV_KEY,c,0);ui.syn();time.sleep(0.4)
def hat(ax,v): ui.write(e.EV_ABS,ax,v);ui.syn();time.sleep(0.12);ui.write(e.EV_ABS,ax,0);ui.syn();time.sleep(0.4)
pre=float(sys.argv[1]); print(f"[pad] {ui.device.path} predelay {pre}s",flush=True); time.sleep(pre)
for tok in sys.argv[2:]:
    if tok in("u","d","l","r"): hat(e.ABS_HAT0Y if tok in("u","d") else e.ABS_HAT0X, -1 if tok in("u","l") else 1)
    elif tok in BTN: tap(BTN[tok])
    elif tok.startswith("s") and tok[1:].isdigit(): time.sleep(int(tok[1:]))
    print(f"[pad] {tok}",flush=True)
time.sleep(8); ui.close(); print("[pad] done",flush=True)
