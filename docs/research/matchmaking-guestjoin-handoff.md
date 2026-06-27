# Matchmaking — GUEST-JOIN IMPLEMENTATION HANDOFF

**START HERE for the next matchmaking step: making a 2nd client actually JOIN a host's Find-Match game.**

Companion / parent doc: `matchmaking-mock360-handoff.md` (the broader mock-360 strategy + the single-box
host-spawn fix). Memory: `cod4-mp-matchmaking-broker` (condensed), `cod4-mp-connect-lever`,
`cod4-mp-mock360-direction`. Owner direction: implement Live **properly** as a reusable mock-360 layer in
`rexglue-sdk`, not game-side hacks.

Repos / build:
- Game: `/home/dlynch/dev/mw-recomp-mp` (branch `feat/matchmaking-broker`).
- SDK: `/home/dlynch/dev/rexglue-sdk` (branch `feat/matchmaking-broker`, off `testing/five-stability-fixes`).
  Build: `ninja -C out/build/linux-amd64 install:Release`. The game loads the installed
  `out/install/linux-amd64/lib/librexruntime.so` via `LD_LIBRARY_PATH`.

---

## TL;DR — where we are

Goal: two instances (a friend over VPN; two local instances for testing) both Find-Match into the **same**
game so they play together (master plan C = full Live P2P). Built this session a **matchmaking broker** in
the SDK. Result: **discovery + QoS + address/port plumbing all work and are trace-proven**, but the joiner
**loops instead of completing the join** because of a host/guest inversion + a stubbed `XGISessionJoinRemote`.

### ✅ DONE & VALIDATED (committed: SDK `3abf11a`+`e90cc2f`, game `8dab407`)
- **Discovery** — host publishes its `XSESSION_INFO` to a shared file registry; joiner's `XSessionSearch`
  (0x000B0016) + `XSessionSearchEx` (0x000B001C) return it as a real `XSESSION_SEARCHRESULT`. Trace-proven:
  the joiner's search result == the host's session (matching xnkid + port).
- **QoS** — implemented `NetDll_XNetQosLookup` (was a stub); joiner passes "Getting match quality".
- **Host port plumbing** — `XSocket::Bind` captures the real OS port (guest Xbox port is ephemeral-remapped);
  `XSessionCreate` stamps it into session XNADDR `wPortOnline`; `XNetXnAddrToInAddr` resolves → 127.0.0.1.
- **Unique per-host XNKID** (was a shared static const → joiner treated a discovered host as itself).
- **Host-file persistence fix** — a single instance creates a transient *search* session + the *host*
  session; `XSessionDelete` of the search session wiped the PID-keyed registry file. `BrokerRemoveHost`
  is now a no-op (TTL reaps).

### ❌ THE REMAINING BLOCKER — the guest-join (this handoff)
The joiner **does** call `XGISessionJoinRemote (0x000B0013)` on the host — discovery→join-intent works — but
then **loops forever** and never connects:

```
[joiner B] 0B001C SearchEx        -> finds host A (peer_hosts=1, result xnkid/port == A)
           0B0010 XSessionCreate  -> B creates its OWN host session (open_pub=17)   <-- inversion
           0B0013 JoinRemote      -> B tries to register A as a REMOTE MEMBER of B's session
           0B0011 XSessionDelete  -> tears B's session down
           0B0010 Create -> 0B001C Search -> 0B0013 JoinRemote -> 0B0011 Delete ... LOOP
```

Two root causes:
1. **Host/guest inversion.** B keeps creating its *own* `open_pub=17` host session and JoinRemote-registers
   A as a *remote member of B's* session — both instances act as host trying to absorb the other. That is
   why B only ever resolves its **own** XNADDR via `XNetXnAddrToInAddr` (confirmed in `nettrace.log`), never
   A's, so it never connects OUT to A.
2. **`XGISessionJoinRemote` (0x000B0013) is a stub** returning `X_E_SUCCESS` with no side effects — it
   registers no remote peer and sets up no host connection, so the title can never finish the join → retries.

---

## The objective for next session

