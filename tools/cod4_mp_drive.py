#!/usr/bin/env python3
r"""Self-contained SDL-pad menu driver for cod4_mp (ONE process: pad stays alive).
Creates a virtual X360 pad, launches cod4_mp (SDL backend), waits for the menu, then
runs an action sequence (argv after --seq), screenshotting each step. Leaves the game
running at the end. Mirrors mw-recomp/tools/cod4_drive_sdl.py.

Actions: u d l r = dpad tap; a b x y start back = buttons; sN = sleep N sec; shot = screenshot.
Usage: cod4_mp_drive.py --boot 16 --seq "shot d shot a s3 shot"
"""
import os, sys, time, subprocess, argparse
from evdev import UInput, ecodes as e, AbsInfo
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(REPO, "out/build/linux-amd64-release")
GAMEDIR = os.environ.get("COD4_GAMEDIR", "/home/dlynch/Games/cod4")
SDKLIB = "/home/dlynch/dev/rexglue-sdk/out/install/linux-amd64/lib"
OUT = "/tmp/cod4mp_drive"; os.makedirs(OUT, exist_ok=True)
os.environ.setdefault("DISPLAY", ":1")
BTN = {"a":e.BTN_A,"b":e.BTN_B,"x":e.BTN_X,"y":e.BTN_Y,"start":e.BTN_START,"back":e.BTN_SELECT}
def make_pad():
    st=AbsInfo(0,-32768,32767,16,128,0); tr=AbsInfo(0,0,255,0,0,0); hat=AbsInfo(0,-1,1,0,0,0)
    caps={e.EV_KEY:list(BTN.values())+[e.BTN_TL,e.BTN_TR,e.BTN_THUMBL,e.BTN_THUMBR,e.BTN_MODE],
          e.EV_ABS:[(e.ABS_X,st),(e.ABS_Y,st),(e.ABS_RX,st),(e.ABS_RY,st),(e.ABS_Z,tr),
                    (e.ABS_RZ,tr),(e.ABS_HAT0X,hat),(e.ABS_HAT0Y,hat)]}
    return UInput(caps,name="Microsoft X-Box 360 pad",vendor=0x045e,product=0x028e,version=0x0114,bustype=e.BUS_USB)
def tap(ui,code,hold=0.12):
    ui.write(e.EV_KEY,code,1); ui.syn(); time.sleep(hold); ui.write(e.EV_KEY,code,0); ui.syn(); time.sleep(0.4)
def hat(ui,axis,val):
    ui.write(e.EV_ABS,axis,val); ui.syn(); time.sleep(0.12); ui.write(e.EV_ABS,axis,0); ui.syn(); time.sleep(0.4)
N=[0]
def shot(tag=""):
    N[0]+=1; fn=f"{OUT}/{N[0]:02d}_{tag}.png"
    subprocess.run(["bash","-c",f"import -window root -crop 1280x720+64+136 {fn} 2>/dev/null"],timeout=15)
    print(f"[drive] shot -> {fn}",flush=True)
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--boot",type=int,default=16); ap.add_argument("--seq",default="shot")
    a=ap.parse_args()
    subprocess.run(["pkill","-9","-x","cod4_mp"]); subprocess.run(["bash","-c","rm -f /dev/shm/xenia_memory_*"]); time.sleep(1)
    with open(os.path.join(BUILD,"cod4_mp.toml"),"w") as f:
        f.write('log_verbose = true\nasync_shader_compilation = false\nvulkan_readback_resolve = true\n'
                'vsync = false\ninput_backend = "sdl"\nmnk_mode = false\n')
    ui=make_pad(); print(f"[drive] pad {ui.device.path}; launching (boot {a.boot}s)",flush=True)
    env=dict(os.environ); env["LD_LIBRARY_PATH"]=SDKLIB+os.pathsep+env.get("LD_LIBRARY_PATH","")
    log=open(f"{OUT}/cod4mp.log","w")
    proc=subprocess.Popen(["./cod4_mp",f"--game_data_root={GAMEDIR}"],cwd=BUILD,stdout=log,stderr=subprocess.STDOUT,env=env)
    time.sleep(a.boot)
    for tok in a.seq.split():
        if proc.poll() is not None: print(f"[drive] !! cod4_mp EXITED (code {proc.returncode}) before '{tok}'",flush=True); break
        if tok=="shot": shot("step")
        elif tok in ("u","d","l","r"):
            ax=e.ABS_HAT0Y if tok in("u","d") else e.ABS_HAT0X; v=-1 if tok in("u","l") else 1; hat(ui,ax,v); print(f"[drive] dpad {tok}",flush=True)
        elif tok in BTN: tap(ui,BTN[tok]); print(f"[drive] btn {tok}",flush=True)
        elif tok.startswith("s") and tok[1:].isdigit(): time.sleep(int(tok[1:]))
        else: print(f"[drive] ?? {tok}",flush=True)
    alive = proc.poll() is None
    print(f"[drive] done. alive={alive} shots in {OUT}",flush=True)
    ui.close()
main()
