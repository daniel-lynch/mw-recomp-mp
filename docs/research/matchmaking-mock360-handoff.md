# Matchmaking / "Mock 360" Live — IMPLEMENTATION HANDOFF

**START HERE for the matchmaking work.** Strategic direction (per owner): implement Xbox Live **properly**
as a reusable **mock-360 session/matchmaking layer in the SDK** (`rexglue-sdk`), even if slower — this code
is generic Xbox API (XGI/XSession, XLiveBase, XAM), not CoD4-specific, so it benefits **every** recomp and
is the foundation of the "grand master plan of mock 360." Prefer real (if minimal) implementations in the
SDK over game-side `[COD4MP-*]` hacks for this layer.

Project: `/home/dlynch/dev/mw-recomp-mp` (CoD4 MP / iw3mp recomp). SDK: `/home/dlynch/dev/rexglue-sdk`
(branch `testing/five-stability-fixes`; build `ninja -C out/build/linux-amd64 install:Release`, the game
loads the installed `librexruntime.so`). Companion: `phase3-ranking-handoff.md` (profile/rank/playlist),
memory `cod4-mp-playlist-matchmaking`, `cod4-mp-profile-system`.

## 🎉🎉 2026-06-26 — FIND MATCH NOW HOSTS (reaches the pre-match host lobby!)
`COD4_LIVE=1 COD4_PLAYLIST=1 COD4_MMHOST=1 COD4_MM_SEARCH_DELAY_MS=6000` → Find Match → select
"Shipment 24/7" → the lobby **transitions from "Searching for available games" to "Waiting for 1 more
player… 1/18 PLAYERS"** (host lobby; "Vote to Skip" appears). `party_host`/`party_iamhost` value byte = 1
(WE ARE HOST). The eternal-search wall is fully cleared. The fixes that got here (this session):
1. **Game `[COD4MP-MMHOST]`** (`src/cod4mp_patches.cpp`): `REX_FUNC(sub_821AC9A0)`→1 under env
   `COD4_MMHOST` → party promotes to host (see the gdb RE below).
2. **SDK `xgi_app.cpp` `XSessionCreate` (0x000B0010)**: fills a valid XSESSION_INFO (nonzero XNKID,
   loopback XNADDR, nonzero XNKEY) + nonzero nonce (was SUCCESS-but-empty → session looked invalid).
3. **SDK `xgi_app.cpp` `XSessionJoinRemote` (0x000B0013)**: was unimplemented → FAIL(0x80004005); now
   SUCCESS (title called it right after create; failure → abort+delete).
4. **SDK `xam_net.cpp` `NetDll_XNetQosListen`**: returned `X_ERROR_FUNCTION_FAILED` → "Microsoft error
   N when trying to listen for QoS queries for gameSession" → host aborts + deletes session. Now returns
   `X_ERROR_SUCCESS` (no real QoS traffic on a single box). **This was the one that made the host stick.**

**PROGRESS (countdown → match-start fires):** poking **`party_minplayers`=1** (dvar* global **0x8244B270**,
value int @dvar+0xC big-endian) satisfies "Waiting for players" → the 10s countdown runs → **`XSessionStart`
(`0x000B0014`) FIRES** (never did before). Verified live (poker `scratchpad/poke_minplayers.py`).

**NEW WALL — "Server is full" (match starts then immediately aborts):** right after `XSessionStart` the
screen shows a **"Notice: Server is full."** (loc key `EXE_SERVERISFULL` @`0x8205C559`) and the msg sequence
is `000B0014`(start) → **`000B0015`(XSessionEnd)** → `000B0011`(Delete) → back to menu. The host's
reservation into its own session/server is rejected as full, so the match aborts (NO map load / SpawnServer
happens). NOT the game-server slot check: forcing `sv_maxclients=12` via `[COD4MP-MAXCLIENTS]`
(`COD4_MAXCLIENTS=12`, hook confirmed firing) did NOT help, and no `XSessionGetDetails (000B001D)` is queried.
So it's the **party→game-session migration** layer. **RULED OUT so far:** (a) Live session slots — the host
`XSessionCreate` requests **17 public + 1 private = 18** slots (create buf+8=0x11, buf+12=1; flags=0x3F), not
0; (b) game-server `sv_maxclients` — forcing =12 (`COD4_MAXCLIENTS`, hook confirmed) didn't help; (c) **a
session-member-table hypothesis @`0x84C25660` (full-flag@+0/array@+8/max@+0x10/cur@+0x14, the cur≥max check
in fns `0x821AADE0`/`0x821AAED8`) — DISPROVEN: runtime `max@0x84C25670`=1264 and `cur` fluctuates (225/207/
280…), i.e. that region is NOT the member count, and force-poking max=18 changed nothing.** Flow facts:
`XSessionStart` (game fn `0x821A7088`→wrapper `0x8210A250`) SUCCEEDS and reaches its success continuation
`0x821AAED8`; "full" is further downstream. `EXE_SERVERISFULL` (`0x8205C559`) has NO direct code ref and NO
data-pointer ref (referenced by name/hash via a localize lookup).

