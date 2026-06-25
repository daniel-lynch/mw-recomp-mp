#!/usr/bin/env python3
# Two virtual X360 pads: join 2 splitscreen players, ready up, Start Match. ONE process.
import os,sys,time,subprocess
from evdev import UInput, ecodes as e, AbsInfo
BUILD="/home/dlynch/dev/mw-recomp-mp/out/build/linux-amd64-release"
GAME="/home/dlynch/Games/cod4"; OUT="/tmp/cod4mp_drive2"; os.makedirs(OUT,exist_ok=True)
os.environ.setdefault("DISPLAY",":1")
SDK="/home/dlynch/dev/rexglue-sdk/out/install/linux-amd64/lib"
BTN={"a":e.BTN_A,"b":e.BTN_B,"x":e.BTN_X,"y":e.BTN_Y,"start":e.BTN_START,"back":e.BTN_SELECT}
def mkpad(name):
    st=AbsInfo(0,-32768,32767,16,128,0);tr=AbsInfo(0,0,255,0,0,0);h=AbsInfo(0,-1,1,0,0,0)
    caps={e.EV_KEY:list(BTN.values())+[e.BTN_TL,e.BTN_TR,e.BTN_THUMBL,e.BTN_THUMBR,e.BTN_MODE],
          e.EV_ABS:[(e.ABS_X,st),(e.ABS_Y,st),(e.ABS_RX,st),(e.ABS_RY,st),(e.ABS_Z,tr),(e.ABS_RZ,tr),(e.ABS_HAT0X,h),(e.ABS_HAT0Y,h)]}
    return UInput(caps,name=name,vendor=0x045e,product=0x028e,version=0x0114,bustype=e.BUS_USB)
def tap(ui,c): ui.write(e.EV_KEY,c,1);ui.syn();time.sleep(0.12);ui.write(e.EV_KEY,c,0);ui.syn();time.sleep(0.5)
def hat(ui,ax,v): ui.write(e.EV_ABS,ax,v);ui.syn();time.sleep(0.12);ui.write(e.EV_ABS,ax,0);ui.syn();time.sleep(0.5)
N=[0]
def shot(t): N[0]+=1; subprocess.run(["bash","-c",f"import -window root -crop 1280x720+64+136 {OUT}/{N[0]:02d}_{t}.png 2>/dev/null"],timeout=15); print(f"shot {t}",flush=True)
subprocess.run(["pkill","-9","-x","cod4_mp"]); subprocess.run(["bash","-c","rm -f /dev/shm/xenia_memory_*"]); time.sleep(1)
open(os.path.join(BUILD,"cod4_mp.toml"),"w").write('log_verbose=true\nasync_shader_compilation=false\nvulkan_readback_resolve=true\ninput_backend="sdl"\nmnk_mode=false\n')
A=mkpad("Microsoft X-Box 360 pad"); B=mkpad("Microsoft X-Box 360 pad 2")
print(f"padA={A.device.path} padB={B.device.path}",flush=True)
env=dict(os.environ); env["LD_LIBRARY_PATH"]=SDK+os.pathsep+env.get("LD_LIBRARY_PATH","")
log=open(f"{OUT}/cod4mp.log","w")
p=subprocess.Popen(["./cod4_mp",f"--game_data_root={GAME}"],cwd=BUILD,stdout=log,stderr=subprocess.STDOUT,env=env)
time.sleep(16)
hat(A,e.ABS_HAT0Y,1)          # down to Split Screen
tap(A,BTN["a"]); time.sleep(2) # enter -> SIGNIN
shot("signin")
tap(A,BTN["a"]); time.sleep(1) # padA joins
tap(B,BTN["a"]); time.sleep(1) # padB joins (2nd player)
shot("two_joined")
tap(A,BTN["a"]); time.sleep(1) # padA continue
tap(B,BTN["a"]); time.sleep(1) # padB continue
shot("after_continue")
tap(A,BTN["a"]); time.sleep(2) # padA start match (host)
shot("after_start")
time.sleep(25); shot("ingame")
print("done",flush=True)
A.close(); B.close()
