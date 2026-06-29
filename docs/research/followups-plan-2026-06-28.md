# Follow-up polish plan — post two-client-VPN milestone (2026-06-28)

Three open items after two real machines + bots reached one Live match. Each was RE'd by a
focused investigation; this is the consolidated implementation plan. None are blockers — the
headline (host + friend + bots in a Live match over VPN, unlocks, prestige) works.

Priority order (effort/value): **1) lobby rank** (tiny, code) → **2) first-join double-queue**
(medium, code) → **3) CaC preview assets** (mostly data/FS, deeper).

---

## 1. Lobby rank shows "level 42" while Barracks shows "Lv 55"  — SMALL, code-only

**Root cause.** Two display surfaces read rank from different sources that we seeded
inconsistently:
- **Barracks** reads **byte stat 252** directly (we seed `=54` → "Lv 55"). Validity flags
  bytes 260/261/263 must be 1 (we set them).
- **Lobby/scoreboard** derives level from **XP**: per member it calls the accessor for
  **RANKXP (dword id 2301)** then `rankForXp`. We seed `RANKXP=65540`, and in this build's
  `mp/rankTable.csv` `rankForXp(65540) = 41` → "level 42". 65540 is NOT level-55 XP here.

**Key functions** (verified in `generated/`):
- Lobby member-row builder **`sub_822B76D0`** (`cod4_mp_recomp.18.cpp:4184`) → accessor
  `sub_821A1CC8(RANKXP)` (`:4615`) → **`rankForXp = sub_82362DC8`** (`:4618`).
- `rankForXp` (`cod4_mp_recomp.23.cpp:48466`) binary-searches `mp/rankTable.csv` (filename VA
  `0x8205AEE0`) — thresholds live in the fastfile, **not** in the static image, so they must be
  found at runtime.
- Existing hooks: `REX_FUNC(sub_82362DC8)` `src/cod4mp_patches.cpp:1295`; accessor
  `REX_FUNC(sub_821A1CC8)` `:1227`; `seedStatBlock()` `:1146`; `forcedRankxp()` `:1101`.

**Fix — seed RANKXP to the real level-55 threshold** (persisted + internally consistent; do NOT
hook `rankForXp` output — the lobby loops over ALL members so a forced output would stamp your
rank onto every player/bot, and the existing hook has a latent default bug: `COD4_RANK` default
55 at `:1303` vs `forcedRank()` 54 at `:1111`).

Steps:
1. **Find the threshold at runtime** — add a one-shot self-scan in the `rankForXp` hook gated by
   `COD4_RANKSCAN` (copy `ctx`, call `__imp__sub_82362DC8` over increasing XP until output ≥ 54,
   log the XP). Likely ≈ **516000** (stock CoD4 rank-55), but confirm by scan, don't hardcode blind.
2. **Bake it** — set `forcedRankxp()` default (`:1104`) to that value (replaces 65540).
   `seedStatBlock` already does `setDword(2301, forcedRankxp())`, so re-seed (delete the per-profile
   `stats.bin`, run `COD4_SEED=1`) writes the consistent value. Byte 252 stays 54.
3. **(Optional) migrate already-persisted blobs** — in `statsTick` (`:1166`), after load, if
   `dword(2301) < threshold && byte(252) >= 54`, rewrite RANKXP and resave (repairs old `stats.bin`
   without a manual reseed).

**Verify:** `COD4_STATPROBE` log shows `rankForXp(xp=N) -> rank=54`; lobby scoreboard shows
"10th prestige, level 55" matching Barracks.

**Effort:** ~1 small hook + 1 scan run. **Risk:** trivial.

---

## 2. First-join double-queue  — MEDIUM, code

