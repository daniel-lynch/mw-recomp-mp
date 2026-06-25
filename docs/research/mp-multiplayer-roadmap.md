# CoD4 MP (iw3mp) recomp — Multiplayer Roadmap: friends + bots + ranking

**Project:** `/home/dlynch/dev/mw-recomp-mp` (recompiled Xbox 360 CoD4 MP / iw3mp, on the prebuilt
`rexglue-sdk`). **Date:** 2026-06-24.
**End goal:** play CoD4 MP with a friend + bots (ideally smart "Bot Warfare"-style AI) and **rank up**,
"like the old days." **Companion docs:** `mp-connect-bots-handoff.md` (deep RE history). **Memories:**
`cod4-mp-startmatch-spin`, `cod4-mp-live-backend-fake`, `cod4-mp-bots-playing`.

---

## 0. Mental model (read this first)

You do **not** need to build "matchmaking." Real Xbox Live matchmaking (find random games) is dead
servers and isn't the goal. The goal is **a private match you host, your friend joins, filled out with
bots.** The host already works. So the project is three additive gaps, not a from-scratch server:

1. **Bots that PLAY** (currently connect but spectate) — local, no networking.
2. **Friend connect** over the network (the real "server" work) — System Link as a LAN/VPN testbed,
   then the Xbox **Live → Private Match** menu as the eventual front-end (user's stated preference).
3. **Ranking / XP persistence** (stats are currently faked empty → rank 1).
4. **Stretch: smart Bot Warfare AI** (custom GSC injection).

### ⭐ MVP TARGET (agreed 2026-06-24)
**Queue into a game → persistently-named bots load → player loads in → the bots actually PLAY.**
Bot *tuning* (skill/personality) is explicitly OUT of MVP scope — stock bots are fine. Friend-connect
(Phase 2 netcode) is a **fast-follow, NOT in the MVP** (MVP is solo player + named bots). The MVP's
critical path has exactly **one hard gate: Phase 1 bots-that-play (team-join via menuresponse).**
Everything else is cheap: names come from a roster DB (Phase 5 Layer B, external software), and "queue"
is just our orchestrator picking gametype+map+subset and driving a private match.

REALITY CHECK on "tie it into a backend DB/matchmaking — straightforward": the **DB/roster/sampler side
IS straightforward** (normal host-side software, no RE). But this is a **recompiled binary, not editable
game source** — there is no matchmaking API to plug into; the game's matchmaking is the dead Xbox Live
stack we fake. So "queue" = orchestrator-driven private match, "bots work" = the Phase-1 RE. The RE is the
work; the backend is the garnish.

**MVP build order:** (1) Phase 1 bots team-join+spawn. (2) set each injected bot's NAME from a roster
file (find how a testclient's userinfo/name is set; `addtestclient` core = sub_82205A08). (3) thin
orchestrator: roster DB (names, e.g. "BOSSMAN PAC", "xX_ghost_Xx [W524]" clan-tag style) → pick subset →
write roster file → launch+drive game into a match. (4) optional early: background progression sim so the
named roster has evolving ranks. Phase 4 (smart AI) and Phase 2 (friend) come after MVP.

---

## 1. What already works (baseline)

- Boots to the MP main menu; **Split Screen → Start Match → spawns into first-person Backlot TDM**
  (connstate 9 = CA_ACTIVE). Root unblock = a rexglue jump-table mis-detection fix in
  `config/functions_mp.toml` (`[[switch_tables]]` @0x8220A2D0 reg 10). See `cod4-mp-startmatch-spin`.
- **System Link → Create Game hosts a real listen server**; `XSessionJoinRemote()` adds clients.
  `COD4_ADDBOTS=11` → **12 clients connected** (1 human + 11 bots) — but **bots spectate**.
- **signin=2 (Xbox Live) boots to working Live menus** (Xbox LIVE w/ green online dot, Barracks w/ a
  stats panel, Private Match) via a faked dead XStorage/LSP backend. `COD4_LIVE=1`. See
  `cod4-mp-live-backend-fake`. Create-a-Class is reachable but greyed (rank-gated → Phase 3).

### Env flags (all default-off; the live/bot features are opt-in)
| Flag | Effect |
|---|---|
| `COD4_LIVE=1` | signin=2 + fake the dead Xbox Live backend (XStorage/LSP/XnAddr ONLINE). |
| `COD4_LIVE_TRACE=1` | stderr-log every XAM message dispatch (app/msg/buffer). |
| `COD4_ADDBOTS=N` | add N `addtestclient` bots once cl0 is active; also bumps sv_maxclients to N+1. |
| `COD4_CONSOLE=1` | re-enable the Com_Printf echo (off by default — silences LSP spam). |
| `COD4_BOTLOG=1` | log every dvar name the GSC reads via `getdvarint`. |
| `COD4_CMD="…"` / `COD4_CMD_DELAY=s` | inject a console command via Cbuf (sub_82238768). |

### Build / run / drive (from repo root unless noted)
```
# SDK (if edited):   cd /home/dlynch/dev/rexglue-sdk && cmake --build out/build/linux-amd64 --config Release --target install -j24
# Codegen (toml/xex): rexglue codegen cod4_mp_manifest.toml   (bin at rexglue-sdk/out/install/linux-amd64/bin)
cmake --build out/build/linux-amd64-release -j24
COD4_LIVE=1 DISPLAY=:1 python3 tools/cod4_mp_drive.py --boot 32 --seq "a s6"     # -> Xbox Live menu
COD4_BOTLOG=1 DISPLAY=:1 python3 tools/cod4_mp_drive.py --boot 16 --seq "s2 d a s2 a s2 a s2 a s25"  # Split Screen -> CHOOSE TEAM
# ALWAYS after a kill:  pkill -9 -x cod4_mp ; rm -f /dev/shm/xenia_memory_*
```
Log: `/tmp/cod4mp_drive/cod4mp.log`. Screenshot the game window directly (not `-window root`):
`WID=$(DISPLAY=:1 wmctrl -l | grep -i cod4_mp | awk '{print $1}'); DISPLAY=:1 import -window $WID out.png`.
All game-side hooks live in `src/cod4mp_patches.cpp` (`[COD4MP-*]` tags); SDK fakes in
`rexglue-sdk/src/kernel/xam/apps/xlivebase_app.cpp`, `xam_net.cpp`, `src/system/xam/app_manager.cpp`,
`include/rex/system/xam/user_profile.h`.

---

## 2. PHASE 1 — Bots that PLAY (team-join + spawn)  ⟵ start here

**State:** bots CONNECT (`addtestclient` handler `sub_82263110`, called by `[COD4MP-BOTS]`) but never
pick a team, so they spectate.

**KEY FINDING (course-correction):** the handoff's "preferred path" (set `scr_testclients`=N so the
stock GSC auto-spawns+joins) is **DEAD** — the gametype GSC in this build's fastfiles **never reads
`scr_testclients`** (proven via the `[COD4MP-BOTLOG]` hook on `getdvarint`=`sub_8225B158`; the 39 dvars
it reads are all gametype settings). There is no GSC consumer to drive.

**THE PATH: inject the `menuresponse` client command per bot** — replay exactly what a human's
"Choose Team → Auto-Assign → pick class" does. The GSC handles that path (it's how anyone joins).

### Pieces already located
- `addtestclient` handler `sub_82263110` → real core `sub_82205A08` (returns the new client/entity).
- `menuresponse`@0x8206A0A0 (registered in client-cmd registrar `sub_8235E5B0`, recomp.23, via
  `sub_8221CF38(name,1,13,6)` → interned id in the global struct @0x82621808). `changeclass`@0x820746F8.
- Team arg strings (runtime menu pool, supply our own command text): team_marinesopfor@0x82BB73C4,
  autoassign@0x82BC8E9C.
- GSC builtins (found via name-node table, BE-ptr search over `tools/mp_image.bin`):
  `getdvarint`=sub_8225B158, `setteam`=sub_82257610, name-node region ~0x823A2C10/0x823A37B0/0x823A3264.
- GSC string pool base = `*(0x82B8405C)`; entries 12 bytes, text at +4 (1-based index from
  Scr_GetParamString = sub_8220E640).

### Next actions
1. **Find `SV_ExecuteClientCommand(client, "menuresponse team_marinesopfor autoassign")`.** Not
   string-findable (release build stripped) and the cmd-id table is in a giant shared global, so static
   RE is slow. Do it **empirically**: drive the human (cl0) to Auto-Assign once and capture the
   processing fn + clientNum + arg string — via a targeted hook or a gdb backtrace at the moment of
   join — then replay it for each bot's clientNum.
2. Per bot: inject `menuresponse team_marinesopfor autoassign`, then a `changeclass <class>` to spawn.
   Get the exact arg syntax from the captured human flow. Bot clientNums come from tracking
   `sub_82205A08` results (or just iterate svs.clients slots 1..N).
3. Verify bots spawn on teams and the match scores. NOTE: stock testclients may barely move/shoot — Phase
   1 gives team-joined *bodies* (shootable, fill the match); real combat AI is Phase 4.

**Risk:** if menuresponse for a bot client is rejected server-side (e.g. needs a valid usercmd/connect
state), fall back to finding the native team-assign fn the GSC ultimately calls and invoking it directly.

---

## 3. PHASE 2 — Friend connect (the real "server" work)

**Transport plan:** System Link over a VPN (ZeroTier/Hamachi-style) as the **testbed** (two machines on
one virtual LAN, host Create Game, friend joins via the System Link browser), then move the front-end to
the Live **Private Match** menu (user's preference) once the netcode is proven.

**Networking reality (checked):**
- SDK **has a real native UDP socket layer** — `src/system/xsocket.cpp` (`sendto`/`recvfrom`),
  wired to `WSASendTo`/`WSARecvFrom` in `xam_net.cpp`. Raw packets can flow.
- **BUT `XNetXnAddrToInAddr` is a stub** (`xam_net.cpp`, returns 1, writes nothing), as are
  `XNetInAddrToXnAddr`, `XNetConnect`. So a peer's secure XNADDR never resolves to a real IP → today
  the local match runs over **in-process loopback with zero real socket I/O**. This is THE blocker.

### Work
1. **Implement XNADDR↔real-IP mapping** (`XNetXnAddrToInAddr` / `XNetInAddrToXnAddr` /
   `XNetServerToInAddr`): allocate a virtual IN_ADDR per peer XNADDR and keep a table that the socket
   layer translates to the real LAN/VPN IP+port when sending. (Xenia/Cxbx and "XLink Kai"-style bridges
   do exactly this.)
2. **System Link broadcast bridge:** make the host's `XNetGetTitleXnAddr` advertise a routable address
   and ensure System Link discovery broadcasts (the LAN browser) are sent/received over the VPN
   interface so the friend's box finds the host's session.
3. **XNet secure-socket encryption:** confirm whether CoD4's VDP/netchan traffic is XNet-encrypted
   (keys normally exchanged via the Live session). If so, bypass/disable so plaintext CoD4 netchan flows
   between the two recomp instances (both are "us," so no real crypto needed).
4. Validate: 2 machines on a VPN, host Create Game (System Link), friend joins via browser, both spawn.
5. Port the front-end to **Live → Private Match**: needs more of the dead Live session/party stack faked
   (XSession create/join, invites). Build on `cod4-mp-live-backend-fake` (app 0xFC/0xFB message fakes).

**Risk:** the secure-socket layer and System Link discovery are the unknowns; budget real time. A
direct-IP "connect <ip>" path (if one exists in iw3mp) could be a simpler interim than full System Link.

---

## 4. PHASE 3 — Ranking / XP persistence

**State:** the Live stats download (msg `0x00050009`) is faked to return **success + 0 bytes**, so stats
are empty → **rank 1**, and Create-a-Class is greyed (rank-gated).

### Work
1. **Persist a real stat blob:** on the stats download (`0x00050009` for the stats file) return a stored
   blob from a local file instead of empty; implement the **upload** counterpart
   (XStorageUploadFromMemory) to write the blob back. This makes XP/rank/unlocks survive across sessions.
2. Reference points (from handoff): RANKXP stat @0x8206A524; `rankTable.csv` loader sub_822B5E48;
   stat blob accessor sub_821A1CC8 / statGet sub_821A2090; **setRank builtin sub_8227C830** (call-the-
   handler trick, like addtestclient). Pushing RANKXP high unlocks classes locally.
3. Knock-on: with real XP, **Create-a-Class ungreys** (also needs onlinegame=1 + ui_allow_classchange=1)
   and rank-up notifications fire in matches.

---

## 5. PHASE 4 — Smart bots (Bot Warfare)  [stretch]

"Bot Warfare" is a GSC mod (PC CoD4). This recomp loads GSC from the game's fastfiles, so adding it means
**injecting custom GSC** into the script VM — either rebuild/patch the `.ff` fastfiles to include the mod
scripts, or hook the GSC script-loader to register extra scripts/threads. Research-heavy; do it only
after Phase 1 (team-join) and ideally the netcode work, since it's the largest unknown. Stock bots
(Phase 1) are the bridge until then.

---

## 5.5 PHASE 5 — Playlist experience + persistent bot population ("the living game" layer)

**Vision (user):** queue for a gamemode → rotating maps + vote-to-skip + a lobby between matches; and a
persistent stable of **100–1000 "unique" bots** with distinct names / skills / playstyles that you face
a random subset of each match, and that **progress in the background** (rank up, prestige) so the
population feels alive, "like the old days."

**Key reframe — splits into 3 layers by difficulty:**

**A. Game systems we just DRIVE (mostly already in CoD4):**
- Map rotation + gametype: `sv_mapRotation` / the playlist system + `map_rotate`. CoD4 rotates natively.
- Vote-to-skip / map vote: CoD4's end-of-match map vote + `callvote`. Enable + drive.
- Lobby between matches: the intermission / post-game lobby exists; keep bots + human in it between maps.
- "Queue for a gamemode": repurpose Find Match / playlist-select → start a private match with the chosen
  gametype + our bot subset on the rotation.
  → These are config/driving tasks, not new engines, but they ride on **Phase 1** (bots in the match).

**B. Population data layer we BUILD (normal host-side software — easy & fun, no guest RE):**
- **Roster DB** (JSON/SQLite): 100–1000 bots, each `{name, skill tier, playstyle (rusher/camper/sniper/
  objective…), preferred class/weapons, rank, XP, prestige, lifetime K/D & stats, fav map}`.
- **Gamertag generator**: 1000 plausible CoD-era names.
- **Queue sampler**: on queue, pick the match's bots from the roster (optionally skill-matched to your
  rank for an SBMM flavor + variety); persist the matchup.
- **Background progression sim**: a scheduled offline simulator (could use `/schedule` or a cron/loop)
  that periodically "plays" simulated games for the roster, advancing XP/rank/prestige/stats by each
  bot's skill + variance. Come back later → the population has moved.
- **Bridge to the game**: the orchestrator writes the chosen subset (names + per-bot skill/playstyle/
  rank) to a roster file/env the game reads when injecting bots (extends `COD4_ADDBOTS`).

**C. AI quality that makes skill/personality REAL = Phase 4 (Bot Warfare).** Stock testclients have no
skill knobs; Bot Warfare exposes per-bot difficulty/behavior. So "different skill levels & playstyles"
genuinely requires the GSC-injected AI.

**Cheap early win (decoupling insight):** the *persistent-identity + progression illusion* (named bots at
DB ranks, facing "a subset of my 1000," population progressing in the background) can land EARLY with even
stock/dumb Phase-1 bots — they just won't *play* smart yet. **Layer B is independent external software
buildable now**; it only needs Phase 1 to inject *named* bots, and gets its payoff upgrade from Phase 4.
So Layer B can proceed in parallel; skill/personality switches on with Phase 4.

**Scale note:** 1000 is the POPULATION (DB rows); you only ever inject ~the team-fill (~11) per match —
well within `sv_maxclients`. No engine limit.

**Open questions for Phase 5:** does iw3mp expose `sv_mapRotation`/`map_rotate` + the vote UI in this
build (probe like the `getdvarint` BOTLOG)? Can we drive the post-match lobby + restart the rotation
without the dead Live playlist backend? How are per-bot Bot Warfare skill/personality params set
(dvars vs GSC) so Layer B can address an individual bot?

---

## 6. Sequencing & open questions

**Recommended order:** Phase 1 (bots play) → Phase 2 (friend netcode) → Phase 3 (ranking) → Phase 4
(smart AI) → Phase 5 (playlist + population). BUT **Phase 5 Layer B (roster DB, names, sampler,
background progression sim) is independent external software and can be built in parallel any time** —
it only needs Phase 1 to inject *named* bots for an early "living population" taste, and Phase 4 to make
the skills/personalities real. This vision also **raises Phase 4's priority** (it's the gate for
per-bot personality/skill). Phase 1 remains the test harness for everything.

**Open questions to resolve as we go:**
- Will stock testclients move/shoot at all, or just stand? (Determines how soon Phase 4 is needed.)
- Is CoD4 netchan XNet-encrypted on this title? (Determines Phase 2 socket work.)
- Does iw3mp expose any direct-IP connect path (simpler interim than System Link discovery)?
- GSC injection feasibility for Bot Warfare (fastfile rebuild vs loader hook).

**Cross-cutting reminders:** `pkill -9` leaks `/dev/shm/xenia_memory_*` (~4.8 GB each) → always
`rm -f` after. Guest mem is big-endian; guest base 0x100000000. Release SDK emits no log macros —
probes must fprintf(stderr) or fopen a file. Keep the jump-table fix in `functions_mp.toml`.
