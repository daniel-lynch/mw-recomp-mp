# Phase 3 — Ranking / XP + Custom Classes — IMPLEMENTATION HANDOFF

## 2026-06-25 — PLAYLIST / MATCHMAKING (started): generation WORKS; Find-Match UI is online-gated

Goal: Find Match shows gamemodes (e.g. "Shipment 24/7") → queue → short lobby (with bots) → start.

**DONE — playlist generation:** CoD4's playlists are a TEXT format parsed by `sub_821E6F90` from the fixed
buffer `0x84C495F8` (filled by the dead Live game-settings download → empty by default → Find Match shows
nothing). `[COD4MP-PLAYLIST]` (gated `COD4_PLAYLIST`) injects a generated playlist into that buffer before
the parser runs. Grammar (extracted from the parser's keyword strings):
```
version <N>
gametype <type>          # type ∈ {dm war tdm dom sd sab ctf koth}; then: name english "<label>" ; script <name>
playlist <N>             # name english "<label>" ; description english "<text>" ; ranked ; teambased ;
                         #   maxparty <N> ; set <dvar> <val> ; rule ... ; then entry lines: "<gametype> <map>"
```
Maps live in a table @0x8203F00F (mp_shipment/mp_vacant/…); gametypes @0x8203F0F8. Our injected
`version 21 / gametype war / playlist 1 "Shipment 24/7" ranked / war mp_shipment` **parses with ZERO errors**
and populates the internal playlist struct (verified: "Shipment 24/7" + "Team Deathmatch" land @0x84A0EEA0,
the parser output region @~0x84A0CF50).

**⭐ The per-playlist `ranked` flag is in the playlist text** — so a generated ranked playlist is also the
lever for Path B's "earned XP" (a match launched from a ranked playlist should accrue XP). Ties tasks
together: build the match through a ranked playlist → rank up for real.

**Find Match display gate — ROOT CAUSE FOUND (2026-06-25):** the empty list is NOT (only) the online layer —
it's that the playlist has **0 entries**. The Find Match list feeder `sub_821E6A48` counts only playlists
whose **entry-count field `@+8512` != 0**. Parsed-playlist struct layout (base `0x84A0CD50`, stride **8528**,
playlist index 1 = our injected one): `+8512` = entry count, `+8520` = maxparty (our `12` ✓ stored),
entries array at `+1344` stride **56** (entry: map[31]@+0, gametype[15]@+32, weight(int)@+48 must be > 0).
Entry line format = `<map(≤31)> <gametype(≤15)> <weight(>0)>` (e.g. `mp_shipment war 1`).

**✅ SOLVED — Find Match now LISTS "Shipment 24/7"!** The entry format is **COMMA-separated**:
`<map>,<gametype>,<weight>` (e.g. `mp_shipment,war,1`) — NOT space-separated. The entry tokenizer reads the
whole line (flag@1031 rest-of-line) then splits on commas; my earlier space-separated lines became a single
map token with empty gametype/weight → rejected. Confirmed via memory probe (entryCount=1, map=mp_shipment,
gt=war, wt=1) AND on screen (Find Match shows "Shipment 24/7" + description). Format reference:
IW4 `playlists.info` (FreeTheTech101/IW4-Dump-Files) uses the same `mp_afghan,tdm,4` comma form. Built-in
playlist in `[COD4MP-PLAYLIST]` now: `version 21` / `gametype war`(name+script) / `playlist 1` "Shipment 24/7"
ranked+teambased+maxparty 12 / `mp_shipment,war,1`. `COD4_PLAYLIST_FILE` still overrides for iteration.

**✅ Selecting the playlist enters a LOBBY** ("Shipment 24/7" with Create a Class / Barracks / Invite
Friends, party panel) — then shows **"Searching for available games"** and hangs.

**Party/lobby state machine (file 18, sub_822B6xxx–822BExxx; dvars registered in `sub_822B9FB0`):** probed
the live lobby dvars — **`party_host=0`** (we're a CLIENT searching, not hosting), `party_minplayers=2`,
`party_maxplayers=18`, `party_gameStartTimerLength=10` (the 10s start countdown), `party_timer=0`. So the
flow needs: search FAILS → we HOST (`party_host`→1) → meet minplayers=2 (add a bot) → 10s timer → auto-start
(internal `xpartygo`; note the `xpartygo` command itself is "only for private match", so Find Match
auto-starts via the state machine). Key funcs: `xpartygo`/`xplaylistchoosegame` uiscripts in `sub_822C8108`;
party frame likely `sub_822B8238` (largest). Start consumes `party_mapname`/`party_gametype`.

**THE BLOCKER = the matchmaking SEARCH never completes** (dead Live `dw`/LSP/session backend; "Error creating
session" @0x8207dad0 = the `80004005` also seen entering Private Match). So the host-fallback never fires.
NEXT (deep, the Live session/matchmaking fake): make the session search COMPLETE with 0 results → host path,
fake `XSessionCreate`/session-start so hosting succeeds, then bots-to-minplayers + the 10s timer auto-starts
on `party_mapname`/`party_gametype`. Reuse System-Link hosting (bots already play). This is a substantial
subsystem — consider committing the working stack (profile/rank/Path B/playlist+lobby) before diving in.
ALT vehicle still: Private Match (but it hits the same session-create error on the dead backend).

--- earlier dead-end (kept for context): space-separated attempts ---
**entry won't commit (RESOLVED above — was the comma format):** extensive iteration via a file-driven
injector (`COD4_PLAYLIST_FILE` overrides the built-in text — fast iterate, no rebuild) + a memory probe of
the struct. Findings:
- Entry struct (in playlist record): `map[31]@+0`, `gametype[15]@+32`, `weight(int)@+48`, stride 56 at
  `+1344`; count `@+8512`; the count INCREMENTS at `loc_821E7C34` of `sub_821E6F90`, gated on `weight > 0`.
- The entry-add reads its FIRST field via an "unget + flag@1031=1" path (`sub_821CDCE0` then `sub_821CD980`)
  that captures the **entire rest of the line literally** (verified: quotes stripped, TABS preserved) — so
  `mp_shipment war 1` lands wholesale in `map@+0`, leaving gametype/weight empty.
- Tried (all → entryCount 0): `mp_shipment war 1`, `war mp_shipment 1`, tab-sep, quoted map, single token,
  3-line (map/gametype/weight), two bare lines, with/without maxparty. NONE add an entry, and NONE log a
  "Playlist error: Invalid line / Exceeded entries" (both `loc_821E7BFC` and `loc_821E7C64` call the error
  printer `sub_82233910`). So bare lines are consumed SILENTLY without becoming entries — they never reach
  the entry-add failure paths. Dispatch uses `sub_821CC128` (returns 0 = MATCH); the entry-add is reached on
  a MATCH of the last-checked token, i.e. entries are likely dispatched by matching the first token against a
  TABLE (gametype/map), not a plain fall-through. Exact entry syntax still unknown.

**RECOMMENDED next moves (pick one):**
1. **Get ground-truth format:** decompress a real CoD4 `playlists`/`playlists.info` from the `.ff` fastfiles
   (they're zlib — need a fastfile/zlib extractor; `strings` won't see it) and copy the exact entry syntax.
   Fastest path to a correct playlist.
2. **Finish the parser RE:** trace `sub_821CC128`'s table arg at the dispatch just before the entry-add
   (~line 1540 of the `sub_821E6F90` dump) to learn which table the entry's first token matches, and the
   real field order. Tools in place: `COD4_PLAYLIST_FILE` + the entry-count memory probe.
3. **Pivot to Private Match** for the actual gameplay goal (Shipment + bots + countdown + start) — it's
   local/offline and bots already play in System Link; layer the lobby/countdown UX there instead of faking
   the online Find Match path. (Less authentic, but unblocks the lobby+bots experience now.)

Then the online population/party layer (`playlist_totalonline`@0x82072218 / `playlist_population`@0x820721D0
via `sub_821EB738`) may still need faking for the search→lobby flow if staying with Find Match.

---

## 🎉 2026-06-25 — PATH B v1 SHIPPED: REAL, PERSISTED rank + unlocks (per profile)

`COD4_LIVE=1 COD4_STATS=1 [COD4_SEED=1]` writes real values into the online-stats block and persists
them, so rank/unlocks are genuine (not a per-read force) and survive across runs:
- **Block:** ctrl 0 @ guest `0x84C59D20`, stride 16924 (`[COD4MP-STATS]` in `src/cod4mp_patches.cpp`).
- **Seed (`COD4_SEED=1`, first run only):** all byte stats → 250 (unlocks rank-/challenge-gated
  weapons/perks/attachments), byte 252 → `COD4_RANK`(=54 → Lv 55 Commander), flags 260/261/263 → 1,
  RANKXP dword(2301) → `COD4_RANKXP`. Then saved.
- **Persist:** load-or-seed once on first accessor read, then autosave every 30 s to
  `<user_data_root>/415607E6/profile/<profile-name>/stats.bin` (per-profile, via the `user_profile_name`
  cvar from our profile system; override with `COD4_STATS_FILE`).
- **Verified:** seed → Rank Lv 55 Commander ("Next Rank: Prestige"), Create-a-Class weapon categories
  show **unlocked ("new")** + M16A4 selectable; relaunch with `COD4_STATS=1` alone → **loads** the file →
  rank persists. This is the proper fix vs Route-1's `COD4_UNLOCK` read-force (which the in-editor unlock
  checks bypassed because they read the block directly).

**Why this beats the read-force:** the editor's weapon/perk unlock checks read the stat block DIRECTLY,
so forcing the accessor return (`COD4_UNLOCK`) left them locked; writing the block makes the values real
everywhere. `COD4_UNLOCK` is retained as a no-persistence quick toggle.

**OPEN (the "earned from scratch" half):** untested whether an offline System-Link/bot match accrues XP
into the block. If it does, the 30 s autosave already persists earned progress — start unseeded
(`COD4_STATS=1`, no `COD4_SEED`, no file) → block stays 0 → play → autosave. If offline matches DON'T
grant XP (CoD4 only ranks up in "ranked" matches), that needs the match flagged ranked — separate work.
Next: probe the block (rank byte @+256, RANKXP dword @+3208) before/after a bot-match kill.

Run: `COD4_LIVE=1 COD4_STATS=1 COD4_SEED=1 ./cod4_mp` (first run) then `COD4_LIVE=1 COD4_STATS=1 ./cod4_mp`.

---

## 🎉 2026-06-25 — ROUTE 1 SOLVED: rank shows + Create-a-Class FULLY UNLOCKED & EDITABLE

`COD4_LIVE=1 COD4_UNLOCK=1` now shows a real rank (default **Lv 55 Commander**, `COD4_RANK=<0-based idx>`)
and **ungreys every rank-gated item — Challenges, Clan Tag, and Create a Class.** Verified end-to-end: the
full Create-a-Class editor opens (Primary Weapon / Side Arm / Special Grenade / Perk 1-3 / Rename / Save,
with live weapon/perk preview + stat bars).

**THE LEVER (found by bisecting a `COD4_FORCEALL` diagnostic over the stat accessor `sub_821A1CC8`):** the
menu rank LEVEL + rank-gating are driven by **BYTE stats** (statId < 2000), NOT RANKXP/rankForXp:
- **statId 252 = rank level** (0-based; menu displays +1, so 252=54 → "Lv 55").
- **statId 260 / 261 / 263 = validity flags** — must be non-zero or the panel falls back to Lv 1 and the
  gates stay locked. (252 alone or the flags alone do nothing; all must be set together.)
Implemented in `src/cod4mp_patches.cpp` accessor hook under `COD4_UNLOCK`: force 252→`COD4_RANK`(=54),
260/261/263→1. RANKXP(2301)/PLEVEL(2326) forcing is kept (drives the XP *number*). The `COD4_FORCEALL`
[`COD4_FORCE_LO`/`_HI`/`_VAL`] range-force diagnostic is retained for future stat bisection.

This is the **offline-stats force** (live each session via env; not persisted). Custom classes the user makes
DO persist (profile blob `0x63E83FFD` customclass1-5, via the profile system below). True XP *progression*
persistence would populate the XLiveBase stats download blob `0x00050009` (Path B, future).

Run: `COD4_LIVE=1 COD4_UNLOCK=1 [COD4_RANK=54] DISPLAY=:1 python3 tools/cod4_mp_drive.py --boot 34 \
--seq "a s4 d d a s3"` → Xbox LIVE → Create a Class.

---

## ✅ 2026-06-25 — PROFILE SYSTEM SHIPPED (skate3-modeled) + blob contents mapped

A host-side **local-profile system** (modeled on the Skate 3 recomp `mchughalex/skate3recomp`, cloned to
`/home/dlynch/dev/skate3recomp`) is implemented and verified:
- `src/cod4mp_user_settings.{h,cpp}` — `profiles.toml` store (`<user_data_root>/profiles/profiles.toml`):
  named `LocalProfile`s (id/gamertag/xuid/signin), selection, load/save. toml++ via the SDK.
- `src/cod4mp_app.h::OnFinalizePaths` — loads profiles.toml, ensures a usable profile, applies the selected
  one to the SDK profile cvars BEFORE kernel init. `COD4_PROFILE=<gamertag>` selects/creates a profile.
- SDK `rexglue-sdk/src/system/xam/user_profile.cpp` — `UserProfile` now reads `user_profile_name` /
  `user_profile_xuid` / `user_profile_signed_in` / `user_live_signed_in` / `selected_user_profile` cvars
  (mirrors the skate3 SDK fork) instead of hardcoding `name_="User"`.
- **Effect:** the per-profile content dir keys on the profile NAME, so each profile keeps its own
  title-specific save blobs. Verified: profiles.toml created; `COD4_PROFILE=DJ_Recon` switches profile;
  blobs land at `~/.local/share/cod4_mp/<TITLE=415607E6>/profile/<Name>/{63E83FFF,63E83FFD}`.

**The game WRITES + SDK PERSISTS the profile blobs** (no format-RE needed to read them — they're plaintext
`set <cvar> "<val>"` command lists):
- **`0x63E83FFF` (TITLE_SPECIFIC1)** = control/options config (gpad_buttonsConfig, snd_volume, input*…).
- **`0x63E83FFD` (TITLE_SPECIFIC3)** = `playlist`, `clanName`, `motd`, **`customclass1`..`customclass5`** (the
  custom classes — empty until created). So **custom classes persist here as cvar strings** once the UI lets
  you save one (or if we pre-seed the blob with a valid class string).

**CORRECTION to the prior "BREAKTHROUGH":** rank/XP is **NOT** in the profile blobs (they're config + class
strings). So the menu rank-LEVEL is **online-stats-bound** (the dead XLiveBase stats download
`0x00050009`), not the profile. Route-1 force of the stat-blob ACCESSOR moved only the XP *number*; the rank
LEVEL reads xp via a still-unpinned path (rankForXp gets fed 0). REMAINING route-1 lead: the rank GATE lives
in `sub_821B1BC0`/`sub_821B20C0` (per-client UI struct stride **25632** @ base **0x84C00720** + ctrl*25632;
flags/xp fields ~+25621/+25624/+25628) — find the field the gate compares and either force it or, better,
populate the stats DOWNLOAD blob (`0x00050009` in `rexglue-sdk/.../xlivebase_app.cpp`) with a real RANKXP so
the whole online-stats path reads a real rank. `COD4_PROFILE_TRACE=1` logs profile reads/writes.

---

**START HERE next session.** Project `/home/dlynch/dev/mw-recomp-mp` (recompiled Xbox 360 CoD4 MP / iw3mp).
Phases 1 (bots play) + 4 (Bot Warfare) are DONE — see `mp-multiplayer-roadmap.md` status banner. This
handoff implements **Phase 3: rank up + usable Create-a-Class custom classes** ("rank up like the old days").
Companion deep-RE: `mp-connect-bots-handoff.md` §"XBOX LIVE / CUSTOM-CLASSES PATH". Memory:
`cod4-mp-live-backend-fake`, `cod4-mp-gsc-param-block` (how to call builtins / read the GSC param block).

---

## GOAL (definition of done)

1. Boot the Live menus (`COD4_LIVE=1`), open **Barracks** and **Create-a-Class**, and the class UI is
   **ungreyed + editable** (pick weapons/perks, save a custom class).
2. The player shows a **real rank** (not rank 1), and unlocks reflect it.
3. **Persistence:** rank/XP/unlocks survive across runs (stretch within Phase 3, but the payoff).
4. Verify in a real bot match: rank-up notifications fire, and the human can spawn with a custom class.

## CURRENT STATE (the wall)

- `COD4_LIVE=1` → **signin=2 (SignedInToLive)**, Live menus render (Xbox LIVE w/ online dot, Barracks w/ a
  stats panel, Private Match). Create-a-Class is **reachable but GREYED** — it's **rank-gated**.
- Stats are faked **empty**: the Live stats download (XLiveBase msg **`0x00050009`** in
  `rexglue-sdk/src/kernel/xam/apps/xlivebase_app.cpp`) returns **success + 0 bytes** → the stat blob is all
  zero → **RANKXP = 0**.
- So the whole feature is "online-only" data bound to the dead Live backend. `COD4_LIVE` already fakes
  *enough of the backend to reach the screen*; what's left is **supplying rank/XP data** + ungreying the UI.

## ⚠️ 2026-06-25 UPDATE — Path A force-unlock DONE & MEASURED; the handoff premise is INCOMPLETE

Implemented `COD4_UNLOCK` (env-gated, default OFF) in `src/cod4mp_patches.cpp`. Hooks added + the exact
RANKXP read chain mapped and validated with a new `COD4_STATPROBE`:

- **Pair reader `sub_821A2020(ctrl, idx)`** — idx 0 = RANKXP, idx 1 = PLEVEL (name array @`0x8239D000` =
  `{"RANKXP","PLEVEL"}`). Resolves name→numeric statId via **`mp/playerStatsTable.csv`** (lookup
  `sub_821D3C68` + atoi), then reads the blob through the accessor. STATPROBE confirms it returns 0 on the
  empty Live blob. (No magic id / no CSV parse needed — we key on the pair index and self-learn the id.)
- **Accessor `sub_821A1CC8(ctrl, statId)`** — universal stat-blob read. Per-controller block stride **16924**
  @ base ~`0x84C59D20`; byte@+16920 = "initialized" flag; statId<2000 = byte stat @block+4+id;
  2000≤statId<3498 = dword stat @block+2004+(ctrl*4231+id-2000)*4. We self-learn the RANKXP/PLEVEL ids here.

**What `COD4_UNLOCK` achieves:** the Barracks **XP number** now reads high (forced 65540) ✅ — proving the
accessor force works end-to-end.

**What it does NOT achieve (the corrected wall):** the displayed **rank LEVEL** and the rank-gated greying
("Create a Class / Clan Tag / Challenges — *Unlocked at Lance Corporal [Lv 5]*") **do NOT come from the stat
blob.** Forcing RANKXP at the accessor/pair reader, forcing the playercard xp-getter **`sub_821E6848`**
(returns INT_MAX in menus when stats-valid flag @`0x85022E6E`==0), AND force-pinning **`rankForXp`
(`sub_82362DC8`)** output to 30/55 ALL leave the menu rank at **Lv 1** and items greyed. STATPROBE shows the
gate path feeds `rankForXp(xp=0)` and ignores the pinned output. So **the menu's current-rank source is a
DIFFERENT subsystem** — the Barracks summary rank is computed without `rankForXp`/`sub_821E6848` at all.

**Leading hypothesis for the real lever:** the Xbox **profile-settings title blob**
(`XamUserReadProfileSettings` → `rexglue-sdk/src/kernel/xam/xam_user.cpp`, title-specific setting), which the
Live fake leaves empty → rank defaults to 1. CoD4 360 stores rank/unlocks in the profile blob, separate from
the online stats blob. NEXT SESSION should: (1) find the exact UI function drawing the Barracks summary
"Rank Lv N" (it does NOT call `sub_82362DC8`/`sub_821E6848`) and trace its source; (2) if it's profile-based,
fake a populated title-specific profile setting in `xam_user.cpp` and re-test. Also still worth trying the
two UI dvars the original handoff named (`ui_allow_classchange` @`0x82071F00`, `onlinegame` @`0x82059658`) —
Create-a-Class may be gated on those independently of the Lv-N gates (unverified; the dvar reader tool gave
unreliable struct matches).

## ✅✅ 2026-06-25 BREAKTHROUGH — the rank/unlock source is the PROFILE blob (0x63E83FFF), runtime-confirmed

Instrumented the SDK profile read (`rexglue-sdk/src/kernel/xam/xam_user.cpp`,
`XamUserReadProfileSettingsEx`, gated `COD4_PROFILE_TRACE`). At signin CoD4 reads this id list:
`{0x10040018, 0x10040022, 0x10040003, 0x10040024, 0x63E83FFD, 0x63E83FFF}` (array @`0x8203EC40` in img).
The two that matter:

- **`0x63E83FFF` = XPROFILE_TITLE_SPECIFIC1** — CoD4's player-profile BINARY blob: **rank / XP / prestige /
  unlocks / custom classes**. Empty by default → **rank 1, classes locked.** THIS is the real lever (not the
  stat blob — see prior section; the stat blob only drives the XP *number*).
- `0x63E83FFD` = TITLE_SPECIFIC3 (secondary blob).

**The persistence plumbing ALREADY EXISTS in the SDK** (`src/system/xam/user_profile.cpp`):
`GetSetting`→`LoadSetting` reads `<user_data_root>/<TITLE_ID>/profile/User/63E83FFF` on first title access;
`XamUserWriteProfileSettings`→`AddSetting`→`SaveSetting` writes it back. So **"changing the profile from
default" = putting a valid populated `63E83FFF` blob file at that path** (no file exists yet — the game has
never written one in our offline runs). `user_data_root` = `emulator_->user_data_root()` (SDK default; the
app only sets `game_data_root` = exe/`assets`). Confirm the exact dir by triggering one write and seeing
where `63E83FFF` lands.

### THE REMAINING WORK = obtain a populated `63E83FFF` blob. Two routes:

1. **Capture-bootstrap (avoids format RE — RECOMMENDED):** find the in-memory player-data struct the menu
   reads for rank (the source `sub_821B20C0`/`sub_821B2060` read — a per-client struct stride ~25632 @
   ~`0x84C0xxxx`; rank/xp near +27436, a valid flag near +27445) and the same struct the game SERIALIZES on
   `XamUserWriteProfileSettings`. Force it high, trigger a profile write (the game serializes current state
   into the blob), let the SDK persist it, then drop the force → the persisted blob now carries the rank.
   Bonus: forcing that struct *also* gives the immediate Path-A force-unlock (menu shows rank + classes
   ungrey) without any blob-format work.
2. **Craft the blob (format RE):** reverse the CoD4 title-specific player-data layout (likely versioned +
   checksummed; a malformed blob is rejected → defaults) and write a `63E83FFF` with the desired
   rank/prestige/unlocks/classes. More work but fully deterministic + lets us pre-bake a profile.

Either way the SDK already loads/saves it, so once we have ONE valid blob, rank-up + custom classes persist
across runs. NEXT SESSION: pursue route 1 — locate the menu's player-data struct (probe the
`sub_821B20C0`/`sub_821B1BC0` reads), force rank high to confirm the menu ungreys, then trace the write
serializer to capture+persist. Use `COD4_PROFILE_TRACE=1` to watch profile reads/writes.