Make the **joiner** (the instance that finds a host in search) actually **join the host's session as a
guest and connect OUT to the host**, instead of creating its own host session. Concretely, after a
successful search the joiner should:
- adopt the host's session identity (the discovered `XSESSION_INFO`: XNKID + host XNADDR + XNKEY), and
- connect its game netchannel to the host's real address:port (we already resolve that: 127.0.0.1 + the
  host's real OS port advertised in `wPortOnline`).

Two non-exclusive angles (try in this order):

### Angle 1 — implement `XGISessionJoinRemote` (0x000B0013) properly (SDK, xgi_app.cpp ~line 168)
Currently:
```cpp
case 0x000B0013: {  // XGISessionJoinRemote(session_ptr, user_count, xuid_array, private_slots_array)
  ... REXKRNL_DEBUG(...); return X_E_SUCCESS;   // STUB
}
```
Investigate what the title expects this to accomplish. On a real console JoinRemote registers remote XUIDs
into the *local* session object and (with XNet) makes their secure addresses reachable. Likely we need it to
establish/return the host connection context so the title proceeds to connect rather than loop. Pair this
with gdb (below) to see what the title does with the result.

### Angle 2 — stop the joiner from creating its own host session (suppress host-fallback when a join target exists)
The joiner shouldn't `XSessionCreate(open_pub=17)` a host session when search returned a host. Options:
- In the SDK, when a recent `XSessionSearch` returned ≥1 broker host for this process, make the **next**
  `XSessionCreate` behave as a *guest* (don't publish; don't present as host) — or have the title skip it.
- OR find the **game-side** host-fallback decision (a `sub_XXXX` in iw3mp) and gate it: if a joinable host
  was found, take the join branch, not the host branch. This is where gdb pays off.

The right fix probably combines both: JoinRemote wires the connection to A, and the joiner stops self-hosting.

---

## How to reproduce (reliable harness)

Pads MUST be started before the games (pad-before-launch = reliable SDL bind). `pgrep -fc pad_daemon`
self-matches — verify daemons via the `READY` line in their logs, not pgrep.

```bash
# 0) build SDK if changed
cd /home/dlynch/dev/rexglue-sdk && ninja -C out/build/linux-amd64 install:Release

# 1) clean
pkill -9 -x cod4_mp; pkill -9 -f pad_daemon.py; sleep 1
rm -f /dev/shm/xenia_memory_*; rm -rf /tmp/cod4_mp_sessions; mkdir -p /tmp/cod4_mp_sessions
rm -f /tmp/padA.fifo /tmp/padB.fifo /tmp/padA.log /tmp/padB.log

# 2) pads first (distinct products 0x028e / 0x028f), confirm READY in logs
cd /home/dlynch/dev/mw-recomp-mp
setsid python3 tools/pad_daemon.py --product 0x028e --fifo /tmp/padA.fifo >/tmp/padA.log 2>&1 </dev/null &
setsid python3 tools/pad_daemon.py --product 0x028f --fifo /tmp/padB.fifo >/tmp/padB.log 2>&1 </dev/null &
sleep 3; grep READY /tmp/padA.log /tmp/padB.log   # both must say READY

# 3) launch each game via a DEDICATED background process (NOT piped — piping kills them via the pgroup).
#    Use run_in_background or `setsid env ... &`. LD_LIBRARY_PATH must point at the installed SDK lib.
export LD_LIBRARY_PATH=/home/dlynch/dev/rexglue-sdk/out/install/linux-amd64/lib
cd out/build/linux-amd64-release
# A = HOST:
env SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x045e/0x028e" \
  COD4_LIVE=1 COD4_PLAYLIST=1 COD4_MMHOST=1 COD4_MAXCLIENTS=12 COD4_MM_SEARCH_DELAY_MS=6000 \
  COD4_MM_BROKER=1 COD4_MM_REGISTRY=/tmp/cod4_mp_sessions COD4_MM_PORT_IDX=0 \
  COD4_GSCINJECT=1 COD4_BOTSPAWN=1 COD4_BOTAI=1 COD4_BOTSETTLE=120 COD4_BOTNAMES=1 COD4_STATS=1 \
  DISPLAY=:1 ./cod4_mp --game_data_root=/home/dlynch/Games/cod4 >/tmp/cod4_A.log 2>&1 &
# B = JOINER (separate XDG_DATA_HOME so profiles don't collide; NO MMHOST):
env SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x045e/0x028f" XDG_DATA_HOME=/tmp/cod4_p2_data \
  COD4_LIVE=1 COD4_PLAYLIST=1 COD4_MM_BROKER=1 COD4_MM_REGISTRY=/tmp/cod4_mp_sessions \
  DISPLAY=:1 ./cod4_mp --game_data_root=/home/dlynch/Games/cod4 >/tmp/cod4_B.log 2>&1 &

# 4) place windows on monitor 3 (DP-2 @ x=5120), side by side:
mapfile -t W < <(DISPLAY=:1 wmctrl -l | awk '/cod4_mp/{print $1}')
DISPLAY=:1 wmctrl -ir "${W[0]}" -e 0,5120,200,1280,720   # A (host) left
DISPLAY=:1 wmctrl -ir "${W[1]}" -e 0,6400,200,1280,720   # B (joiner) right

# 5) drive (FIFO tokens: u d l r = dpad, a b x y start back = buttons, sN = sleep N).
#    IMPORTANT: get BOTH on the SAME playlist (gametype must match or the title rejects the host).
#    Deterministic: from the picker press UP x5 to land on Team Deathmatch (top), then A to select.
#    Drive A FULLY into its hosting lobby FIRST (else both publish + cross-discover + race):
printf 'a s2 a s2 u u u u u a\n' > /tmp/padA.fifo      # A: LIVE -> Find Match -> TDM -> host
sleep 18    # A should now show "Waiting for N players" lobby; /tmp/cod4_mp_sessions/<pidA>.session exists
printf 'a s2 a s2 u u u u u a\n' > /tmp/padB.fifo      # B: LIVE -> Find Match -> TDM -> (should JOIN A)
```