**Root cause.** The host only goes live in *reaction* to the joiner, and that transition *ejects*
the queue-1 joiner:
- The host sits in a pre-game party lobby; its auto-start countdown re-arms forever without a 2nd
  member (the lobby readiness handshake can't be faked — proven dead even under clean netns).
- Queue-1 joiner lands in the *lobby* (`0joinParty`/`0memberJoin`). That presence lets the host
  reach match-start; `COD4_MM_ARBEMPTY` then returns empty arbitration → host **aborts → `XSessionStart`**
  → spawns a solo+bots match, which closes the lobby the joiner is in ("Game lobby closed").
- Queue-2 catches the host in its brief post-abort-start "party still open" window and reaches a
  reserved SV slot → st3→st4 ACTIVE. A fully-settled in-match host instead replies `partyFull`.

**Recommended: (a) HOST-GOES-LIVE-FIRST.** (Lobby-carry / option (b) is conclusively dead.)

**Phase 1 — drive the host live solo+bots up front (low risk, reuses existing levers).**
- `COD4_MM_FORCEGO` (`src/cod4mp_patches.cpp:765-810`) already drives the start orchestrator
  (`sub_82207F48`→`sub_822079D8`) and can clear the start-gate dvar `*(0x82A9E034)+0xc`, but it's
  gated `members >= 2` (`:777`) — that's the joiner coupling. Add `COD4_MM_HOSTLIVE` (or
  `COD4_MM_FORCEGO_MIN=1`) to fire the same path on `members >= 1` after a settle.
- Keep `COD4_MM_ARBEMPTY` (host pre-publishes its own arb at create `xgi_app.cpp:991-1006`, so solo
  `registrants>=1`), bots (`GSCINJECT`/`BOTSPAWN`/`BOTAI`), `COD4_MM_BOTRESERVE=2` (open SV slots),
  `COD4_MM_NODROP`. Reuse host-fallback gate `sub_821AC9A0→1` (`:376`), Com_Frame connect-window
  clear `sub_822367B8` (`:564`), `ui_partyFull` suppress `sub_822B31B0` (`:471`).
- Result: a **live bot match with reserved open slots before any joiner searches**; the broker
  already advertises it (`BrokerPublishHost` `xgi_app.cpp:207`, heartbeat `:768`).

**Phase 2 — keep the live host accepting join-in-progress (the genuinely new gate).** Pick:
- **Route A (clean, preferred):** RE + flip the in-match party-join-accept gate so the host accepts
  the joiner's `0memberJoin` while fully in-match instead of `partyFull`. Lead: party `partyFull`
  wire sender, candidate **`sub_822BC400`** (matchmaking-mock360-handoff.md:149). Method: with the
  host live (Phase 1), have the joiner do a fresh search+join and gdb HW-watchpoint which fn emits
  `partyFull` to the inbound `0memberJoin` (party member array `@0x8246C480` stride 0xC0,
  `party_maxplayers` `@0x8243BDD8`). Hook it (`COD4_MM_OPENJOIN`) → accept + route to a reserved SV
  slot. Makes the live host permanently joinable → queue-2 becomes queue-1.
- **Route B (pragmatic, ships now):** joiner-side **auto-re-queue** — a Com_Frame
  (`sub_822367B8`) hook on the joiner that detects "lobby closed" (`menu_xboxlive_lobbyended`) and
  auto re-issues Find Match once. Internal double-queue still happens but the **user queues once**.
  Safe fallback if `sub_822BC400` proves fastfile/uiscript-driven and hard to reach from exe hooks.

**Recommendation:** ship **Route B** for a one-queue UX immediately; pursue **Route A** for the
clean permanent fix. They stack.

**Verify:** host alone → SVPROBE shows live `[0:st4/net][1..N:st4/bot]` with free slots and NO
joiner; joiner single Find Match → reaches st4 ACTIVE, no "lobby closed". Timing test: joiner
searches 30s+ after go-live to see if the open window is transient (decides A mandatory vs B enough).

**Effort:** Phase 1 small (reuses FORCEGO). Phase 2 Route B small; Route A = a gdb RE session.
**Risk:** Phase 1 makes the joiner ALWAYS do join-in-progress (proven but the harder path); main
risk is the party closing before queue-1 → mitigated by Route A/B.

---