Hooks live behind `[COD4MP-UNLOCK]` / `[COD4MP-STATPROBE]` / `[COD4MP-PROFILE]` tags. Test:
`COD4_LIVE=1 COD4_UNLOCK=1 [COD4_RANK=N COD4_RANKXP=N] COD4_STATPROBE=1`. Screenshot the game window BY ID
(`wmctrl -l | grep cod4_mp` → `import -window <id>`), NOT `-window root` (root grabs the desktop). Driver:
patched copy of `tools/cod4_mp_drive.py` with a window-id `shot()` (see scratchpad `drive_wid.py`).

## THE GATING CHAIN (what to flip, in order)

The "unlocked at rank N" LOGIC lives in **GSC fastfile scripts** (`maps/mp/gametypes/_rank.gsc` / `_class.gsc`)
which read the **RANKXP stat**. So the lever is the stat value, plus two UI dvars:

1. **`onlinegame`** dvar (desc: "online game with stats, custom classes, unlocks") **@0x82059658**, registered
   in `sub_822CD280`. The Live cfg (`default_xboxlive.cfg` @0x820728D0, run on the Live path) already
   `set onlinegame 1` — **verify it's 1 under COD4_LIVE**; if not, force it.
2. **`ui_allow_classchange`** **@0x82071F00** = 1 (and `ui_customClassName` @0x82071918), set in
   `sub_821EF0F0` (.11:46698). Force to 1 if the greyed state is from this.
