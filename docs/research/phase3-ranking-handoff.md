# Phase 3 — Ranking / XP + Custom Classes — IMPLEMENTATION HANDOFF

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
  zero → **RANKXP = 0 → rank 1 → classes locked**.
- So the whole feature is "online-only" data bound to the dead Live backend. `COD4_LIVE` already fakes
  *enough of the backend to reach the screen*; what's left is **supplying rank/XP data** + ungreying the UI.

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
