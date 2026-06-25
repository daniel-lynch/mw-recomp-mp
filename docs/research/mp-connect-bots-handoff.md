# CoD4 MP (iw3mp) — Handoff: local-match connect wall + bots, two parallel tracks

**Date:** 2026-06-23  **Project:** `/home/dlynch/dev/mw-recomp-mp` (sibling of `mw-recomp`, reuses
the PREBUILT installed `rexglue-sdk`).  **Companion memory:** `memory/cod4-mp-state.md`,
`memory/cod4-mp-bots-feasibility.md`, `memory/cod4-mp-plan.md`.

End goal: play MP with a friend + BOTS + a promod-style ruleset. This handoff covers the immediate
blocker (getting a human into a local match) and the bots path, set up so they can be worked **in
parallel** (e.g. two subagents / two sessions).

---

## STATUS — where we are

### 🧱 XBOX LIVE DIG 2026-06-24 PM8 — hit the dead-server XStorage wall (custom classes = major project)
Pursued the Live path to reach Create-a-Class. APPLIED (kept, harmless at signin=1):
- **SDK `xam_content_device.cpp` `XamContentGetDeviceState`**: default `device_id==0` → HDD (matches
  `XamContentGetDeviceData`). Fixes "no storage device" → got the Live menu PAST "Downloading game
  settings". KEEP.
- **`generated/cod4_mp_recomp.9.cpp` `sub_821A2720` `[COD4MP-NETGATE D]`**: force-skip the fatal MOTD
  storage-path failure (~line 3940). Dormant at signin=1.
- Decrypt tools persisted to `tools/`: `xexdec.py`, `rd.py`, `allstr.py`, `mp_image.bin` (flat image,
  file_off = VA−0x82000000). Invaluable for reading any VA without the game running.
THE WALL: Custom classes need **signin=2 (SignedInToLive)** (set in `user_profile.h:221`, currently
reverted to 1 for a usable build). At 2, the Live init builds storage paths for motd → playlist →
game-settings via **`sub_8210AC38`** → import **`sub_82365AF4` = XStorage** (Xbox Live ONLINE content
storage), which is **dead (servers gone) AND unimplemented in the SDK** — each file returns err 1627
and fatal-Sys_Errors at boot. Faking it = dummy paths + empty content for every Live-storage file +
not crashing downstream, then likely auth/session/stats layers behind it. **It's a real multi-step
project; custom classes are an online-only feature fundamentally bound to the dead Live backend.**
The "must be signed in to Xbox LIVE" notices (`XBOXLIVE_NOTSIGNEDIN`@0x82072194,
`SIGNEDOUTOFLIVE`@0x8206A5C8) are `==2` gates — they gate Barracks/Create-a-Class too, so swallowing
them isn't enough (the gate returns instead of proceeding). signin=2 (full XStorage fake) is the
real path. Realistic playable experience = **System Link 6v6 with PRESET classes** (how CoD4 LAN
always worked); custom classes are a rainy-day deep-dive.

### 🔭 XBOX LIVE / CUSTOM-CLASSES PATH (research 2026-06-23 PM7, subagent + XEX decrypt) — not yet applied
- **The "Xbox LIVE" menu item is ALREADY reachable today.** Its sign-in gate `sub_822C5CB8`
  (cod4_mp_recomp.18.cpp:37283) accepts signin state **> 0**, and the SDK reports 1 (SignedInLocally),
  so it passes (else shows `XBOXLIVE_MUSTLOGIN`@0x8205A290). The Live item runs `set systemlink 0;
  set splitscreen 0; set onlinegame 1; exec default_xboxlive.cfg` (@0x820728D0) → opens `menu_xboxlive`
  (@0x82072924) via uiScript executor `sub_821ED6F8` (.11:42890).
- **To satisfy true "SignedInToLive" (19 game-side `==2` gates for online features):** one-line SDK
  edit — `rexglue-sdk/include/rex/system/xam/user_profile.h:221` `return 1;` → `return 2;` (propagates
  to `XamUserGetSigninState`/`...Info`, `xam_user.cpp:69`/`:90`). Lowest effort; analog of the existing
  privilege/link grants. If a residual notice appears swallow it at `sub_82234308` (same sink) for the
  relevant VA (e.g. `XBOXLIVE_SIGNEDOUTOFLIVE`@0x8206A5C8). Per-user Live flag global: 0x824308A8;
  poller `sub_821B03F0` (.9:36174). Cached dvars `xblive_loggedin`@0x8207AF74, `xblive_rankedmatch`.
