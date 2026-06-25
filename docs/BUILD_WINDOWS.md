# Building & testing `cod4_mp` on Windows (rexglue + mw-recomp-mp)

This is the **Call of Duty 4 MULTIPLAYER** (iw3mp / `default_mp.xex`) recompile. The headline feature to
test right now: **Bot Warfare bots that fully play a System Link match** — they navigate the map, sprint,
crouch/prone/jump, aim down sights, shoot, knife, and kill, split across two teams, with working killcams.

Status: this tree is developed/tested on Linux. The source + SDK are portable to Windows-Clang (same as the
blops/mw2 recomps). Build natively on Windows per below.

> **You provide separately (copyrighted, NOT in the repo):**
> 1. `assets/default_mp.xex` — the Xbox 360 CoD4 **multiplayer** executable (needed for codegen *and* run).
> 2. The CoD4 360 **game data** (the extracted disc/`zone` fastfiles etc.) at a folder you'll pass as
>    `--game_data_root`. Needed to *run* (not to build).

---

## 1. Prerequisites (Windows host, once, Administrator PowerShell)

```powershell
choco install llvm cmake ninja git 7zip -y      # clang++ 18+, CMake, Ninja
winget install KhronosGroup.VulkanSDK           # Vulkan SDK (or vulkan.lunarg.com installer)
```

Also install the **Windows SDK** (Visual Studio Build Tools → "Desktop development with C++", or the
standalone Windows SDK). Use the **"x64 Native Tools" / LLVM** shell so `clang++` + the Windows SDK are on
PATH. Verify in a fresh shell: `clang++ --version` (≥18), `cmake --version`, `ninja --version`.

## 2. Get the source (two sibling repos)

```powershell
cd C:\dev
git clone git@github.com:daniel-lynch/rexglue-sdk.git
git clone git@github.com:daniel-lynch/mw-recomp-mp.git
git clone https://github.com/ineedbots/iw3_bot_warfare.git   # Bot Warfare — for the waypoint data (runtime)
```

The SDK must match the version this game targets — **`sdk_version = "0.8.1.29"`** (see
`mw-recomp-mp/cod4_mp_manifest.toml`). If `main` has drifted, check out the tag/commit for 0.8.1.29.

The **Bot Warfare** mod (`iw3_bot_warfare`) is an external clone (not vendored here). The bot GSC itself is
already committed under `mw-recomp-mp/gsc_inject/` (our adapted copy); we only need the upstream clone for
its hand-made **waypoint CSVs** (`scriptdata/waypoints/<map>_wp.csv`), served from disk at runtime.

Drop your `default_mp.xex` at `C:\dev\mw-recomp-mp\assets\default_mp.xex`.

## 3. Codegen (regenerate `generated/` — it is NOT committed)

`generated/` is the PPC→C++ the recompiler emits from the xex; it's large + copyright-derived, so it's
git-ignored. Regenerate it once after cloning (and any time `config/*.toml` or the xex changes). The
`rexglue` codegen tool ships with the SDK:

```powershell
cd C:\dev\mw-recomp-mp
# rexglue.exe is produced by the SDK build (step 4) or its tools; run it on the manifest:
rexglue codegen cod4_mp_manifest.toml
# -> writes ~50 MB of cod4_mp_recomp.*.cpp into generated/
```