3. **RANKXP stat @0x8206A524** — push HIGH. The menu + GSC read it via the stat blob (accessor
   **`sub_821A1CC8`**, statGet handler **`sub_821A2090`** .9:2834). `rankTable.csv` @0x8205AEE0, loader
   **`sub_822B5E48`** (.18:574) maps XP→rank. This is the primary lever.

## STRATEGY — two paths (do A first for the quick win, then B for persistence)

### Path A — FORCE-UNLOCK (fast; proves the chain; no blob-format RE)

Get Create-a-Class to ungrey + show max rank immediately, by making the game *read* a high RANKXP:

- **Lever 1 (cleanest): hook `statGet` `sub_821A2090`** — when it's asked for the **RANKXP** stat id, return a
  high value (e.g. enough XP for max rank / prestige). One hook, no blob layout needed. RE which arg is the
  stat id (likely r3/r4 = stat index) and which return path carries the value; compare against the RANKXP
  id. This makes BOTH the Barracks menu and the in-match `_rank.gsc` see max rank → classes unlock.
- **Lever 2 (alt): call the `setRank` builtin `sub_8227C830`** via the call-the-handler trick (same pattern
  as addtestclient / our botmovement builtins — see `cod4-mp-gsc-param-block` for the GSC param-block ABI:
  self entity = r3>>16, params read direct from the VM block @0x82E399A8). Good for the in-match player; may
  NOT move the *menu* stat (the menu reads the blob directly) — so Lever 1 is preferred for ungreying the UI.