## 3. Create-a-Class preview: teal checkerboard art + `UNLOCALIZED(PERKS_FRAG_1)`  — DATA/FS, deeper

**Root cause (both are GAME-side engine fallbacks, NOT the SDK, and PRE-EXISTING — unrelated to
the stats work; M40A3 + frag are base content).**
- **Checkerboard** = IW3 engine's `$default`/"missing material" placeholder (the SDK's own
  missing-texture fallback is *transparent* `(0,0,0,0)`, not a checker —
  `rexglue-sdk/src/graphics/vulkan/texture_cache.cpp:141,3106`; no checker pattern anywhere in
  `src/graphics/`). It fires because the preview material's image isn't resident in any loaded zone.
  At the menu only the UI/common zones load (`cod4mp_patches.cpp:433`); the weapon/perk preview
  pixels live in the **streamed high-mip store** `/home/dlynch/Games/cod4/highmip/*.hi` (2480 files)
  + `packfile1.pak` (830 MB), which **neither repo references/mounts** (grep finds no
  `highmip`/`.hi`/`packfile`/`.pak`). So those images get no pixels → default material.
- **`UNLOCALIZED(...)`** = IW3 `SEH_StringEd_GetString` miss format (SDK never emits it). The
  **English localized fastfiles are absent** from the game dir (`find … -iname "*localized*"` →
  nothing; only `common_mp.ff`/`ui_mp.ff`/`code_post_gfx_mp.ff` present). Perk/weapon display-name
  keys (`PERKS_FRAG_1`) live in the missing `localized_english_*.ff`. The game requests English
  (`XGetLanguage_entry` hardcodes kEnglish, `rexglue-sdk/src/kernel/xam/xam_info.cpp:185`); a missing
  file resolves silently to null (`host_path_device.cpp:54-73`) so there's no error in the log.

**Plan (mostly data + filesystem wiring; likely no game-recomp patch):**
1. **Step 0 — file-open trace** (highest value): instrument
   `rexglue-sdk/src/filesystem/devices/host_path_device.cpp:54` (`ResolvePath`/`OpenFile`) to log
   every guest path + hit/miss. Boot to Create-a-Class → it names exactly which `.ff`/`.hi`/`.pak`
   miss.
2. **Strings:** supply `localized_english_*.ff` (extract from the CoD4 360 disc/region) into
   `/home/dlynch/Games/cod4` → `SEH_StringEd_GetString("PERKS_FRAG_1")` resolves, no SDK change. If
   the FF exists but isn't requested, fix the language→FF-name mapping (`xam_info.cpp:185`,
   `xam_locale.cpp:96`). If it loads but the key's missing, dump the zone with the existing
   `COD4_FFDUMP_FILE` tool (`cod4mp_patches.cpp:438`) and grep.
3. **Materials:** from the trace, if the image loader requests the streamed store, mount
   `highmip/*.hi` + `packfile1.pak` into the SDK filesystem (host-path device / game-data root
   mapping). Note: an undecodable *format* would show transparent/black, not a checker — so this is
   an asset-**residency** miss, not a format miss; don't chase `texture_cache.cpp` format handling
   unless the trace shows pixels actually arriving.

**Verify:** with trace on, confirm the missing `localized_english_*.ff` + `*.hi`/`*.pak` requests;
after supplying/mounting, the grenade shows its real name and the weapon/perk previews render real
art. Additive data/FS only → no gameplay-path risk.

**Effort:** medium (needs the disc's localized fastfiles + high-mip store wiring). **Risk:** low
(additive). **Note:** the two symptoms likely share one upstream cause — assets outside the
always-resident UI/common zones aren't present/mounted.

---

### Cross-cutting reminders
- Two machines MUST have **distinct `profiles.toml` `xuid`+`gamertag`** (the party rejects duplicate
  XUIDs — the bug that blocked the whole VPN join). Already in BUILD_WINDOWS.md §7.
- `COD4_SEED` only seeds when no `stats.bin` exists; delete a stale one before re-seeding. (Could add
  a force-reseed / seed-version bump so this isn't a trap.)
