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

**Important — use the right branch on BOTH repos: `feat/matchmaking-broker`.** This branch has everything:
the Xbox 360 System Link host-start + Live backend fakes (a superset of the older `testing/five-stability-fixes`),
PLUS the **two-client matchmaking** work (the headline below). A clean `main` / `testing/five-stability-fixes`
does NOT have the matchmaking. Check it out on BOTH sibling repos:

```powershell
cd C:\dev\rexglue-sdk    ; git checkout feat/matchmaking-broker
cd C:\dev\mw-recomp-mp   ; git checkout feat/matchmaking-broker
```

(The game records `sdk_version = "0.8.1.29"` in `cod4_mp_manifest.toml`; this branch is that line plus the
cod4 xam fakes + the matchmaking, and is the SDK the committed code was built against.)

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

## 7. TWO CLIENTS in one "Xbox Live" match (host + a friend over VPN)

This is the new headline. Two **separate machines** (you + a friend), both on the same VPN, join the **same
Find-Match game** with bots. One machine is the **HOST**, the other is the **JOINER**. There is no real Xbox
Live — a tiny **network broker** stands in for the Live matchmaking service: the host advertises its session
**directly to the joiner over UDP** (no shared folder, no SMB), the joiner discovers it and connects over the
VPN. **Only the JOINER needs to know an IP — the host's.** The host auto-learns the joiner's address from its
first packet, so it doesn't need to know the joiner's IP in advance (handy when the joiner is on a different
subnet, e.g. a `172.16.x.x` VPN address).

> **The example IPs below are placeholders.** Use your real VPN addresses. In our setup the **host is
> `192.168.2.2`** and the **joiner is a `172.16.x.x`** address — substitute accordingly. The only IP that has
> to be typed anywhere is the **host's**, into the joiner's `COD4_MM_PEERS`.

> **Honesty note (read this):** this path was developed and **validated same-box** using Linux network
> namespaces to emulate "two real machines with distinct IPs" — two clients + 9 bots in one Team Deathmatch,
> joiner spawned into first-person and stayed in for 150 s+, and the network broker's UDP gossip was verified
> moving session/arbitration records bidirectionally between the two distinct IPs. The **real
> two-machine-over-VPN run is the live test you're about to do.** It uses the identical SDK real-IP mode; the
> one thing only your VPN can confirm is that it passes UDP between you. If it doesn't connect, the
> diagnostics at the end tell you which hop failed.

### 7.1 What each side needs