- **Lever 3 (UI only): force `ui_allow_classchange=1` + `onlinegame=1`** in `src/cod4mp_patches.cpp` (poke the
  dvar values like the maxclients hook does) in case the grey is partly a UI-state gate, not just rank.

**First experiment (make-or-break):** under `COD4_LIVE=1`, hook `statGet` to return a big RANKXP, force the
two UI dvars, navigate to Create-a-Class, and confirm it ungreys + you can edit a class. If it ungreys, the
chain is understood and the rest is polish + persistence.

### Path B — REAL PERSISTENCE (the "proper" rank-up; do after A works)

Make rank/XP/unlocks survive across sessions by giving the Live stats path a real blob:

1. **Download:** in `xlivebase_app.cpp` msg `0x00050009`, instead of success+0-bytes, return a **stored stat
   blob** read from a local file (e.g. `~/.cod4mp/stats.bin` or next to the exe). On first run, synthesize a
   blob with the desired starting RANKXP.
2. **Upload counterpart:** implement **XStorageUploadFromMemory** (the write-back message) to persist the
   blob the game pushes after a match → XP/rank/unlocks accrue and survive.
3. **Blob format is the RE work:** find the RANKXP offset within the blob (use the statGet accessor
   `sub_821A1CC8` / `sub_821A2090` to learn the blob layout — which byte range backs the RANKXP stat id).
   Once you can read/write RANKXP in the blob, the rest of the stats follow the same indexing.

