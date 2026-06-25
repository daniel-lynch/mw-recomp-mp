# CoD4 MP recomp — Session Handoff (2026-06-24)

**START HERE for next session.** Project: `/home/dlynch/dev/mw-recomp-mp` (recompiled Xbox 360 CoD4 MP /
iw3mp on prebuilt `rexglue-sdk`). This session got Xbox Live menus working, bots team-joining AND
spawning in both Split Screen + System Link, and scoped/cloned Bot Warfare. **Companion docs:**
`mp-multiplayer-roadmap.md` (the full 5-phase plan + ⭐MVP), `mp-connect-bots-handoff.md` (older deep RE).
**Memories:** `cod4-mp-live-backend-fake`, `cod4-mp-bots-playing`, `cod4-mp-botwarfare-gsc`,
`cod4-mp-startmatch-spin`.

---

## ⭐ THE MVP (agreed)
"Queue into a game → persistently-NAMED bots load → player loads in → bots PLAY." Friend-connect =
fast-follow (not MVP). User order: **System Link ✓ → bot names → Bot Warfare**. We have a RECOMPILED
BINARY (not editable source) — the DB/orchestrator is easy; the in-game RE is the work.

---

## STATUS — what works NOW (this session's wins)

1. **Xbox Live menus boot (`COD4_LIVE=1`)** — signin=2; reaches Xbox LIVE menu, Barracks (stats panel),
   Private Match. Faked the dead XStorage/LSP backend. Create-a-Class is reachable but greyed (rank-gated
   → Phase 3 ranking). See `cod4-mp-live-backend-fake`. 5 changes, all gated on env `COD4_LIVE`.

2. **Bots TEAM-JOIN** (Split Screen + System Link), `COD4_ADDBOTS=N`. Scoreboard-confirmed: 1 human + N
   bots auto-assigned across Marines/OpFor (named bot0..N).

3. **Bots SPAWN** ✅ (the big one). Bots reach CS_ACTIVE (SV_AddTestClient drives them fully). Spawn was a
   TIMING bug: team + class menuresponse must be injected in SEPARATE frames (~2s apart) or spawnPlayer
   never fires. Now spaced → bots spawn into bodies (minimap green teammate + enemy markers). They STAND
   STILL (no AI yet). See `cod4-mp-bots-playing`.

4. **Bot Warfare cloned + scoped** — `iw3_bot_warfare/` (`gh repo clone ineedbots/iw3_bot_warfare`).
   Adapter-based: reuse 17k-line brain, write a small **iw3mp adapter**. See `cod4-mp-botwarfare-gsc`.

---

## ➡️ IMMEDIATE NEXT STEP: usercmd injection (make a bot MOVE on command)

This is the foundation Bot Warfare sits on. Bots are spawned but stationary. Need native primitives the
BW adapter calls: **`botmoveto(pos)`** (walk toward a world pos), **`botaction(action)`** (press button),
**`botstop()`**, **`botangles`** (= stock `setplayerangles`, already exists). Implement by injecting the
bot's per-frame **usercmd** (forwardmove/rightmove/buttons/viewangles).

**Where to start (RE):**
- Find **SV_ClientThink / SV_BotUserMove** — the per-frame server fn that consumes a client's usercmd and
  runs player movement. For bots it likely reads a zeroed/last usercmd. Find the usercmd struct + where a
  bot's usercmd lives (per-client). The usercmd has forwardmove/rightmove (sbyte), buttons (bitfield),
  weapon, viewangles.
- SV_ClientEnterWorld (sub_822042E0) builds a zeroed usercmd at r1+144 (32 bytes: 4× std r27=0) and
  passes it — shows the usercmd struct size (~32 bytes) + that bots start with a zero usercmd.
- The MVP test: each frame, write a nonzero forwardmove into the bot's usercmd (or call the movement fn)
  and confirm the minimap teammate arrow MOVES. Then expose as a callable for the adapter.