**STRING-REF FINDING (2026-06-26):** the localized key is **`@EXE_SERVERISFULL` @`0x8205C558`** (the `@`
localize-prefix byte; that's why searching `0x8205C559` found nothing). It has **exactly one code ref:
`0x822BB510`**, inside fn **`0x822BB4B8`** (called from `0x822C7890`) — but that's a **party slot-LIST
display** routine (loops `party_maxplayers` slots, `Com_Printf` ch16 + per-slot UI-add via `0x822B7DD8`,
gated on party fields `[0x8246B280 + 0x2F00/0x2EF4/0x2EFC]`). So `@EXE_SERVERISFULL` there is a per-slot
LABEL, **not** the modal. The bare key **`EXE_SERVERISFULL` @`0x8205C559`** has NO code/data ref → it's
emitted as a **network rejection reason** (built into a format string) by the listen server's connect
handler (IW3 `SV_DirectConnect`: `numClients >= sv_maxclients` → reject "EXE_SERVERISFULL"), and the
client shows it as the modal "Notice". **So the modal = the host's own loopback connect to its freshly
started listen server being rejected as full.** Why `COD4_MAXCLIENTS=12` (forces `sv_maxclients` at dvar
registration) didn't help: the matchmaking server-start almost certainly RE-sets `sv_maxclients` (from the
session/playlist) AFTER our hook, OR the reject is a different count (e.g. reserved/private slots) that also
reports "full".