Screenshot a window: `DISPLAY=:1 import -window <WID> /tmp/x.png`. Driving menus blind is flaky — screenshot
between steps. The picker reopens on the profile's LAST-selected playlist (profile blob 63E83FFD), so use
explicit UP presses to normalize.

---

## Observability (use these — `netlog()`/`REXKRNL_DEBUG` do NOT reach the game stdout log)

All under `/tmp/cod4_mp_sessions/` (the broker registry dir):
- `trace.log` — broker events: `XGI msg=0x00XXXX` (session-lifecycle sequence, the key one),
  `PUBLISH`, `SEARCH: peer_hosts=N`, `SEARCH: result[i] xnkid=.. port=..`, `XSessionCreate: OWN xnkid=..`,
  `REMOVE (no-op)`. Write your own with `BrokerTrace(fmt, ...)` in `xgi_app.cpp`.
- `nettrace.log` — `XNetXnAddrToInAddr: xnkid=.. ina=.. port=.. -> 127.0.0.1` (which session the title is
  resolving — own vs host) and `BIND fd=.. requested_xbox_port=.. -> real_os_port=..`. Write with
  `NetTrace(fmt, ...)` in `xam_net.cpp`.
- `<pid>.session` — 104-byte `BrokerEntry` per published host (magic+pid+ts+60B XSESSION_INFO+slots).
- Sockets: `ss -ulnp | grep cod4_mp` (each instance binds ~3 ephemeral UDP sockets). No tcpdump perms here
  (no passwordless sudo) — use `ss` (connected-UDP peers) or `strace -p <pid> -f -e trace=connect,sendto`.

### gdb notes (for the game-side host-fallback / join decision)
- `sudo sysctl -w kernel.yama.ptrace_scope=0` is already enabled. Break on the `sub_XXXX` SYMBOL (weak),
  not a guest VA. `handle SIGSEGV nostop noprint pass` (runtime soft-MMU). PPCContext arg at `$rdi`,
  membase `$rsi=0x100000000`; guest regs r3@+0, r4@+0x20, r5@+0x28. Watchpoints can use the host data addr
  `0x100000000 + guestVA`. Heavy but works.
- Target: find the iw3mp function that decides host-vs-join after `XSessionSearch` returns. Start by
  watching what runs between the `0B0013` (JoinRemote) and `0B0011` (Delete) in the loop — that's where the
  title gives up on the join and tears down. The JoinRemote handler's guest caller (LR in the PPCContext
  when the SDK 0x000B0013 case runs) is the entry point into that game logic.