- Strategy: blanket-probe the sv_client.c neighborhood (sub_82205xxx–sub_82208xxx, recomp.12) for the
  per-frame client-think fn (like we found SV_ExecuteClientCommand), or trace from SV_Frame.

**Then:** wire Bot Warfare — GSC source injection (hook script-source fetch under Scr_LoadScript) + write
the iw3mp adapter mapping do_botmoveto/do_botaction → the native usercmd injection (new GSC builtins) +
bootstrap `maps\mp\bots\_bot::init()` at CodeCallback_StartGameType (hook sub_822634A0). See
`cod4-mp-botwarfare-gsc` for the full plan.

---

## KEY ADDRESSES / FUNCTIONS (discovered this session)

**Bots / SV_client (recomp.12 unless noted):**
- svs.clients = **`*(0x82F82D8C)`** (BE); client_t **stride = 0xA2C08**; client N* = svs.clients+N*stride.
  Human=cl0, bots=cl1..N. clientNum = (cl-svs.clients)/0xA2C08. sv_maxclients dvar* @ `*(0x82EE1D78)`,
  int value at +12. CS_ACTIVE=4 at client_t+0 (server state; distinct from client connstate 0x82435780).
- **SV_AddTestClient = sub_82205A08** (addtestclient GSC builtin wrapper = sub_82263110).
- **SV_ExecuteClientCommand = sub_82205CB8(client_t* cl, char* cmd, int clientOK)** — bot-cmd injection.
- **SV_DirectConnect = sub_822046C0** (FREE→CONNECTED), **SV_SendClientGameState = sub_822052B0**
  (CONNECTED→CLIENTLOADING), **SV_ClientEnterWorld = sub_822042E0(client_t*, usercmd*)** (→CS_ACTIVE).
- Bot JOIN commands: **`mr 16 4 autoassign`** (team) then **`mr 16 13 offline_class1_mp,0`** (class).

**GSC engine (for Bot Warfare injection):**
- **Scr_LoadScript = sub_82220780** (→sub_82220518; source fetch deeper via sub_8221D320 — the inject hook).
- **Scr_GetFunctionHandle = sub_82220048(script, "FuncName")**.
- **GSC bootstrap = sub_822634A0** (loads gametype + _callbacksetup, resolves CodeCallback_*; handles
  stashed ~0x82614A88). Engine calls CodeCallback_StartGameType@0x820657CC / _PlayerConnect@0x8206580C /
  _PlayerKilled@0x82065864 etc.
- getdvarint builtin = sub_8225B158; GSC string pool base = `*(0x82B8405C)` (12B entries, text +4);
  setteam builtin = sub_82257610. Scr_GetParamString(0)=sub_8220E640.

**Live (SDK, gated COD4_LIVE):** user_profile.h signin_state()→2; xlivebase_app.cpp msgs 0x00050009 +
0x00058035 →SUCCESS; xam_net.cpp XNetGetTitleXnAddr +ONLINE bit; app_manager.cpp COD4_LIVE_TRACE.
Game-side: sub_8210AC38 (BuildServerPath fake), sub_821032F8 (LSP XamEnumerate→997 when lr==0x821AB344).

---

## CODE STATE — `src/cod4mp_patches.cpp` (all env-gated, default off)

- `[COD4MP-BOTS]` — the main bot logic. `sub_822CB3B0` (per-frame pump) paced by atomic `g_bot_trigger`
  (armed when cl0 sends `mr 16` in the sub_82205CB8 hook): t≥120 add N bots, t≥360 inject team, t≥480
  inject class. `cod4_client_cmd(num,cmd)` = the injector (writes cmd to r1-0x800, calls __imp__sub_82205CB8).
  Env: `COD4_ADDBOTS=N`, `COD4_BOTNOJOIN=1` (add only).
- `[COD4MP-SVPROBE]` (env `COD4_SVPROBE`) — logs ExecClientCmd, SV_SendClientGameState, SV_ClientEnterWorld
  (clientNum). Diagnostic.