**NEXT (focused):** find `SV_DirectConnect` (server-side connect handler; xref `sv_maxclients` @`0x82055D5C`
or the `svs.clients` loop) and its "full" branch — see the exact count vs limit it compares at connect time,
and what sets `sv_maxclients` on the Find-Match server-start (vs our COD4_MAXCLIENTS hook). Likely fix:
ensure `sv_maxclients` (and/or the session's open/private-slot count) is large enough at the moment the host
connects — either re-apply the maxclients force on the start path, or fix the slot count the start derives.
Then host connects → map loads (mp_shipment) → wire System-Link bots (GSCINJECT/BOTSPAWN/BOTAI).
NOTE: avoid the gdb hardware-read-watchpoint route — `rwatch` fell back to software single-stepping and hung
the game; use plain breakpoints on `SV_DirectConnect`/`sub_*` instead.

**Two cleanups still owed:** (1) replace the `party_minplayers` poke with a shipped lever (game-side
`[COD4MP-*]` or set it in the party config); (2) make host-fallback fire WITHOUT `COD4_MM_SEARCH_DELAY_MS`.
Party dvar VALUE is at dvar+0xC (BIG-ENDIAN; bool value = top byte). Party dvar globals span 0x8243BDxx
(party_host @0x8243BDC0) AND 0x8244Bxxx (party_minplayers @0x8244B270 → dvar* 0x84B38180 at runtime).

## WHERE WE ARE (works today, all env-gated, default build unchanged)
`COD4_LIVE=1 COD4_PLAYLIST=1` →
1. Main menu → **Xbox LIVE → Find Match → lists "Shipment 24/7"** (our injected playlist; entry format is
   COMMA-separated `map,gametype,weight`, parser `sub_821E6F90`, buffer `0x84C495F8`; see ranking handoff).
2. Select it → **enters the LOBBY** ("Shipment 24/7", Create a Class / Barracks / Invite Friends, party panel).
3. Lobby shows **"Searching for available games" and HANGS** ← the wall this handoff cracks.

Live state already faked (in SDK): signin=2 (`COD4_LIVE`), profile read/write + persistence
(`XamUserReadProfileSettings`, `xam_user.cpp` / `user_profile.cpp`), XStorage download/build-path + LSP
enumerate (`xlivebase_app.cpp`). Probe party state: `party_host=0` (we're a client searching),
`party_minplayers=2`, `party_maxplayers=18`, `party_gameStartTimerLength=10` (10s start timer),
`party_timer=0`.

## ⚠️ 2026-06-26 UPDATE — empty-results + 0005800E DONE, but they're NOT the gate (root cause was incomplete)

Implemented both SDK fixes (`rexglue-sdk` branch `testing/five-stability-fixes`, rebuilt+installed):
1. **`XSessionSearchEx 0x000B001C` (+ `0x000B0016` + weighted `0x000B0065`) now write a well-formed
   EMPTY result header** — new `WriteEmptySearchResults()` helper in `xgi_app.cpp` writes
   `XSESSION_SEARCHRESULT_HEADER{dwSearchResults=0, pResults=0}` at `search_results_ptr` (guarded on
   `results_buffer_size>=8`). Confirmed at runtime: the live search buffer is valid
   (`search_results_ptr=0x84C2C920`, `results_buffer_size=0x186A0`), the write lands, dispatch returns
   SUCCESS (trace `async app=000000FB msg=000B001C ... -> 00000000`). Async **does** reach our handler:
   `AppManager::DispatchMessageAsync` (app_manager.cpp:83) calls `app->DispatchMessageSync`.
2. **`XLiveBase 0x0005800E` now returns SUCCESS** (was falling through to the generic FAIL = `0x80004005`;
   polled ~3400×/run). `xlivebase_app.cpp`.

**RESULT: NO behavior change. The lobby still hangs on "Searching for available games."** The msg histogram
is unchanged (`000B0006`:`000B0007`:`000B001C` ≈ **4:2:1**, ~66 search iters/sec) and **no new
`XSessionCreate 0x000B0010` ever fires during the search** (only the 2 startup ones). So:

**THE REAL GATE IS GAME-SIDE, NOT THE SEARCH OUTPUT.** The game re-issues a fresh
SetContext×4 → SetProperty×2 → SearchEx loop every frame and never decides to host, even though each
search now completes instantly with a clean 0-result header. "Search never completes" was the WRONG root
cause — completion isn't what's missing. The host-vs-search decision lives in the IW3 **party/matchmaking
state machine** (file 18) and is gated on something else.

**Leading hypotheses (untested) for the host trigger:**
- **Search-duration timer never accumulates:** instant completion → the game restarts the search every
  frame → a "searched ≥ T seconds with 0 results → host" timer keeps resetting. Experiment: hold the
  search overlapped `IO_PENDING` for ~5–8 s, then complete it empty (needs delayed completion for just
  `000B001C` in `xeXMsgStartIORequestEx`/a timer thread, not the blanket `CompleteOverlappedImmediate`).
- **`party_host` promotion is a separate path:** we're a CLIENT in our own party (`party_host=0`); Find
  Match may require the party to self-promote to host before it'll `XSessionCreate`. Probe whether
  `party_host` ever flips, and find the promotion trigger in file-18 party fns (`sub_822B9FB0` dvars,
  frame `sub_822B8238`, uiscripts `sub_822C8108`).
- **Join-not-host:** return 1 fake result so the game JOINs a faked local session instead (less desirable
  than hosting for single-box+bots, but may be the only path the SM actually takes).

**2026-06-26 (cont.) — TIMING HYPOTHESIS DISPROVEN too.** Added an env-gated delayed search
completion: `COD4_MM_SEARCH_DELAY_MS` (default 0 = original instant) holds `XSessionSearchEx`
(`0x000B001C`) `IO_PENDING` for N ms before completing empty (detached thread, single-in-flight guard,
in `xeXMsgStartIORequestEx`, `xam_msg.cpp`). With `=6000`: the search rate **collapsed 2672 → 10**
(SetContext 10714 → 66), proving **the game serializes on the overlapped — it waits for each search to
complete before re-firing**, so we fully control the search lifecycle. BUT it **still never hosts**
(only the 2 startup `XSessionCreate`s) after ~10 unhurried empty searches over ~60s. So host-fallback
is **NOT** gated on search-result-count NOR search-duration. It's a party/online **STATE flag**.
(`0005800E` rose to ~4180× — it's a free-running background notify poll, unrelated to the search gate.)

**NEXT:** stop guessing at the SDK boundary — RE the game-side decision. Find the fn that reads the search
result COUNT and branches (search-again vs host), and/or where `party_host` is set. Static img
(`tools/img.bin`, base 0x82000000, capstone PPC32 BE) + the gdb harness (`tools/cod4_mp_disasm_spin.sh`
pattern) to break in the search loop and walk the caller. The two SDK fixes above are correct and worth
keeping regardless (proper API contract), but they are NOT sufficient.

### 2026-06-26 — GAME-SIDE RE PROGRESS (host-promotion located; decision gate still open)
Static RE of `tools/img.bin` (helper `scratchpad/ppc.py`: VA ref/use finder, func-start, caller finder).
Confirmed addresses:
- **`party_host` dvar string** @ `0x8205DD80`; registered once @ `0x822BA6CC` (find-or-register
  `0x821D32A0`); the dvar* is cached to **global `0x8243BDC0`** (`stw` @ `0x822BA740`). The dvar's bool
  VALUE byte is at **dvar+0xA** (readers do `lbz r,0xA(dvar)`).
- **`Dvar_SetBool(dvar*, bool)` = `0x821D2B90`** (verified: reads dvar+0xA, writes value as "1"/"0").
- **`party_host` is WRITTEN in exactly one place: `Dvar_SetBool(party_host,1)` @ `0x822B94B4`**, inside a
  **host-promotion routine starting @ `0x822B90E8`**. That routine sets a CLUSTER of party dvars at once
  (party_host→1 plus globals `0x82430000-0x4218`, `-0x41D4→0`, `0x8246EC80→0`, `0x82435750→1`, …) and
  takes args (r5≈0x2F/0x30 reason code, r6, r7). It is the "become the party host" path.
- **6 callers of `0x822B90E8`:** `0x821A6254`, `0x821AD844`, **`0x822B9604`**, **`0x822B9828`** (both
  party-internal, tail-calls gated by upstream branches reading session state @ `+0x9D0`/`lbz +0xC` and a
  table @ `0x82440000-0x1D74` `+0xC`), `0x822C6FB0`, `0x822C7230` (UI-script area, likely
  xpartygo/host-private handlers).

### 2026-06-26 (cont.) — FULL DECISION CHAIN MAPPED + live-probed (host predicate located; NOT yet cracked)
Traced the whole search→host decision (helpers in `scratchpad/ppc.py`; live reads via `scratchpad/gate_probe.py`,
shm guest-VA == file offset):
- **Matchmaking SM dispatcher = `0x821A6028`** (arg r3 = ctx index; state records array @ **`0x82448CD0`**,
  stride **0x28**). Per tick: if `[rec+0]==0x3E5`(IO_PENDING) return; reads overlapped result via
  `0x82102C88`/**`0x821032D0`**; result `0x3E4/0x3E5`(incomplete/pending) → **search branch `0x821A62A8`**
  (`bl 0x821A4FE0` = start-search, which calls XSessionSearchEx wrapper **`0x8210A398`** → XGI `0x000B001C`);
  result `0`(success) → **process branch `0x821A6190`** → the host-vs-research decision @ `0x821A61C0`.
- **Host decision @ `0x821A61F8`:** `bl 0x822B3658` (host predicate). TRUE → `bl 0x822B90E8`
  (host-promotion, sets party_host=1); FALSE → `0x821A5380` (keep searching). **This is the loop.**
- **Host predicate `0x822B3658`** returns 1 (host) iff: (a) `0x821AC458()==0` — scans session-slot array
  @`0x84C20AE0`(+0x1C, stride 0x28) for word[+0]==4 & byte[+4]!=0; **live: returns 0 (allows host)** ✓;
  AND (b) a ctx struct-chain (`[ctx+0x2F54]`→`[+0x2EFC]`/`[+0x2EF4]`) doesn't veto; AND (c) **`0x821AC9A0()!=0`**.
- **Final gate `0x821AC9A0`** returns 0 (no host) if `[0x8239D098]==3` OR `byte[0x84C209CB]==0`, else
  `([0x84C20FF4] >= jumptbl0x82233008(party_maxplayers))`. **LIVE PROBE in the stuck lobby: `[0x8239D098]==3`**
  → early-exit → predicate false. (`byte[0x84C209CB]=1`, `[0x84C20FF4]=0`, party_host=0, slot-scan allows.)
- **`[0x8239D098]` source:** written @`0x821B01E8` = return of **`0x8210A618`**, which calls
  **XLiveBase msg `0x00058006`** (via `XMsgInProcessCall 0x82365AF4`, app 0xFC) and returns the buffer value.
  Our SDK `xlivebase_app.cpp` 0x58006 handler writes **1** (XONLINE_NAT_OPEN) — yet the global reads **3**.
  ⚠️ UNRESOLVED DISCREPANCY: either 0x58006 isn't reaching our handler, or another writer sets it. (`0x82365AF4`
  did not disassemble cleanly — verify it's really the XMsg import thunk.)

**EXPERIMENTS (both negative):** (1) live-poked `[0x8239D098]=0` alone (no delay) → held (writer idle when
search isn't completing) but no host — because without the delay the SM rarely reaches the process branch.
(2) `COD4_MM_SEARCH_DELAY_MS=6000` + background poker forcing `[0x8239D098]=0` AND `[0x84C20FF4]=0xFFFF`
for 90s → pokes held (verified post-run) but **STILL no host**. So `0x821AC9A0` is not the sole gate:
either the predicate `0x822B3658` isn't being evaluated (SM stays in search-issue, never process), or its
ctx struct-chain gate (b) vetoes. **NEXT (gdb, decisive):** the game is recompiled to native `sub_XXXX`
symbols — breakpoint `sub_822B3658` (host predicate) and `sub_822B90E8` (host-promotion) in the stuck
lobby: confirm whether they're hit at all; if `sub_822B3658` IS hit, single-step its gates (a)/(b)/(c)
with real ctx to see which returns "no host"; if it's NOT hit, the SM never reaches the process branch —
investigate the dispatcher `0x821A6028` result read (`0x821032D0`) / state record `[ctx+0]` to see why
success isn't processed (candidate: the search overlapped is re-issued before the dispatcher reads result 0).
Also resolve the 0x58006→`[0x8239D098]`=3 discrepancy (instrument the SDK 0x58006 handler / dump `0x82365AF4`).

### 2026-06-26 — ✅ HOST GATE CRACKED (gdb-confirmed lever)
Recompiled game exposes guest fns as native `sub_XXXX` symbols → gdb breakpoints work. Convention at a
recompiled fn entry: **ctx=`$rdi`, base=`$rsi` (=0x100000000)**, guest `rN` = `*(uint*)(ctx+N*8)`, guest
mem = `base+guestVA` (values are BIG-ENDIAN). Harnesses: `scratchpad/bp_count.sh` (ignore-count),
`bp_inspect.sh` (read gate inputs), `bp_force.sh` (force globals + watch host-promotion).

**Counts (lobby, COD4_MM_SEARCH_DELAY_MS=6000, ~14s):** dispatcher `sub_821A6028` 841×, result reads
`sub_821032D0`/`sub_82102C88` 844×, start-search `sub_821A4FE0` 3×, **host predicate `sub_822B3658` 3×**
(IS evaluated, once per completed search), **host-promotion `sub_822B90E8` 0×**. So the predicate runs and
returns "no host" every time.

**Inspected `sub_822B3658` at the breakpoint:** arg `r3`(=ctx P) = **0** → gate (b) `[P+0x2F54]`=0 → NO veto;
gate (a) `sub_821AC458` (session-slot scan @`0x84C20AE0`) allows; **gate (c) `sub_821AC9A0` reads
`[0x8239D098]` = 3 (BE) → early-exit → predicate returns 0.** Confirmed `predicate ret r3 = 0`.

**FORCE TEST (decisive):** at each predicate hit, set `[0x8239D098]=0`. Alone → still NO host-promotion
(because gate (c) ALSO needs `[0x84C20FF4] >= jumptbl0x82233008(maxplayers=18)`, and `[0x84C20FF4]=0`).
Set **BOTH** `[0x8239D098]=0` AND `[0x84C20FF4]=0x40000000`(BE; gdb `set *(uint*)=0x40`) → **`>>>
sub_822B90E8 host-promotion REACHED <<<`**. ✅ The host gate is exactly these two conditions.

**THE TWO LEVERS:**
- **`[0x8239D098]`** (must != 3): = return of `0x8210A618` = XLiveBase msg **`0x00058006`** value (dispatch
  `0x82365AF4` = confirmed XMsgInProcessCall import thunk). ⚠️ Our SDK 0x58006 handler writes **1** but the
  global reads **3** — UNRESOLVED (our scanner found only writer `0x821B01E8` which stores `0x8210A618()`;
  either another writer sets it at signin, or 0x58006 path differs). Static value 3 likely = a Live
  connection-state enum.
- **`[0x84C20FF4]`** (must >= jumptbl(maxplayers)): a FIELD in the big session-manager struct @`0x84C20xxx`
  (same struct as gate-a slots `0x84C20AE0` + byte `0x84C209CB`); no `stw` writer (struct-filled). =0 because
  no real matchmaking population. Likely "available/QoS player count".

**CLEANEST FIX POINT (gdb-validated blast radius):** `sub_821AC9A0` (the failing gate) has **ONLY ONE
caller** — the host predicate `sub_822B3658` itself. So a game-side `[COD4MP-*]` hook making `sub_821AC9A0`
return **1** under `COD4_LIVE` (or a new `COD4_MMHOST`) bypasses both fragile globals and makes the host
predicate fire — precisely, with no collateral. (`sub_822B3658` itself has 3 callers, so hook the gate, not
the predicate.) NEXT: implement that hook, then validate END-TO-END (host-promotion → XSessionCreate fills
valid XSESSION_INFO → bots to party_minplayers → 10s timer → map load). Owner-preference note: this host
decision is inherently game-side; the "proper SDK" angle is resolving `[0x8239D098]` via 0x58006 + filling
`[0x84C20FF4]`/session info in `xgi_app.cpp`, but the one-caller gate hook is the reliable unblock.

### 2026-06-26 — ✅ HOST-FALLBACK NOW FIRES (COD4_MMHOST shipped) → next wall = XSessionCreate validity
Implemented the gate hook: **`[COD4MP-MMHOST]` in `src/cod4mp_patches.cpp`** — `REX_FUNC(sub_821AC9A0)`
returns 1 under env **`COD4_MMHOST`** (rebuild: `ninja -C out/build/linux-amd64-release cod4_mp`). TEST
(`COD4_LIVE=1 COD4_PLAYLIST=1 COD4_MMHOST=1 COD4_MM_SEARCH_DELAY_MS=6000`, Find Match → playlist → lobby):
- `[COD4MP-MMHOST] forcing gate sub_821AC9A0 -> 1` fires; the party **promotes to host and now calls
  `XSessionCreate` (0x000B0010) — which it NEVER did before.** ✅ Host decision unblocked.
- **But the lobby still shows "Searching":** log timeline = MMHOST → 2× `XSessionCreate (000B0010)` →
  immediately 2× **`XSessionDelete (000B0011)`**. The game creates the host session, finds it invalid, and
  tears it down → falls back to search (party_host reads 0 again). CAUSE = our XGI `XSessionCreate` stub
  (`xgi_app.cpp:64`) returns SUCCESS without filling `session_info_ptr` (XSESSION_INFO) or `nonce_ptr`.

**NEXT (handoff plan step 2, SDK):** `XSessionCreate 0x000B0010` must fill a valid **XSESSION_INFO** at
`session_info_ptr` (XNKID sessionID @+0 [8B, nonzero], XNADDR host @+8 [36B, loopback/our System-Link addr],
XNKEY keyExchangeKey @+44 [16B]; total 60B) + a nonzero **nonce** (u64) at `nonce_ptr`. Then the host
session is "valid" → not deleted → party proceeds: fill to `party_minplayers=2` with a bot → 10s
`party_gameStartTimerLength` → loads `party_mapname`/`party_gametype` (Shipment/war) → System-Link bots
take over (COD4_GSCINJECT/BOTSPAWN/BOTAI). Verify XSessionCreate's caller (game side) to see exactly which
fields it validates (sessionID!=0 likely). This is the same fix that clears Private Match's "Error creating
session" 0x80004005. Also still TODO: make host-fallback reliable WITHOUT `COD4_MM_SEARCH_DELAY_MS`
(predicate must be evaluated — confirm the SM reaches the process branch under instant completion).

**The mechanism is now clear:** host-fallback = the matchmaking SM calling `0x822B90E8`. In our Find-Match
search loop that call NEVER fires (no `XSessionCreate`), so the SM never takes a host transition. The
decision gate is the branch upstream of `0x822B9604`/`0x822B9828` (and/or whatever sets the session-state
byte they read @ `+0x9D0`/`+0xC`). **NEXT RE STEP:** (a) disassemble the body of `0x822B90E8` to confirm
role + the meaning of the `0x824357xx`/`0x8246ECxx` party-state globals; (b) find the function(s)
containing the `0x822B9604`/`0x822B9828` tail-calls and the condition that selects host vs keep-searching;
(c) dynamically (gdb on the recompiled `sub_822B90E8`) confirm whether it's reached at all in the lobby and
which caller would fire — then identify the online/session flag the gate needs (candidate: the session
state byte @ struct`+0x9D0`+0xC, or a "search passes exhausted" counter). Once the gate is known, either
fake the flag (SDK/`xgi`/`xlivebase`) or force the transition (game-side `[COD4MP-*]` poke), then the
existing host path + System-Link bots take over. The `COD4_MM_SEARCH_DELAY_MS` toggle (default 0) is kept
as a search-lifecycle test lever.

## ROOT CAUSE (precise — but see the 2026-06-26 UPDATE above: this is INCOMPLETE)
Enable **`COD4_LIVE_TRACE=1`** → during the search the game spams these app/msg via XMsgStartIORequest:
| app | msg | meaning | ×/run | handler |
|---|---|---|---|---|
| 0xFB (XGI) | `0x000B0006` | XGIUserSetContextEx | 2098 | `xgi_app.cpp` ✅ stub→SUCCESS |
| 0xFB | `0x000B0007` | XGIUserSetPropertyEx | 1044 | ✅ stub→SUCCESS |
| 0xFB | **`0x000B001C`** | **XSessionSearchEx** (the search) | 518 | ⚠️ stub→SUCCESS, **writes NO results** |
| 0xFB | `0x000B0010` | XSessionCreate | 2 | ✅ stub→SUCCESS (no real session) |
| 0xFB | `0x000B0012` | XSessionJoin* | 2 | stub |
| 0xFC (XLiveBase) | `0x0005800E` | status/notify poll | 1181 | (check `xlivebase_app.cpp`) |

Async dispatch is synchronous under the hood: `xeXMsgStartIORequestEx` (`xam_msg.cpp:54`) calls
`DispatchMessageAsync` then `CompleteOverlappedImmediate(overlapped, result)` — so overlappeds DO complete.
**The blocker:** `XGI 0x000B001C` (XSessionSearchEx, `xgi_app.cpp:164`) returns `X_E_SUCCESS` but never
WRITES the result buffer — it doesn't zero the result count in the `X_SESSION_SEARCHRESULT_HEADER` at
`search_results_ptr` (buf+28, with `results_buffer_size` at buf+24). So the game can't conclude "0 games
found" and never falls through to **hosting**. No unimplemented-message errors fire — it's an OUTPUT bug.

## THE PLAN (proper mock-360 matchmaking, in SDK `xgi_app.cpp` unless noted)
1. **XSessionSearchEx (`0x000B001C`): return a well-formed EMPTY result set.** Zero the result-count field
   in the caller's `search_results_ptr` header (XSESSION_SEARCHRESULT_HEADER: `dwSearchResults` count = 0,
   `pResults` = 0). Then the game sees 0 games → host-fallback fires → `party_host`→1. (Verify the exact
   header layout from Xenia `xgi_app`/`xam_net` or the IW3 caller `sub_82xxxxx` that reads it.)
   - Later (real matchmaking): to JOIN instead of host, return 1+ results pointing at a faked session — but
     for single-box + bots, **host is what we want**.
2. **XSessionCreate (`0x000B0010`): produce a usable session.** Currently returns SUCCESS without filling
   `session_info_ptr`/nonce. Fill a deterministic XSESSION_INFO (sessionID, key, host XNADDR = loopback/our
   System-Link addr) + nonce so the host session is valid. This is also what fixes Private Match's
   "Error creating session" (`0x8207dad0`, error `0x80004005`).
3. **Drive the party state machine to start** (game side, file 18: dvars `sub_822B9FB0`, frame likely
   `sub_822B8238`, uiscripts `sub_822C8108`). After hosting: fill to `party_minplayers=2` with a BOT (reuse
   our bot-add: `addtestclient`/`COD4_ADDBOTS` path, memory `cod4-mp-bots-playing`), the **10s
   `party_gameStartTimerLength`** counts down → internal start consumes `party_mapname`/`party_gametype`
   (Shipment/war) → loads the match. Bots already PLAY in System-Link matches, so once it launches we reuse
   `COD4_GSCINJECT/BOTSPAWN/BOTAI`.
4. **Confirm XP accrual:** the playlist is `ranked` → the launched match should grant XP into the stat block;
   pairs with Path B persistence (`COD4_STATS`, memory `cod4-mp-rank-unlock-finding`). Probe rank byte
   `0x84C59D20+256` / RANKXP dword `+3208` before/after a kill.
5. **Also check `XLiveBase 0x0005800E`** (polled 1181×, `xlivebase_app.cpp`) — likely a connection/notify
   status the search loop checks; make sure it returns "connected/idle" not "busy".

## HOW TO WORK IT
- **Trace:** `COD4_LIVE=1 COD4_LIVE_TRACE=1 COD4_PLAYLIST=1` then grep the run log for
  `async app=… msg=…` / `sync app=…` (the tag is `[COD4MP-LIVE]`). Distinct ids:
  `grep -oE "(async|sync) app=[0-9A-Fa-f]+ msg=[0-9A-Fa-f]+" … | sort | uniq -c`.
- **Drive to the lobby (headless, DISPLAY=:1):** patched driver
  `…/scratchpad/drive_wid.py` (screenshots the game window BY ID via wmctrl, not -window root):
  `python3 drive_wid.py --boot 36 --seq "a s4 a s6 a s8"` (Xbox LIVE → Find Match → select playlist → wait).
- **Playlist iterate without rebuild:** `COD4_PLAYLIST_FILE=/path` overrides the built-in
  (`[COD4MP-PLAYLIST]` in `src/cod4mp_patches.cpp`); probe script `/tmp/plprobe.sh`.
- **dvar/struct probes:** read `/dev/shm/xenia_memory_*` (BE). Party dvars (struct +0xC int): scan for a
  struct whose +0 = the name string VA. Playlist struct base `0x84A0CD50` stride 8528 (entryCount@+8512).
- **SDK rebuild:** edit `rexglue-sdk/src/kernel/xam/apps/xgi_app.cpp` →
  `ninja -C out/build/linux-amd64 install:Release` (game auto-loads the new .so; no game rebuild needed).

## KEY ADDRESSES / FILES
- SDK matchmaking: `rexglue-sdk/src/kernel/xam/apps/xgi_app.cpp` (app 0xFB / XSession), `xlivebase_app.cpp`
  (0xFC), `xam_msg.cpp` (XMsgStartIORequest async→overlapped), `xam_net.cpp`, `xam_user.cpp`/`user_profile.cpp`.
- Game party/lobby (file 18): dvars `sub_822B9FB0`; uiscripts (`xpartygo`/`xplaylistchoosegame`)
  `sub_822C8108`; party frame likely `sub_822B8238`; start consumes `party_mapname`@0x8205C770 /
  `party_gametype`@0x8205C780. `xpartygo` cmd = "private match only" → Find Match auto-starts via the SM.
- Playlist: parser `sub_821E6F90`, text buffer `0x84C495F8`, struct `0x84A0CD50`.
- "Error creating session" string `0x8207dad0` (= 0x80004005, also Private Match).

## NOTE — the broader mock-360 surface (already partly done, all reusable in SDK)
XAM user/profile (signin, profile read/write + disk persistence), XStorage (download/build-path), XGI
(XSession contexts/properties/create — stubs), XLiveBase (storage/stats/status msgs), LSP enumerate. Filling
out XGI session-create + search + the XLiveBase status poll completes a minimal but real **offline Live /
LAN-style matchmaking** that any recomp can reuse.