Knock-on: with a real blob, **rank-up notifications fire in matches** and Create-a-Class ungreys *for real*
(no force needed). Path A's hooks can then be removed or kept as a "god mode" toggle.

## RECOMMENDED ORDER

1. Confirm `onlinegame` is 1 under COD4_LIVE (BOTLOG-style getdvarint probe, or read the dvar). Force if not.
2. **Path A Lever 1** (statGet RANKXP hook) + force `ui_allow_classchange` → get Create-a-Class to UNGREY.
   This is the gate; everything else is downstream. Gate it behind a new env (e.g. `COD4_UNLOCK`).
3. Build a custom class in the UI; verify it's selectable at Choose-Class in a System Link bot match.
4. **Path B**: stat blob download/upload + local file → persistence + real rank-up.
5. Verify end-to-end in a 6v6 bot match (rank shown, custom class usable, XP persists across a relaunch).

## RE REFERENCE POINTS (all confirmed in prior RE; see mp-connect-bots-handoff.md)

| Thing | Address / fn |
|---|---|
| Live stats download msg | `0x00050009` (xlivebase_app.cpp DispatchMessageSync) |
| RANKXP stat | @0x8206A524 |
| statGet handler / accessor | `sub_821A2090` (.9:2834) / `sub_821A1CC8` |
| setRank builtin | `sub_8227C830` (call-the-handler trick) |
| rankTable.csv + loader | @0x8205AEE0 / `sub_822B5E48` (.18:574) |
| `onlinegame` dvar / reg | @0x82059658 / `sub_822CD280` (.18:54497) |
| `ui_allow_classchange` / `ui_customClassName` | @0x82071F00 / @0x82071918 (`sub_821EF0F0` .11:46698) |
| Live cfg (sets onlinegame=1) | `default_xboxlive.cfg` @0x820728D0 |
| signin_state (=2 under COD4_LIVE) | `rexglue-sdk/include/rex/system/xam/user_profile.h:~221` |