- `[COD4MP-LIVE]` — sub_8210AC38 / sub_821032F8 fakes (gated COD4_LIVE).
- `[COD4MP-CONSOLE]` (env `COD4_CONSOLE`, default OFF) — Com_Printf echo (sub_82234CB8). The ONLY console
  sink; gating it silenced the LSP spam.
- `[COD4MP-BOTLOG]` (env `COD4_BOTLOG`) — getdvarint name logger. `[COD4MP-FIX1]` keep (hang guard).
- Older diagnostic NET2/CONN/GSCERR/CMD/MAXCLIENTS hooks present, env/dormant.

**SDK** (`/home/dlynch/dev/rexglue-sdk`): the Live fakes above + the long-standing System Link net fakes
(XNetGetEthernetLinkStatus ACTIVE, XnAddr LAN IP). NETGATE A/C/D bypasses are baked into generated/*.cpp.

---

## BUILD / RUN / TEST
```
# SDK (only if edited): cd /home/dlynch/dev/rexglue-sdk && cmake --build out/build/linux-amd64 --config Release --target install -j24
# Codegen (only if toml/xex): rexglue codegen cod4_mp_manifest.toml
cmake --build out/build/linux-amd64-release -j24                              # game (from repo root)
# Split Screen + bots (fastest bot test; reaches CHOOSE TEAM then spawns):
COD4_ADDBOTS=3 COD4_SVPROBE=1 DISPLAY=:1 python3 tools/cod4_mp_drive.py --boot 16 --seq "s2 d a s2 a s2 a s2 a s18 a s5 a s28"
# Xbox Live menu:  COD4_LIVE=1 DISPLAY=:1 python3 tools/cod4_mp_drive.py --boot 32 --seq "a s6"
# ALWAYS after: pkill -9 -x cod4_mp ; rm -f /dev/shm/xenia_memory_*
```
Screenshot the GAME window (not root): `WID=$(DISPLAY=:1 wmctrl -l | grep -i cod4_mp | awk '{print $1}');
DISPLAY=:1 import -window $WID out.png`. Zoom regions with `convert in.png -crop WxH+X+Y +repage -scale ... out.png`.

## GOTCHAS (cost time this session)
- **Log files contain NUL bytes** (console mem dumps) → `grep` treats them BINARY and silently finds
  nothing. ALWAYS `grep -a`.
- **Virtual pad (drive.py) CANNOT navigate the System Link "Create Game" popup submenu**, and it CONFLICTS
  with a real controller. For pad-driven runs the user's REAL controller must be OFF. (System Link Create
  Game works fine pad-only when no real controller is active.)
- connstate `0x82435780` reads 0 in-game in Split Screen (unreliable) but 9 in System Link — DON'T trigger
  bots off it; use the cl0 `mr 16` command-trigger (works in both).
- `lis` immediate → VA conversion bit me twice (0x82F8AD8C vs 0x82F82D8C; 0x82FB vs 0x82B8). Double-check:
  `lis r,imm` → `(imm & 0xFFFF) << 16` (imm is the signed 16-bit; 65536+imm if negative).
- `pkill -9` leaks `/dev/shm/xenia_memory_*` (~4.8GB) → always rm. Guest mem big-endian; guest base 0x100000000.

## OPEN THREADS / cleanup
- Bot NAMES (user's next item): names built in SV_AddTestClient (sub_82205A08) via sub_8211D8E0 (userinfo
  fmt) — currently bot0..N. Override from a roster for the MVP.
- The spaced-timing spawn (team t≥360, class t≥480) works but the 2s gap is heuristic — may need tuning.
- Diagnostic hooks (SVPROBE/BOTLOG/CONSOLE/NET2/CONN) are env-gated & harmless but should be trimmed before
  any commit. KEEP [COD4MP-FIX1] and the real fixes.