(If `rexglue` isn't on PATH yet, build the SDK first — step 4 — then run codegen, then re-build the game.)

## 4. Build the SDK (its install prefix is consumed by the game build)

```powershell
cd C:\dev\rexglue-sdk
cmake --preset win-amd64
cmake --build out/build/win-amd64 --config Release --target install -j20
# Installs to:  C:\dev\rexglue-sdk\out\install\win-amd64
```

## 5. Build the game

```powershell
cd C:\dev\mw-recomp-mp
cmake --preset win-amd64-release -DCMAKE_PREFIX_PATH=C:\dev\rexglue-sdk\out\install\win-amd64
cmake --build out/build/win-amd64-release -j20
# Binary:  C:\dev\mw-recomp-mp\out\build\win-amd64-release\cod4_mp.exe
```

## 6. Run + drive the bots

```powershell
cd out\build\win-amd64-release
@"
log_verbose = true
async_shader_compilation = false
vulkan_readback_resolve = true
input_backend = "sdl"
"@ | Set-Content cod4_mp.toml

# --- paths to the runtime GSC + waypoint data (the defaults are the dev box's Linux paths; OVERRIDE them) ---
$env:COD4_GSCDIR = "C:\dev\mw-recomp-mp\gsc_inject"                          # the committed bot GSC + adapter
$env:COD4_WPDIR  = "C:\dev\iw3_bot_warfare\scriptdata\waypoints"             # waypoint CSVs from the BW clone

# --- bot feature flags (env vars) — set these in the SAME shell before launching ---
$env:COD4_MAXCLIENTS = "12"   # server size (12 = full 6v6: 1 human + 11 bots)
$env:COD4_BOTS       = "11"   # how many Bot Warfare bots to fill
$env:COD4_BOTSETTLE  = "240"  # frames between each bot's spawn — 240 needed at 11 bots (150 crashes ~bot 4)
$env:COD4_GSCINJECT  = "1"    # inject Bot Warfare GSC
$env:COD4_BOTSPAWN   = "1"    # force each bot's team + class spawn
$env:COD4_BOTAI      = "1"    # native usercmd AI (movement / buttons / aim)

.\cod4_mp.exe --game_data_root=C:\path\to\cod4\gamedata
```

> **Paths matter:** `COD4_GSCDIR` and `COD4_WPDIR` default to the original dev box's Linux paths and WILL NOT
> exist on your machine — set them to your clones (above) or the bots won't get their GSC / waypoints.

- `async_shader_compilation = false` → synchronous shaders so the menu renders (slow first frames, allow
  ~40–90 s).
- `vulkan_readback_resolve = true` → required for guest frames to reach the swapchain.
- `assets\default_mp.xex` must be present at runtime; `--game_data_root` must point at the CoD4 game data.

### Navigate to a bot match (your real Xbox controller works)

`Main menu → System Link → Create Game → Start Match → CHOOSE TEAM → Auto-Assign → CHOOSE CLASS → (pick one)`.
After you spawn in, the bots load themselves in one at a time (watch the scoreboard fill) and start playing.

### What you should see (this is what to test)

- **6v6 fills in:** 11 bots join, split across Marines/OpFor, each with a **random class loadout**.
- **Bots fully play:** they navigate the whole map (Backlot waypoints), **sprint** in the open,
  **crouch/prone/jump** around obstacles, **aim down sights**, shoot, knife, and get kills.
- **Killcams work:** when a bot kills you, the killcam **replays** the bot's last few seconds from its POV
  (not a frozen `0:00.0` red frame — that bug is fixed).
- **Scoreboard:** bots show a clean ping (no red bar).

### Knobs (optional)

| env var | effect |
|---|---|
| `COD4_BOTS=N` | number of bots (try 7 for a lighter match; 11 = full 6v6) |
| `COD4_BOTSETTLE=N` | inter-bot spawn delay in frames (raise if high bot counts crash; lower for faster fill) |
| `COD4_BOTCLASS=N` | force every bot onto `offline_classN_mp` (1–5) instead of random |
| `COD4_BOTDUMP=1` | verbose per-bot logging (stance / speed / ADS / movement) to stderr |
| `COD4_LIVE=1` | boot the Xbox LIVE menus instead (separate feature — Barracks / Private Match) |
| `COD4_BOTPINGFIX_OFF=1` / `COD4_BOTCMDTIME_OFF=1` | disable the ping / killcam-timeline fixes (default ON) |

---

## Notes / known caveats

- **High bot counts are a settle-time tradeoff,** not a hard wall: 7 bots are very stable at any settle;
  11 bots need `COD4_BOTSETTLE=240` (each bot's heavy spawn-script must drain before the next is injected).
  If a match crashes during the bot fill, raise `COD4_BOTSETTLE` or lower `COD4_BOTS`.
- **Two-player / friend-connect is NOT wired yet** — this is single-host + bots over System Link. Connecting
  a second real client is the next milestone.
- Custom Create-a-Class is still rank-gated (greyed) — bots use the 5 default offline classes; player ranking
  / fake-Live unlocks are a planned next step.
- The bot logic is all behind the `COD4_*` env flags above and is off unless you set them; a plain launch
  boots to the normal menus.