## HOW TO TEST (Live menu path)

```
COD4_LIVE=1 [COD4_UNLOCK=1] DISPLAY=:1 python3 tools/cod4_mp_drive.py --boot 32 --seq "a s6"
# navigate: Xbox LIVE -> Barracks (rank/stats panel) and Create-a-Class (greyed vs editable).
# Screenshot the game window by id (not -window root). pkill -9 + rm /dev/shm after.
```
GSC builtin / param-block ABI for calling setRank or reading stat ids: see `cod4-mp-gsc-param-block`
(param block @0x82E399A8: +0x1c numParams, +0x10 top, entry=top-i*8, +4 type[6=int/4=vec], +0 value).
Disassembly: base 0x82000000, img end 0x853D0000, `tools/img.bin`, capstone PPC32 BE.

## RISKS / NOTES

- Custom classes are fundamentally an **online feature on a dead backend** — expect to fake more of the Live
  stats/session layer than just the download; watch for crashes in adjacent XLiveBase messages (trace with
  `COD4_LIVE_TRACE=1`). The prior dig called this "a real multi-step project," but the *force-unlock* (Path A)
  should be a quick win that decouples the UI-unlock from the full persistence build.
- SDK-side changes go in `rexglue-sdk` (branch `testing/five-stability-fixes`); game-side hooks in
  `src/cod4mp_patches.cpp` (`[COD4MP-*]` tags). Rebuild SDK (install) if you touch it, then the game.
- Keep everything env-gated (`COD4_LIVE` / a new `COD4_UNLOCK`) so the default build is unchanged.