| | HOST (you, `192.168.2.2`) | JOINER (your friend, `172.16.x.x`) |
|---|---|---|
| Role | hosts the match + runs the bots | joins the host's match |
| `COD4_LOCAL_IP` | **its own** VPN IP (`192.168.2.2`) | **its own** VPN IP (`172.16.x.x`) |
| broker enable | `COD4_MM_NETBROKER=1` (auto-learns the joiner) | `COD4_MM_PEERS=192.168.2.2` (the host's IP) |
| Bots | yes (host owns the bots) | no |

Either `COD4_MM_NETBROKER=1` **or** `COD4_MM_PEERS=<ip[,ip…]>` turns on the network broker. The host uses
`COD4_MM_NETBROKER=1` and learns the joiner from its inbound packets; the joiner uses
`COD4_MM_PEERS=<host IP>`. (If you'd rather be explicit, you *can* set `COD4_MM_PEERS` on both sides to the
other's IP — self-originated records are ignored, so listing both IPs on both machines is also fine.) The
broker gossips on **UDP port 31100** by default (override with `COD4_MM_BROKER_PORT`, same value on both).

### 7.2 Firewall (both machines)

The title uses raw UDP on the Xbox ports, plus the broker port. Open them inbound on **both** machines
(Administrator PowerShell):

```powershell
New-NetFirewallRule -DisplayName "cod4mm-udp" -Direction Inbound -Action Allow `
  -Protocol UDP -LocalPort 59395,62723,59651,31100
```

(If your VPN exposes its own adapter/profile, make sure the rule applies to it — set `-Profile Any` if unsure.)

### 7.3 HOST — launch (your machine, IP `192.168.2.2`)

Same build + `cod4_mp.toml` as section 6. Set these in the SAME shell, then launch:

```powershell
$env:COD4_GSCDIR = "C:\dev\mw-recomp-mp\gsc_inject"
$env:COD4_WPDIR  = "C:\dev\iw3_bot_warfare\scriptdata\waypoints"

# --- be the Live host of a matchmaking game ---
$env:COD4_LIVE         = "1"                    # Xbox LIVE menus (Find Match)
$env:COD4_PLAYLIST     = "1"                    # populate the Find-Match playlist
$env:COD4_MMHOST       = "1"                    # host the match (loopback connect-window fix)
$env:COD4_MM_BROKER    = "1"                    # enable the broker (advertise + aggregate)
$env:COD4_MM_NETBROKER = "1"                    # network broker; auto-learns the joiner (no joiner IP needed)
$env:COD4_LOCAL_IP     = "192.168.2.2"          # this host's REAL VPN IP

# --- matchmaking join glue (validated set) ---
$env:COD4_MM_ARBEMPTY  = "1"   # host aborts its own start so the joiner can seat
$env:COD4_MM_NOKICK    = "1"   # swallow the endparty/arbitration kick of the joiner
$env:COD4_MM_NODROP    = "1"   # don't SV_DropClient the joiner once it's in
$env:COD4_MM_BOTRESERVE= "2"   # leave 2 slots free so the joiner has somewhere to go
$env:COD4_MM_PORT_IDX  = "2"   # advertise the GAME socket (59651) as the online port
$env:COD4_MM_NOREROUTE = "1"   # real distinct IPs: do NOT port-reroute (that's a same-box hack)

# --- bots ---
$env:COD4_MAXCLIENTS = "12"
$env:COD4_BOTSETTLE  = "120"
$env:COD4_GSCINJECT  = "1"
$env:COD4_BOTSPAWN   = "1"
$env:COD4_BOTAI      = "1"

.\cod4_mp.exe --game_data_root=C:\path\to\cod4\gamedata
```

### 7.4 JOINER — launch (your friend's machine, IP `172.16.x.x`)

No bots, no host flags. Point `COD4_MM_PEERS` at the host, use your own IP:

```powershell
$env:COD4_LIVE         = "1"
$env:COD4_PLAYLIST     = "1"
$env:COD4_MM_BROKER    = "1"
$env:COD4_MM_PEERS         = "192.168.2.2"       # the HOST's IP — turns on the UDP network broker
$env:COD4_LOCAL_IP         = "172.16.x.x"        # YOUR real VPN IP (the friend's actual address)
$env:COD4_MM_NOREROUTE     = "1"
$env:COD4_MM_PORT_IDX      = "2"
$env:COD4_MM_SEARCH_DELAY_MS = "10000"           # CRUCIAL: hold Find Match ~10s so the broker rendezvous
                                                 # completes — otherwise the joiner host-falls-back before
                                                 # the host's session arrives and you BOTH end up hosting

.\cod4_mp.exe --game_data_root=C:\path\to\cod4\gamedata
```

> **If the joiner "goes straight to hosting its own game,"** this flag is missing or too small. First contact
> is one-way (the joiner beacons the host, the host then learns it and gossips its session back), so the
> joiner's *first* search is empty by definition; without the delay its title instantly host-falls-back. 10 s
> covers the round trip comfortably. (The host is unaffected — `COD4_MMHOST` makes it always host and never
> adopt, so even a delay there is harmless.)

### 7.5 Drive sequence (real controllers)

Both of you go: `Main menu → Xbox LIVE → (sign in) → Find Match → <playlist>`.

1. **HOST first.** Let it create the session + start filling bots. Give it ~15–20 s — the host needs to be
   gossiping its session and have a few bots seated before the joiner searches.
2. **JOINER then** picks the same playlist and presses through Find Match. It discovers the host's session
   (received over UDP from the host), shows **"Trying to join potential match"**, and connects to
   `192.168.2.2:59651` over the VPN.
3. The host's match **aborts its own start** (`ARBEMPTY`) so the lobby stays open; the joiner seats. On the
   host you'll briefly see the joiner appear, the arbitration **kick is swallowed** (`NOKICK`), and the
   joiner is **not dropped** (`NODROP`). Joiner advances `connstate 8 → 9` (PRIMED → ACTIVE).
4. Both press through the team/class menus and spawn in. You should be in **one match together with the
   bots playing** around you.

### 7.6 If it doesn't connect — which half failed

- **Joiner never sees a match** (Find Match stays empty): the broker gossip isn't crossing the VPN. Check the
  joiner's `COD4_MM_PEERS` is the **host's** IP (`192.168.2.2`), the host has `COD4_MM_NETBROKER=1`,
  `COD4_MM_BROKER=1` on both, and **UDP 31100** is open inbound on both (the firewall rule above). On the host
  the trace should log `NETBROKER: learned peer <joiner ip>:<port>` once the joiner reaches Find Match — if it
  never does, the joiner's packets aren't arriving (firewall / `COD4_MM_PEERS` / VPN reach). This is the
  broker-reach hop.
- **Joiner says "joining" then drops / times out**: the *game* UDP didn't traverse the VPN even though the
  broker did. Confirm the firewall rule on the **host** for inbound UDP `59651`, and that the joiner can reach
  `192.168.2.2` at all (`Test-NetConnection 192.168.2.2` — note ICMP may be blocked even when UDP works). This
  is the game-port hop.
- **Joiner connects then gets kicked after a few seconds**: the `COD4_MM_NODROP` / `COD4_MM_NOKICK` flags
  aren't set on the **host**. Re-check the host's env block.

> Routing note: the host's replies go back to whatever source address the joiner's packets arrived from, so
> the host needs a route to the joiner's subnet over the VPN. On a typical split-tunnel VPN this is automatic
> (e.g. here `ip route get 172.16.0.5` resolves `via <vpn gw> dev tun0`). If the joiner is on a subnet your VPN
> doesn't route, the host's replies will leak out the default gateway and never arrive.

> The broker's diagnostic trace goes to a local `trace.log` (under `%TEMP%\cod4_mp_sessions` on Windows, or
> `COD4_MM_REGISTRY` if you set one — used only for the log now). Host should show `NETBROKER: listening
> udp/31100`, `PUBLISH(net)`, and `NETBROKER: learned peer …`; joiner should show `SEARCH: RETURNED 1 host
> session(s)`.

### 7.7 Diagnostics — turn on full logging + what to collect

When a join doesn't work, add these to **both** machines' env (they're cheap and off by default), then collect
the two log files from **each** side:

```powershell
$env:COD4_MM_NETLOG_DUMP   = "1"        # log every UDP send/recv: src/dst ip:port + hex/ascii of the payload
$env:COD4_MM_NETLOG_BUDGET = "200000"   # how many packet lines to keep (default 400 is too small)
$env:COD4_MM_SVPROBE       = "1"        # host only: dump the server client slots (who's CONNECTED/ACTIVE)
```

Two log files (both under `%TEMP%\cod4_mp_sessions\` on Windows, `/tmp/cod4_mp_sessions/` on Linux):

- **`trace.log`** — the matchmaking state machine: `NETBROKER: learned peer …`, `SEARCH: RETURNED …`,
  `XSessionCreate … (HOST=1)` vs `GUEST adopt host … host_ina=<ip>:<port> (this is who the joiner will try to
  connect to)`, `JoinRemote`, arbitration. This tells you **who decided to host vs join, and what IP the
  joiner aims at.**
- **`nettrace.log`** — every packet: `PKT SENDTO <ip>:<port> len=… hex=… ascii=…` and `PKT RECVFROM …`. This
  tells you **whether the joiner's connect packets actually reach the host** (look for the host's real VPN IP
  on the joiner's `SENDTO` lines, and the joiner's IP on the host's `RECVFROM` lines), and the IW3
  connectionless commands (`ÿÿÿÿconnect` / `getchallenge`) vs netchannel data.

**The decisive lines for "he won't join me":** on the **joiner**, `GUEST adopt host … host_ina=<ip>` (is `<ip>`
the host's real VPN IP, or wrongly `127.0.0.x` / the joiner's own?) followed by `PKT SENDTO <that ip>:59651`.
If the joiner adopts but never `SENDTO`s the host's IP, address resolution is the culprit; if it `SENDTO`s but
the host's `nettrace.log` shows no matching `RECVFROM`, it's the VPN/firewall on the game port.

To collect on Windows: zip the folder — `Compress-Archive $env:TEMP\cod4_mp_sessions cod4mm-logs.zip` — from
**both** machines after a join attempt.

---

## Notes / known caveats

- **High bot counts are a settle-time tradeoff,** not a hard wall: 7 bots are very stable at any settle;
  11 bots need `COD4_BOTSETTLE=240` (each bot's heavy spawn-script must drain before the next is injected).
  If a match crashes during the bot fill, raise `COD4_BOTSETTLE` or lower `COD4_BOTS`.
- **Two-player / friend-connect IS now wired** — see section 7 for the two-machine matchmaking test. It was
  validated same-box (netns-emulated distinct IPs); the real two-machine-over-VPN run is the open live test.
- Custom Create-a-Class is still rank-gated (greyed) — bots use the 5 default offline classes; player ranking
  / fake-Live unlocks are a planned next step.
- The bot logic is all behind the `COD4_*` env flags above and is off unless you set them; a plain launch
  boots to the normal menus.