- **Custom classes / create-a-class:** gated by dvar **`onlinegame`** (desc literally "online game with
  stats, custom classes, unlocks" @0x82059658, registered in `sub_822CD280` .18:54497) — the Live cfg
  sets it to 1. Class UI dvars `ui_allow_classchange`@0x82071F00 / `ui_customClassName`@0x82071918 in
  `sub_821EF0F0` (.11:46698). The actual "unlocked at rank N" LOGIC is in GSC fastfile scripts
  (`maps/mp/gametypes/_rank.gsc`/`_class.gsc`), reading the **RANKXP stat** (@0x8206A524; rankTable.csv
  @0x8205AEE0 loader `sub_822B5E48` .18:574). **To unlock all classes locally:** set onlinegame=1 +
  ui_allow_classchange=1, and push RANKXP high — either via the stat blob (accessor `sub_821A1CC8`,
  statGet handler `sub_821A2090` .9:2834) or by calling the **setRank builtin `sub_8227C830`**
  (.16:7855) directly (same call-the-handler trick as addtestclient `sub_82263110`). Real matchmaking
  is dead, so stop at the local Barracks/Create-a-Class screens (fully local UI + stat reads).
- TOOLS the subagent left in scratchpad (worth persisting to `tools/`): `xexdec.py` (decrypt
  default_mp.xex → flat image, retail key 20B185A59D28FDC340583FBB0896BF91), `rd.py VA` (read string at
  guest VA), `allstr.py needle` (string→VA). file_offset = VA − 0x82000000. Makes ALL data VAs readable.

### ✅✅✅ MILESTONE 2026-06-23 PM6 — SYSTEM LINK 12-PLAYER HOST WORKS (bots connect, but spectate)
- **System Link → Create Game → Start now hosts a real listen server** (no splitscreen 4-cap, no
  party-shutdown). `XSessionJoinRemote()` adds all clients to the session. With `COD4_ADDBOTS=11` →
  **12 clients connected** (1 human + 11 bots). This is the path for 6v6 / friend-connect / Live.
- **How it was unblocked (Live/net gates):**
  - SDK `rexglue-sdk/src/kernel/xam/xam_net.cpp`: `XNetGetEthernetLinkStatus` returns ACTIVE
    (0x0B); `XNetGetTitleXnAddr` returns LAN IP 192.168.1.117 + full status 0x66
    (ETHERNET|STATIC|GATEWAY|DNS). `xam_user.cpp`: `XamUserCheckPrivilege` GRANTS all (was hard-deny).
  - **The Start "active network connection" notice** = localized `@XBOXLIVE_NETCONNECTION`
    (VA 0x8206A5B0), shown via notice opener `sub_82234308`. Live gate `sub_821AD5A8`
    (cod4_mp_recomp.9.cpp) checks `XNetGetEthernetLinkStatus`; but Start uses a CACHED path.
    **BYPASS applied (found by subagent, verified via XEX decrypt):** (A) force the "connected" branch
    in sub_821AD5A8 (`goto loc_821AD608` at .9.cpp ~29533, prevents teardown+notice); (C) swallow the
    notice at the sink — `sub_82234308` early-returns when `ctx.r4.u32==0x8206A5B0` (.13.cpp:52828).
- **OPEN — bots spectate, don't play.** Raw `addtestclient` (handler sub_82263110) adds connected
  SPECTATORS; it doesn't team-assign. To make them PLAY they need `menuresponse team_marinesopfor
  autoassign` (+ a class) like a human does.
  **TODO #2 (NOTED FOR LATER — the team-join, do this next):** preferred path = make the stock CoD4
  GSC `_bot` loop spawn+auto-join them by reporting **`scr_testclients`=N**. The GSC reads it via the
  `getdvarint` builtin (name string @0x82065678). `scr_testclients` has NO guest-addressable literal
  (only in host GSC pool >4GB), so: hook the getdvarint builtin / Dvar lookup to return N when the
  resolved name == "scr_testclients", then the GSC auto-spawns+joins N test clients (remove the
  current addtestclient hack). Alt path = find `SV_ExecuteClientCommand` (the client-cmd processor;
  buried under the registrar sub_8235E5B0 which registers menuresponse@0x8206A0A0 / changeclass@
  0x820746E4) and call it per-bot with the menuresponse string. Team strings: autoassign@0x82BB73C4,
  team_marinesopfor@0x82BC8E9C.

### ✅✅ SOLVED 2026-06-23 PM5 — local match now loads to CHOOSE TEAM (Backlot/TDM)
**Single root cause for the whole "Awaiting challenge" saga: a rexglue jump-table mis-detection.**
`sub_8220A068` is the **GSC/menu DFA lexer** (MP twin of SP `sub_821AE3D8`, same 2645-line body — NOT
"Com_sprintf" as earlier guessed). Its token-type dispatch `bctr` at guest **0x8220A2D0** is a jump
table indexed by **r10** (`cmplwi r10,98; rlwinm r0,r10,2; lwzx r0,r12,r0`), but rexglue's
`detectJumpTable` mis-traced the index as **r7** (the load reuses r0 as both dest & scaled index) and
emitted `switch (ctx.r7.u32)`. Wrong index → wrong token handler → empty/-1 token → GSC
**"script compile error: bad syntax"** (aborted Game Init) AND a degenerate **negative length** into
the token-text copy `sub_82209F20` → the **infinite-loop hang**. ONE fix kills both.
- **THE FIX (permanent, in `config/functions_mp.toml`):** a `[[switch_tables]]` override pinning
  `address=0x8220A2D0, register=10, labels=[…99 targets…]`. Re-ran `rexglue codegen
  cod4_mp_manifest.toml` + rebuilt → dispatch now `switch (ctx.r10.u32)`. **This is the identical bug +
  fix the SP project already hit** (`mw-recomp/config/functions.toml`, `sub_821AE3D8` @ 0x821AE640).
  Found via Option-2 (SP comparison) — the SP `functions.toml` comment literally described it.
- **Result:** Split Screen → Start Match now: Game Init completes → `Going from CS_FREE to CS_CONNECTED`
  → stats packets → `CA_LOADING` → `LOADING… maps/mp/mp_backlot.d3dbsp` → **CHOOSE TEAM screen**
  (OpFor/Marines/Auto-Assign). Per-client connstate advances 0→5→8→9. `[COD4MP-FIX1]` clamp no longer
  fires. The old TRACK A (connect handshake) is RESOLVED by this — it was never a net-layer issue.
- **Cleanup TODO:** the diagnostic hooks in `src/cod4mp_patches.cpp` (`[COD4MP-NET2]`, `[COD4MP-CONN]`,
  `[COD4MP-FIX1]` now-redundant clamp, `[COD4MP-CONSOLE]`, `[COD4MP-GSCERR]`, `[COD4MP-GSCLOAD]`) can be
  stripped — the real fix is the config override. `[COD4MP-CONSOLE]` (Com_Printf echo, sub_82234CB8) is
  worth keeping handy: the in-game console sink sub_822DDCF0 is gated off, so this is the only console.
- **PLAYABLE + BOTS ✅ (same session):** Choose Team (Auto-Assign) → Choose Class (Grenadier) → **spawn
  into first-person on Backlot** (M16A4, HUD, minimap, match timer; connstate 0→4→5→8→9=CA_ACTIVE).
  **Bots working** via `[COD4MP-BOTS]` in `src/cod4mp_patches.cpp`: the `addtestclient` command handler
  is `sub_82263110` (= SV_AddTestClient_f; cmd_function_t node at guest `0x823A3260` = {next=0,
  name="addtestclient"@0x82064E84, fn=0x82263110}; it reads NO Cmd args, adds 1 test client/call).
  No console/Cbuf is wired, so the hook **calls sub_82263110 directly N times** once cl0 is CA_ACTIVE,
  N from env **`COD4_ADDBOTS`**. `COD4_ADDBOTS=3` → 3 bots: server logs **4× `CS_FREE→CS_CONNECTED`**
  (1 human + 3 bots = full sv_maxclients=4). How I found the handler: searched live shm for a BE pointer
  to the "addtestclient" name VA (0x82064E84) → the cmd node → its fn ptr.
- **Next: a 2nd HUMAN (friend) connect** (split-screen 2nd pad or networked), bot AI/teams polish, and a
  promod-style ruleset. The Cbuf/console is still unwired — finding it would allow `map_restart`,
  `scr_testclients`, etc. the normal way (currently bypassed by the direct-call hook).
- New tool `tools/cod4_mp_xref.py` (reliable lis+addi/ori VA xref) was key to confirming identities.



- iw3mp **boots to the MP main menu** ✅ and **Split Screen loads a local match that renders in 3D**
  ✅ (Team Deathmatch / Backlot fully drawn) — but it **hangs on "Awaiting challenge…1."**

### MILESTONE 2026-06-23 PM — hang FIXED; real blocker is a GSC "script compile error: bad syntax"
- **The "Awaiting challenge" hang is GONE.** Added guard `[COD4MP-FIX1]` in `src/cod4mp_patches.cpp`:
  it overrides `sub_82209F20` and clamps a NEGATIVE guest `r4` (copy length) to 0, routing the fn into
  its clean empty-copy path. With it, Start Match returns to `Com_Frame` — the per-frame connect pump
  `sub_822CAC00` went from frozen-at-#6050 to **#14200+ and actively ticking**. (This is a real,
  safe fix, not just diagnostic: a copy length is never legitimately negative; `r4 = speclen − 2`
  underflows to −1 only for a degenerate `%`-spec. KEEP it.)
- **But Start Match now ends on an in-game `Error: script compile error / bad syntax / (see console
  for details)` screen** (confirmed by user screenshot, build 1.0.472). So the hang was *masking* a
  real **GSC compile failure**: the chain we were stuck in (`…→ sub_8220B328 → sub_8220A068 →
  sub_82209F20`) is the GSC compiler's **error-reporter** building `^1Error: bad syntax` in scratch
  region `0x7015xxxx`; the negative-length copy was formatting that error's detail string. The compile
  fails FIRST, then the reporter hung. The clamp does NOT cause the syntax error (it happens before).
- Evidence: live-shm strings around `0x70157E00` show `^1Error: bad syntax`, `script compile error`,
  `(see console for details)`, plus GSC asset names everywhere (`maps/mp/gametypes/_*`, `codescripts/*`,
  `maps/mp/_*`). The release SDK doesn't capture the game's own console, so the "(see console for
  details)" line (which script + line#) is not in stderr — must be obtained by hooking the GSC
  compile-error reporter or reading the parser's current-script global.
- Connect state is still 0 / clock still 1 / `getchallenge` still silent simply because the match
  **aborts at the script error** before it ever gets to connect. Fix the GSC compile, then Track A resumes.

**NEXT (new top priority): find which GSC script fails to compile and why.** Progress this session:
- The failure fires at the VERY START of Game Init: console order is `------- Game Initialization
  -------` → `gamename: Call of Duty 4` → `gamedate: Sep 7 2007` → (FIX1 clamp / error banner) →
  `Server Shutdown`. So `GScr_LoadScripts`/`SV_InitGameVM` compiles the gametype scripts (TDM /
  mp_backlot) and the FIRST/early compile fails `bad syntax`, aborting the match. GSC source IS loaded
  (live shm has `maps/mp/mp_backlot.gsc main()`, `scripts/utility.gsc …`, `codescripts/character_mp.gsc
  …`), and `bad syntax` is one entry in the GSC compiler's error-template table (`Error: Expected N
  params…`, `Error: bad escape character…`, `Error: bad syntax`, …) — so it's a REAL parse error, not
  empty source. Likely a **recomp GSC-compiler/parser bug** mis-parsing a valid script.
- Hooked `Com_Printf = sub_82234CB8` (`[COD4MP-CONSOLE]`) — captures the whole boot/init console
  (note: the console sink `sub_822DDCF0` is gated off in this build, so this hook is the only way to
  see it; guest fmt strings embed `\n`, so each call's varargs land on the NEXT physical log line).
  The GSC compile-error DETAIL does NOT go through Com_Printf.
- The real compile-error reporter chain (from the live error stack) is `sub_82271428 → sub_82263760`
  (`[COD4MP-GSCERR]` hooks them). They take a **codePos**, not a filename: at the failure
  `sub_82271428 r4=0x670C` (=26380, the source byte-offset of the syntax error); `sub_82263760
  r3=0x829196B8 r6=0x829B4088` (pointers into compiled-bytecode data, not text).
- **Reporter machinery fully mapped (session PM3):** Start Match `sub_82200598` compiles scripts
  (GScr_LoadScripts region ~calls #36-40: `sub_821FF738`/`sub_82235550`/`sub_821FFD10`/`sub_821FFC90`
  — these are buffer/alloc fns taking handles+sizes, NOT filenames) then reports via
  `sub_822030A0 → sub_82271428(CompileError, 739 lines) → sub_82263760(msg assembler)`.
  - The error codePos = `sub_82103768()` = `*(*(0x82000764)+16)`; the struct is at guest `0x3000A000`,
    only +16 populated (≈ **0x6703**, deterministic across runs). codePos is a byte-offset, NOT a
    pointer, so it must be resolved via the source-buffer table.
  - The empty error-format was a NULL table lookup: `sub_822030A0` builds the CompileError fmt as
    `*(0x82F82D84)` which is **0** (null) → the format processing on a null string underflowed
    (this is the path FIX1 guards). So the "detail" message template itself is missing/0 in this build.
- **STILL OPEN — the literal failing .gsc filename.** codePos `0x6703` needs resolving via the GSC
  `sourceBufferLookup` (NOT in the obvious Scr globals at 0x82000740-0x820007B0, which are XAsset
  handles; the `0x3000A000`/`0x30008000`="default.xex" heap holds the codePos struct + xex name only).
  Reliable next approaches: (a) find `Scr_GetSourceBuffer(codePos)` (the resolver `sub_82271428` uses
  internally to print "file X line N") and read its table; (b) hook the per-file lexer/file-open that
  sets the current-source filename and log each — last before the error wins; (c) since codePos is
  deterministic & early (~26k), the culprit is an early core script (codescripts/* or
  maps/mp/gametypes/_load + its includes) — bisect by content. NOTE the GSC source IS in memory (e.g.
  `maps/mp/mp_backlot.gsc main()`), and per-function debug strings exist as `"<file>.gsc <funcsig>"`.
  Diagnostic hooks added this session: `[COD4MP-GSCLOAD]` on the loader cluster (NOT useful — handles
  not names; can remove). `[COD4MP-GSCERR]` on the reporters gave the codePos.
- **DEAD ENDS (session PM4 — don't repeat):** the codePos struct `*(0x82000764)` = guest `0x3000A000`
  is MINIMAL — only +16 (the codePos) is set, all other fields are 0, so it has **no filename/source
  ptr**. Worse, `0x3000A000+16` is volatile/aliased (a watchpoint on it caught a host `TimerQueue`
  thread writing unrelated incrementing values), so codePos-based resolution is unreliable. gdb gotchas
  that cost time: guest pointers/ints are **big-endian** (byte-swap every `*(uint*)` read of guest mem);
  guest base is `0x100000000` (hardcode it — `rsi` is only base at a `break *fn` EXACT entry, not a
  prologue-skipped `break fn`). Tools written but inconclusive: `cod4_mp_gscfile.sh` (codePos
  watchpoint), `cod4_mp_gscstruct.sh` (struct dump).
- **RECOMMENDED PIVOT to name the file (not yet done):** stop chasing codePos. Instead capture the
  COMPILE-TIME call stack on the Main XThread (the parser, with the source filename in a frame), e.g.
  hook a parser/lexer fn or the rawfile/source read that takes the `.gsc` name as an arg, and log each
  file as it compiles — last before the error wins. Finding that fn is the open task (the GScr loader
  cluster sub_821FF738/82235550/821FFD10/821FFC90 are buffer/alloc, NOT it). Alt strategy: compare GSC
  compiler behavior vs the WORKING SP recomp (`mw-recomp`) to spot the recomp parser divergence.
- New tools: `tools/cod4_mp_xref.py` (RELIABLE data-VA xref: matches `lis`+`addi`/`ori` pairs;
  validated on `localhost`→CL_Connect. NOTE the GSC error strings are pointer-TABLE referenced, so they
  do NOT xref — don't retry that). Hooks `[COD4MP-CONSOLE]`/`[COD4MP-GSCERR]` are diagnostic — strip
  before commit; but KEEP `[COD4MP-FIX1]`.

### ROOT CAUSE — CORRECTED 2026-06-23 (the hang mechanism; now fixed by FIX1 above)
**Start Match (`sub_82200598`) never returns — the Main XThread busy-spins inside it, so the main
`Com_Frame` loop never runs again.** That single blocked loop explains every prior symptom. Evidence:
- gdb at the hang: **Thread 37 "Main XThread" is NOT in a wait syscall** — it is executing guest code,
  and the FULL stack is **byte-identical across two samples 1 s apart** (a spin), namely:
  `sub_82209F20 ← sub_8220A068 ← sub_8220B328 ← sub_8220CF20 ← sub_82220518 ← sub_82220780 ←
   sub_82263760 ← sub_82271428 ← sub_822030A0 ← sub_82200598(Start Match) ← sub_82207D48 ← … ← xstart`.
  `sub_82200598` is the Start-Match fn (handoff already had CL_Connect's caller lr `0x82200868` inside
  it), so **Start Match has not returned** — the client is wedged deep in its call chain.
- **The exact spin (pinned by gdb disasm + breakpoint-hit-count):** an INTERNAL byte-copy loop inside
  `sub_82209F20` (`loc_82209F88`), host PC `sub_82209F20+247` = `mov %bpl,(%r14,%r10,1)` (the `stb`
  store). Proof it never leaves the fn: auto-continue breakpoints on all 8 chain fns logged **0 entry
  hits over 3 s** while the PC stayed pinned. The loop is `r4--; copy byte; while(r4 != 0)` and at the
  hang the guest counter **r4 = 0xFFFF7C2E = −33746 (NEGATIVE)** → it can never reach 0 → ~infinite
  copy from image src guest `0x82e464a1` into scratch `0x70160000+`.
- **Why r4 is negative — traced to a vsnprintf-family bug:** caller `sub_8220A068` (2645-line
  `Com_sprintf`-style fn) computes the copy length at its two `sub_82209F20` call sites as
  `r4 = *(r23+8412) − 2` (and `−3`). The field `*(r23+8412)` is set once (line 31179-84 of
  `cod4_mp_recomp.12.cpp`) as **`r29 − r22`** — the length of the current `%`-conversion specifier
  (printf isolates the spec by temp-null-terminating it at `r29`=scan-ptr and taking `end−start`). At
  the hang that length is **≈ −33744 (0xFFFF7C30)**: the spec pointers are corrupt — `r29` sits ~0x83D0
  bytes *before* `r22`. So a **corrupted format-spec pointer pair → negative spec length → r4<0 →
  unbounded copy**. (Note `0x7C30` also appears in dest base `0x70157c30`, reinforcing pointer corruption.)
- This is NOT a network or thread-deadlock bug. The Server thread (Thread 59) merely sits idle in
  `NtWaitForSingleObjectEx` because the client never gets far enough to talk to it.
- Consequences that fooled the earlier trace: because `Com_Frame` never iterates, (a) the per-frame
  connect pump `sub_822CAC00` (CL_CheckForResend) **stops being called** — its caller is the per-frame
  `sub_822CB3B0`; and (b) the resend realtime clock **`*(0x82430244)` is frozen at 1**, so the resend
  gate `elapsed = now − lastSent ≥ 100ms` could never pass anyway.
- **Bridge experiment was run and came back NEGATIVE/decisive:** poking cl0 state→3 and lastSent→−200
  (so the gate would pass) produced **zero** effect — state stayed 3, send counter stayed 0, CONN hook
  did not fire, `sub_8222D978` (getchallenge transmit) did not fire. Confirms the pump is simply never
  invoked; the blocker is the blocked main loop, **NOT** the state value, the resend gate, or the net layer.
- NB: the per-client state field DOES reach **4 (CA_CHALLENGING)**, contradicting the old "never leaves 0"
  claim — it is set during initial Start-Match processing, then frozen because the loop stops pumping.

**Next step toward a fix:** find out WHY the format-spec pointers (`r22`/`r29`) in `sub_8220A068` are
corrupt at the failing call. Concretely: set a conditional breakpoint at `sub_82209F20` entry early
(during boot, before Start Match) that fires when the length arg (guest r4) is `< 0` or huge, then dump
guest `r22`/`r29`/`r5` (source = the format string being processed) and the call stack. Identify the
format string + which `%`-spec scan corrupts the pointer. Likely either (a) a recomp mistranslation of
the spec-scan pointer arithmetic in `sub_8220A068`, or (b) upstream corruption of the format string/args
fed into Start Match's status print. The `sub_8220A068` spec-length store is at lines 31179-31184 of
`generated/cod4_mp_recomp.12.cpp`; the two unbounded-copy call sites at 31655-31669 and 31691-31705.
Tooling notes: `tools/cod4_mp_disasm_spin.sh` reliably selects the Main XThread (gdb Python
`t.switch()` by name) and disassembles the spin — reuse its thread-select for the conditional-bp run.
`tools/cod4_mp_loopfind2.sh` is the breakpoint-hit-count loop finder. (`cod4_mp_loopfind.sh`
finish-ladder is superseded — its SIGINT lands on a random thread.)

### (OBSOLETE original root note — kept for context; see correction above)
- ~~Root, traced precisely: the per-client **connection state never leaves 0 (CA_DISCONNECTED)**, so
  the challenge-send state machine never reaches CA_CHALLENGING(3), so `getchallenge` is never
  transmitted~~ … It is **NOT** the socket layer, **NOT** the server thread (it loops fine), and
  **NOT** a "need 2 controllers" gate (CL_Connect fires with one player and iterates all 4 slots).

### Build / run / drive (all from repo root unless noted)
```
# SDK (only if you edit it):   cd /home/dlynch/dev/rexglue-sdk && cmake --build out/build/linux-amd64 --config Release --target install -j24
# Codegen (only if toml/xex):  rexglue codegen cod4_mp_manifest.toml   (rexglue at rexglue-sdk/out/install/linux-amd64/bin)
cmake --build out/build/linux-amd64-release -j24
# drive to a local match:
DISPLAY=:1 python3 tools/cod4_mp_drive.py --boot 16 --seq "s2 d a s2 a s2 a s2 a s25"
# ALWAYS after a kill:  pkill -9 -x cod4_mp ; rm -f /dev/shm/xenia_memory_*
```
Menu path: Main Menu → **down → A** (Split Screen) → **A** (join player) → **A** (Continue) →
**A** (Start Match). NB screenshots: use `import -window <wid>` (wid from `wmctrl -l | grep cod4_mp`),
NOT `-window root` — the user's `:1` often has other windows over the game region.

### Key constants / fixes already in place
- XEX: image_base `0x82000000`, code_base `0x820A0000`, entry `0x82103AC8`, 12613 fns.
- `cod4_mp_manifest.toml`: `setjmp_address=0x821217A0`, `longjmp_address=0x8211E680`.
- `src/cod4mp_app.h`: `OnLoadXexImage` → `"game:\default_mp.xex"` (CRITICAL real fix).
- `config/functions_mp.toml` hints: `0x820BDEF8`, `0x820C9478`, `0x820DC640`(parent 0x820DC5F8),
  `0x8210D9C0`(parent 0x8210D938), `0x8229CA60`(parent 0x8229CA18).
- **WORKING TREE has diagnostic hooks in `src/cod4mp_patches.cpp`** ([COD4MP-NET2]/[COD4MP-CONN]) —
  STRIP before any commit. SDK is otherwise clean (only prior SP graphics changes in rexglue-sdk).

### The connect handshake — functions & memory map (verified this session)
- `sub_822CAC00` = client challenge state machine. Per-client **state = `*(0x82435780 + cl*36)`**
  (u32, big-endian). Sends `getchallenge` only when state==3 (==4/==6 are other states). Resend gated
  by elapsed-ms `*(*(0x82430040) + cl*0x61564 + {8,28})` vs thresholds 100/3000.
- `sub_8222D978` = NET_OutOfBandPrint (the getchallenge **transmit**). **Never fires.**
- `sub_822CB820` = SERVER SV_ConnectionlessPacket (refs "rcon"). **Never fires.**
- `sub_822CC548` = client recv (refs "challengeResponse"/"connectResponse"). **Never fires.**
- `sub_822CB048` = **CL_Connect** (refs "localhost"). **Fires once** on Start Match; calls
  `sub_822C9780(0..3)` (per-client) + `sub_8222D2D0` (addr resolve). No direct stores; sets state via
  callees. `sub_822C9780(cl)`: `if (clientState>=3) sub_822C94F0()` — a check, not the 0→2 setter.
- Command-string VAs (for finding more handlers via their split lis/addi(ori) immediates):
  getchallenge `0x82059094`, challengeResponse `0x82059170`, connectResponse `0x820591C8`,
  rcon `0x82058CC4`, localhost `0x820575D8`, loopback `0x8206B7B0`, infoResponse `0x820592A4`.
- SDK net (REMOTE/System-Link only, NOT the local cause; reverted to clean): `WSARecvFrom` stubbed
  `-1`; `XNetXnAddrToInAddr` returns 1 w/o writing; `XNetConnect`/`XNetGetConnectStatus`/
  `XNetRegisterKey` are `REX_EXPORT_STUB`. CoD4 uses VDP sockets (proto 254) + WSASendTo/WSARecvFrom
  for remote traffic; local match uses in-process loopback (zero socket I/O observed).

### Tools (now persisted in `tools/`)
- `cod4_mp_poke.py` (raw guest-VA peek/poke, u32 BE; shm offset == guest VA. `read/write VA`,
  `dread/dwrite BASEVA OFF` for pointer-deref fields). `cod4_mp_mainbt.sh` (full Main-XThread bt ×2 to
  detect a spin — this is how the Start-Match spin was found). `cod4_mp_loopfind.sh` (finish-ladder;
  FIX needed: reliably select the Main XThread before laddering).
- `cod4_mp_drive.py` (menu drive+screenshot), `cod4_mp_drive2.py` (2 virtual pads),
  `cod4_mp_pad_only.py` (pad only, for gdb runs), `cod4_mp_nullcall.sh` (InvalidFunctionTrap drain),
  `cod4_mp_hang_gdb.sh` (drive→SIGINT→all-thread bt), `cod4_mp_match_gdb.sh` (trap capture during
  Start Match), `cod4_mp_dvar.py` (read/`set` guest dvars via shm), `cod4_mp_place.py` (classify a
  missing-target addr: container fn + standalone-vs-chunk).
- How to find a guest fn that references a data VA `0xHHHHLLLL`: low16 `L`=`LLLL`; if `L<0x8000`
  immediate is `+L` with `lis = HHHH`; else `lis = HHHH+1`, `addi = L-0x10000`. Grep generated for
  `addi rX,rX,<imm>` or `ori rX,rX,<L>`, map line→enclosing `DEFINE_REX_FUNC` (see cod4_mp_place.py).

---

## TRACK A — drive the client connection state 0 → 2 → 3 (get the human in)

**Goal:** make a local client's `state` (`*(0x82435780+cl*36)`) advance to CA_CHALLENGING(3) so
`getchallenge` transmits and the loopback handshake completes → spawn into the match.

**Open question:** CL_Connect runs but no client's state leaves 0. Find the 0→CA_CONNECTING(2) setter
and why it doesn't run / doesn't stick.

**First steps:**
1. Find the **writer** of the state field. `sub_822CAC00` has a `+16` store at its tail
   (loc_822CB040, ~line 49530 in `generated/cod4_mp_recomp.18.cpp`) — read the stored value & path.
   Also inspect `sub_822C94F0` (CL_Connect callee) and the match-start/CL_MapLoading chain
   (caller of CL_Connect was lr `0x82200868`, in the `sub_82200598` Start-Match path).
2. Read the **guards** in CL_Connect / sub_822C9780 at runtime — `[r31+244]`, `[r11+22320]`,
   `[r11+22560]`, `[r30+16] vs 5`, and the per-client "active/signed-in" flags — to see which
   branch skips the state-set. Extend the [COD4MP-CONN] hook (already in cod4mp_patches.cpp) or use
   `tools/cod4_mp_dvar.py` (it has a `set` mode: poke fields via the shm).
3. **Bridge experiment (also informs Track B):** with `tools/cod4_mp_dvar.py set`, POKE the state
   field to 3 (and set the resend elapsed `[…+28]` ≥ 3000) for the active client while sitting at
   "Awaiting challenge". If `getchallenge` then transmits (watch the [COD4MP-NET2] sub_8222D978 hook)
   and the server replies / it advances → ONLY the state transition is broken (good: small fix).
   If it still stalls → the loopback transport is ALSO broken (wider fix). Either result is decisive.
4. Hypotheses to confirm/kill: (a) state-set gated on a per-client "signed-in/primary" flag not set
   for our splitscreen join; (b) gated on `sv_running`/server-fully-up which isn't set (see Track B);
   (c) connectTime/time baseline wrong so it never advances even if set.

---

## TRACK B — alternative vehicle: server + bots, bypassing the client handshake

**Rationale:** bots are **server-side** (`addtestclient()` builtin + the in-game GSC `TestClient`/
`addTestClients()`, gated by dvar `scr_testclients`; all confirmed present — see
`memory/cod4-mp-bots-feasibility.md`). They do NOT need the human-client handshake. So we can validate
the bot spawn path even while Track A is unsolved, and possibly get a playable bot match sooner.

**First steps:**
1. **Confirm the listen server actually runs.** Earlier `sv_running` read was ambiguous
   (`tools/cod4_mp_dvar.py` returned raw `0x01000018`). Re-verify: fix the dvar decode, or hook a
   known server fn (e.g. SV_Frame) / read `sv_running` via a guest hook. Need: does Start Match set
   `sv_running=1` and spawn the server? (`dedicated=0`, `sv_maxclients=4` were observed.)
2. **Spawn testclients.** `scr_testclients` is a SCRIPT-created dvar (only exists once the gametype
   GSC runs, i.e. after a match loads). Set it >0 and force the gametype init to re-run: poke it via
   `tools/cod4_mp_dvar.py set scr_testclients 4` once in a match, then trigger `map_restart` (find the
   guest cmd path or the GSC re-entry). Watch whether `addtestclient()` runs (hook its host/guest fn).
3. **Find/hook `addtestclient`.** Locate the GSC builtin's implementation (search the builtin dispatch
   table or the "addtestclient" string ref `→` handler) and hook to confirm a fake client is added to
   the server's client array — bots can exist server-side even if the human is stuck at "Awaiting
   challenge". This proves the bot mechanism independent of Track A.
4. If bots spawn server-side, the remaining gap to "playable" is just Track A (human client connect).
   Track A's bridge experiment (#3) is the join point.

---

## Cross-cutting reminders
- `pkill -9` leaks `/dev/shm/xenia_memory_*` (~4.8 GB each) → ALWAYS `rm -f /dev/shm/xenia_memory_*`
  after a kill, or RAM exhausts.
- ptrace_scope=1: gdb must LAUNCH the child; `handle SIGSEGV nostop noprint pass` (write-watch faults
  are recoverable). The gdb harnesses already do this.
- Release SDK emits NO log-macro output — SDK-side probes must `std::fopen` a file (see how the
  reverted net probes did it). Game-side hooks can `fprintf(stderr,…)`.
- Don't commit the [COD4MP-*] diagnostic hooks; `git checkout -- src/cod4mp_patches.cpp` once the
  real fix lands (the file's clean template is recoverable from git / cod4_app.h pattern).