---

## SDK code map (rexglue-sdk, branch `feat/matchmaking-broker`)

`src/kernel/xam/apps/xgi_app.cpp`:
- `[COD4MP-MMBROKER]` block (top of anon namespace): `BrokerEntry`, `broker_on()`, `broker_dir()`,
  `BrokerPublishHost()`, `BrokerRemoveHost()` (no-op), `BrokerReadHosts()`, `BrokerWriteSearchResults()`,
  `BrokerTrace()`. Constants: `kSearchResultSize=0x5C`, `kSessionInfoSize=60`, `kBrokerTtlSec=900`.
- Dispatch entry: `if (broker_on() && msg in 0x10..0x20) BrokerTrace("XGI msg=...")` — the sequence trace.
- `case 0x000B0010` XSessionCreate: writes XSESSION_INFO (unique XNKID from pid+time; ina/inaOnline
  127.0.0.1; **wPortOnline = host real port** via `rex::system::g_host_real_ports[COD4_MM_PORT_IDX]`),
  then `BrokerPublishHost`.
- `case 0x000B0011` XSessionDelete: `BrokerRemoveHost()` (no-op).
- `case 0x000B0013` XGISessionJoinRemote: **STUB — implement this.**
- `case 0x000B0016 / 0x000B001C` Search/SearchEx: `BrokerWriteSearchResults()` else `WriteEmptySearchResults`.

`src/kernel/xam/xam_net.cpp`:
- `NetDll_XNetXnAddrToInAddr_entry` (~541): writes 127.0.0.1, traces xnkid/ina/port. `NetTrace()` helper.
- `NetDll_XNetQosLookup_entry` (~636): implemented (alloc XNQOS, all targets contacted, signal event).
- `NetDll_XNetQosListen_entry` (~660): returns SUCCESS, **stores no data** (candidate for the QoS-data
  exchange idea, a secondary lead — host registers connection data here, joiner reads via QosLookup).
- Still-stub addressing fns that may matter for the connect: `XNetServerToInAddr`, `XNetRegisterKey`,
  `XNetUnregisterInAddr`, `XNetInAddrToServer` (~1157-1166).

`src/system/xsocket.cpp` / `include/rex/system/xsocket.h`:
- `XSocket::Bind` (~146): getsockname → `real_bound_port_`; records `rex::system::g_host_real_ports[]`
  (extern'd into xgi_app.cpp). `g_host_real_port_count`. Traces BIND to nettrace.log.

Env vars: `COD4_MM_BROKER=1` (enable), `COD4_MM_REGISTRY=<dir>`, `COD4_MM_PORT_IDX=0|1|2` (which of the
host's 3 sockets to advertise — idx0 = guest Xbox port 59395; unverified which is the match/VDP socket
because the connect never landed). Host needs `COD4_MMHOST=1 COD4_MM_SEARCH_DELAY_MS=6000` to host-fallback.

---

## Known facts / gotchas
- Each instance binds ~3 ephemeral UDP sockets; guest Xbox ports are fixed (59395/62723/59651) but
  unbindable → OS picks ephemeral. `bound_port_` stores the guest port; `real_bound_port_` the OS port.
- Search result layout (0x5C): XSESSION_INFO@0 (60), openPub@0x3C, openPriv@0x40, filledPub@0x44,
  filledPriv@0x48, cProps@0x4C, cContexts@0x50, pProps@0x54, pContexts@0x58. We return cProps/cCtx = 0
  (a secondary suspect — the title may want matching contexts/properties; lower priority than the inversion).
- XSESSION_INFO (60): XNKID@0 (8), XNADDR@8 (ina@8, inaOnline@12, wPortOnline@16, abEnet@18, abOnline@24),
  XNKEY@44 (16).
- Ruled out as the blocker: ordering (A hosting first) and gametype mismatch (both on TDM) — neither fixed it.
- Single-box host-spawn ("Server is full") is already solved — see parent handoff (`COD4_MMHOST` clears
  `*(0x84C209D0)+0xc` during the connect window). Use `COD4_MAXCLIENTS<=8` if running bots (9+ bots =
  GSC var-limit crash).
