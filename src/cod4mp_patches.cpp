// Boot-advancement / instrumentation patches for the CoD4 (IW3 MP / iw3mp) recomp.
//
// Generated `sub_*` are weak; a strong definition here wins at link time. Tag logs
// `[COD4MP-*]`. PATTERN to hook a guest function sub_XXXX:
//
//   extern "C" void __imp__sub_XXXX(PPCContext& ctx, uint8_t* base);
//   REX_FUNC(sub_XXXX) {
//     log_once("sub_XXXX reached");
//     __imp__sub_XXXX(ctx, base);
//   }
//
// Read guest globals with (base + guest_addr) + __builtin_bswap32 (big-endian guest).
// STRIP boot hacks before committing (HANDOFF §9).

#include "cod4_mp_init.h"

#include <rex/system/kernel_state.h>
#include <rex/system/function_dispatcher.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <string>

namespace {

[[maybe_unused]] bool env_on(const char* name) {
  // Cache by the call-site string-literal pointer (stable): env vars don't change mid-run, and this is
  // called many times per frame (per bot) in hot paths, where repeated getenv() adds up.
  static const char* keys[32]; static bool vals[32]; static std::atomic<int> count{0};
  int n = count.load(std::memory_order_acquire); if (n > 32) n = 32;
  for (int i = 0; i < n; i++) if (keys[i] == name) return vals[i];   // miss on a not-yet-written slot is safe
  const char* v = std::getenv(name);
  bool on = v && v[0] && v[0] != '0';
  int slot = count.fetch_add(1, std::memory_order_acq_rel);
  if (slot < 32) { vals[slot] = on; keys[slot] = name; }             // same key always maps to same value
  return on;
}

[[maybe_unused]] void log_once(const char* msg) {
  static const char* last = nullptr;
  if (last != msg) {
    std::fprintf(stderr, "[COD4MP-PATCH] %s\n", msg);
    std::fflush(stderr);
    last = msg;
  }
}

[[maybe_unused]] uint32_t rd32(uint8_t* base, uint32_t ga) {
  uint32_t v;
  std::memcpy(&v, base + ga, 4);
  return __builtin_bswap32(v);
}

}  // namespace

// --- IW3 MP guest-function hooks go below this line ------------------------
// [COD4MP-BOTS] addtestclient command handler = sub_82263110 (SV_AddTestClient_f) — used by the legacy
// COD4_ADDBOTS path. (Removed the NET2/CONN tracing hooks: they ran on hot network paths and fflush'd
// stderr every call — pure RE diagnostics, real perf cost.)
extern "C" void __imp__sub_82263110(PPCContext& ctx, uint8_t* base);
// [COD4MP-BOTS] Spawn N test clients once the local match is active. Triggered from the per-frame
// client function sub_822CB3B0 (NOT sub_822CAC00 — that connect-resend pump stops being called
// once CA_ACTIVE, so it's an unreliable trigger). sub_822CB3B0 runs every Com_Frame in-match, so
// it reliably observes cl0 connstate (*(0x82435780)) == 9 (CA_ACTIVE). We add all N at once (a
// short settle after active) on the main thread, like a console `addtestclient` xN would.
extern "C" void __imp__sub_822CB3B0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82205CB8(PPCContext& ctx, uint8_t* base);  // SV_ExecuteClientCommand
// [COD4MP-BOTS] Inject a reliable client command for client `num` via SV_ExecuteClientCommand
// (sub_82205CB8), found empirically by capturing the human's Choose-Team flow. svs.clients =
// *(0x82F82D8C); client_t stride = 0xA2C08; so client N's client_t* = svs.clients + N*stride.
// Unmatched commands (like "mr"=menuresponse) route to ClientCommand(N) -> GSC. The cmd string is
// staged in guest scratch below the current stack; we snapshot/restore ctx around the call so the
// per-frame pump that calls us continues cleanly.
static void cod4_client_cmd(PPCContext& ctx, uint8_t* base, uint32_t num, const char* cmd) {
  uint32_t svc = rd32(base, 0x82F82D8Cu);
  if (svc < 0x10000u) return;
  uint32_t cl = svc + num * 0xA2C08u;
  uint32_t va = (ctx.r1.u32 - 0x800u) & ~0xFu;     // scratch below current frame
  char* d = (char*)(base + va);
  size_t n = 0; for (; cmd[n] && n < 0xFF; n++) d[n] = cmd[n]; d[n] = 0;
  PPCContext save = ctx;
  ctx.r3.u32 = cl; ctx.r4.u32 = va; ctx.r5.u32 = 1; // (client_t*, cmd, clientOK)
  __imp__sub_82205CB8(ctx, base);
  ctx = save;
}
// Trigger for the bot add+join sequence: set to 0 when the HUMAN (cl0) joins (detected in the
// SV_ExecuteClientCommand hook below — the connstate field 0x82435780 proved unreliable in-game),
// then counted up per-frame here. -1 = human hasn't joined yet.
static std::atomic<int> g_bot_trigger{-1};
// Spawn N test clients after the human joins, then TEAM-JOIN + give them a class so they actually
// play (stock testclients otherwise sit as spectators). Driven off the per-frame in-match pump
// sub_822CB3B0, paced by g_bot_trigger: at ~2s add bots, at ~6s inject "mr 16 4 autoassign" +
// "mr 16 13 offline_class1_mp,0" per bot. N from COD4_ADDBOTS. COD4_BOTNOJOIN=1 = add only (debug).
// [COD4MP-BOTSPAWN] Force a FULL spawn (team + class menuresponse) for any active bot that hasn't been
// processed — for use WITH Bot Warfare, which adds + team-assigns bots but (on this engine) leaves them
// short of a live, controllable pmove body. STRICT serial spawn: bring bots in ONE AT A TIME, fully
// settled (COD4_BOTSETTLE / COD4_BOTCLASSDELAY frames; COD4_BOTPARALLEL=1 = old burst). IMPORTANT: this
// is driven from SV_ClientThink (the GAME/server thread) — NOT the net-packet loop — so the menuresponse
// (which fires BW's heavy spawn GSC) runs on the same thread as the GSC VM, avoiding the cross-thread
// race on the GSC name-table critical section that intermittently dead-locked/hung the Server thread.
static void cod4_bot_spawn_pump(PPCContext& ctx, uint8_t* base) {
  if (!env_on("COD4_BOTSPAWN")) return;
  static int sp_phase[24] = {0}, sp_timer[24] = {0};
  static int sp_frame = 0, settle_until = 0;
  static int settle = -1, classdelay = -1;
  if (settle < 0)     { const char* s = std::getenv("COD4_BOTSETTLE");     settle = (s && s[0]) ? std::atoi(s) : 150; }
  if (classdelay < 0) { const char* c = std::getenv("COD4_BOTCLASSDELAY"); classdelay = (c && c[0]) ? std::atoi(c) : 90; }
  bool parallel = env_on("COD4_BOTPARALLEL");
  sp_frame++;
  uint32_t svc = rd32(base, 0x82F82D8Cu);
  uint32_t mcp = rd32(base, 0x82EE1D78u);
  int maxc = (mcp > 0x10000u) ? (int)rd32(base, mcp + 12) : 0;
  if (svc < 0x10000u) return;
  for (int i = 1; i < maxc && i < 24; i++) {
    uint32_t clp = svc + (uint32_t)i * 0xA2C08u;
    uint32_t st = rd32(base, clp + 0), nc = rd32(base, clp + 32);
    if (st != 4) { sp_phase[i] = 0; continue; }      // freed/reconnecting slot -> reset
    if (nc != 0 || sp_phase[i] >= 2) continue;       // real client, or this bot already spawned
    if (sp_phase[i] == 0) {
      if (sp_frame < settle_until) break;            // previous bot still settling -> hold all new spawns
      const char* team = (i & 1) ? "mr 16 4 allies" : "mr 16 4 axis";  // alternate teams -> enemies to fight
      cod4_client_cmd(ctx, base, (uint32_t)i, team);
      sp_phase[i] = 1; sp_timer[i] = 0;
      std::fprintf(stderr, "[COD4MP-BOTSPAWN] cl#%d team (%s)\n", i, team + 6); std::fflush(stderr);
    } else if (sp_phase[i] == 1 && ++sp_timer[i] >= classdelay) {
      // Random class among the 5 default offline classes (offline_class1..5_mp) for loadout variety;
      // COD4_BOTCLASS=N forces a single class.
      static bool seeded = false; if (!seeded) { std::srand((unsigned)std::time(nullptr)); seeded = true; }
      int cls = 1 + (std::rand() % 5);
      const char* fc = std::getenv("COD4_BOTCLASS"); if (fc && fc[0]) cls = std::atoi(fc);
      if (cls < 1 || cls > 5) cls = 1;
      char classcmd[48];
      std::snprintf(classcmd, sizeof(classcmd), "mr 16 13 offline_class%d_mp,0", cls);
      cod4_client_cmd(ctx, base, (uint32_t)i, classcmd);
      sp_phase[i] = 2; settle_until = sp_frame + settle;
      std::fprintf(stderr, "[COD4MP-BOTSPAWN] cl#%d class%d->spawn (next bot in %df)\n", i, cls, settle);
      std::fflush(stderr);
    }
    if (!parallel) break;                            // serial: only the lowest unfinished bot per frame
  }
}
REX_FUNC(sub_822CB3B0) {
  __imp__sub_822CB3B0(ctx, base);
  cod4_bot_spawn_pump(ctx, base);   // net-packet loop: best-known spawn point (game-thread ClientThink was worse)
  static int want = -1, phase = 0;
  if (want < 0) { const char* v = std::getenv("COD4_ADDBOTS"); want = v ? std::atoi(v) : 0; }
  if (want <= 0 || phase >= 3) return;
  int t = g_bot_trigger.load();
  if (t < 0) return;                               // human hasn't joined yet
  g_bot_trigger.store(t + 1);
  if (phase == 0 && t >= 120) {                    // ~2s after human joined: add the bots
    std::fprintf(stderr, "[COD4MP-BOTS] human joined -> adding %d test clients\n", want);
    std::fflush(stderr);
    for (int i = 0; i < want; i++) __imp__sub_82263110(ctx, base);
    phase = env_on("COD4_BOTNOJOIN") ? 3 : 1;
  } else if (phase == 1 && t >= 360) {             // ~4s after add: TEAM select each bot
    std::fprintf(stderr, "[COD4MP-BOTS] team-joining %d bots (autoassign)\n", want);
    std::fflush(stderr);
    for (int k = 1; k <= want; k++) cod4_client_cmd(ctx, base, (uint32_t)k, "mr 16 4 autoassign");
    phase = 2;
  } else if (phase == 2 && t >= 480) {             // ~2s after team: CLASS select -> spawn each bot
    std::fprintf(stderr, "[COD4MP-BOTS] class-selecting %d bots (spawn)\n", want);
    std::fflush(stderr);
    for (int k = 1; k <= want; k++) cod4_client_cmd(ctx, base, (uint32_t)k, "mr 16 13 offline_class1_mp,0");
    phase = 3;
  }
}
// [COD4MP-FIX1] Start-Match hang guard (EXPERIMENT). The hang at "Awaiting challenge"
// is an unbounded byte-copy loop inside sub_82209F20 (a string copy/unescape): it is
// called from the Com_sprintf-family fn sub_8220A068 with a NEGATIVE length. The caller
// computes length = *(r23+8412) - 2, and that field (the %-conversion-spec length r29-r22)
// is 1 for a degenerate/empty format string, so length = 1 - 2 = -1. The copy loop does
// `r4--; while(r4 != 0)` so a negative count never terminates (it just marches memory).
// Clamp any negative guest r4 to 0 here, which routes sub_82209F20 into its clean
// empty-copy path (entry does `cmpwi r4,0; beq <skip-copy>`), letting Start Match return
// to Com_Frame. If this clears the hang, the CONN/NET2 hooks above should start firing.
extern "C" void __imp__sub_82209F20(PPCContext& ctx, uint8_t* base);
REX_FUNC(sub_82209F20) {
  if ((int32_t)ctx.r4.u32 < 0) {
    log_once("[COD4MP-FIX1] sub_82209F20 negative length clamped to 0 (Start-Match hang guard)");
    ctx.r4.u32 = 0;
  }
  __imp__sub_82209F20(ctx, base);
}

// gstr: read a guest string (guest ASCII). Used by the GSC-inject + probe hooks below. (Removed the
// COD4MP-CONSOLE Com_Printf echo, COD4MP-CMD console injection, and COD4MP-GSCERR reporters — pure RE
// diagnostics that hooked hot paths / fflush'd per call.)
static const char* gstr(uint8_t* base, uint32_t va) {
  if (va > 0x10000u && va < 0x90000000u) return (const char*)(base + va);
  return "";
}
// [COD4MP-GSCERR] Always-on GSC/script error capture (cheap: only logs on the error channels, which are
// rare). Com_Printf = sub_82234CB8(channel r3, fmt r4, varargs r5..). Channels ~21-23 carry the script
// runtime/compile error banner + "script:line". Logging these lets us see WHY a match shuts down.
extern "C" void __imp__sub_82234CB8(PPCContext& ctx, uint8_t* base);
REX_FUNC(sub_82234CB8) {
  uint32_t ch = ctx.r3.u32;
  if (ch >= 21u && ch <= 23u) {
    // Print the fmt (r4) + each of r5..r8 BOTH as an int AND, when the value looks like a guest pointer
    // (lands in mapped guest RAM), as a string — so a GSC error message passed via %s is revealed (it was
    // previously shown only as an integer, hiding the actual error text).
    auto argstr = [&](uint32_t v) -> const char* {
      if ((v >= 0x82000000u && v < 0x86000000u) || (v >= 0xA0000000u && v < 0xB0000000u)) {
        const char* s = gstr(base, v);
        if (s && s[0] >= 0x20 && s[0] < 0x7F) return s;   // looks like printable text
      }
      return "";
    };
    std::fprintf(stderr, "[COD4MP-GSCERR] ch=%u '%.200s' r5=%u'%.80s' r6=%u'%.80s' r7=%u'%.40s' r8=%u'%.40s'\n",
                 ch, gstr(base, ctx.r4.u32),
                 ctx.r5.u32, argstr(ctx.r5.u32), ctx.r6.u32, argstr(ctx.r6.u32),
                 ctx.r7.u32, argstr(ctx.r7.u32), ctx.r8.u32, argstr(ctx.r8.u32));
    std::fflush(stderr);
  }
  __imp__sub_82234CB8(ctx, base);
}

// [COD4MP-VARPROBE] GSC script-variable pool usage probe (gated COD4_VARPROBE). The two allocators pop a
// free list whose head index sits at pool_base+offset; the free list is built in-order at init, so the
// MAX head index ever popped == the pool high-water mark (peak simultaneous usage). We track it per pool
// and log on each new 256-entry milestone + a throttled heartbeat, so the countdown-fill curve and the
// per-bot step are visible, and we can see exactly how close to the 0x8000 ceiling we get.
//   value/variable pool: alloc=sub_82217C90, head @ 0x82C8EC14, cap 0x8000  (this is the one that crashes)
//   child/name    pool : alloc=sub_82217B90, head @ 0x82D0EC24, cap 0x10000
static uint32_t g_var_hi = 0, g_child_hi = 0;
static inline uint16_t rd16be(uint8_t* base, uint32_t va) { uint16_t h; std::memcpy(&h, base + va, 2); return __builtin_bswap16(h); }
static void varprobe(uint8_t* base, uint32_t headVA, uint32_t cap, uint32_t* hi, const char* tag) {
  if (!env_on("COD4_VARPROBE")) return;
  uint16_t head = rd16be(base, headVA);
  if (head == 0) return;                       // 0 = free list empty (about to error) — nothing to sample
  if (head > *hi) {
    bool milestone = (*hi >> 8) != (head >> 8); // crossed a 256 boundary
    *hi = head;
    if (milestone) {
      std::fprintf(stderr, "[COD4MP-VARPROBE] %s high-water %u / %u (%.0f%%)\n",
                   tag, head, cap, 100.0 * head / cap);
      std::fflush(stderr);
    }
  }
}
extern "C" void __imp__sub_82217C90(PPCContext& ctx, uint8_t* base);  // Scr_AllocVariable (value pool)
REX_FUNC(sub_82217C90) {
  varprobe(base, 0x82C8EC14u, 0x8000u, &g_var_hi, "VAR");
  __imp__sub_82217C90(ctx, base);
}
extern "C" void __imp__sub_82217B90(PPCContext& ctx, uint8_t* base);  // Scr_AllocChildVariable (child pool)
REX_FUNC(sub_82217B90) {
  varprobe(base, 0x82D0EC24u, 0x10000u, &g_child_hi, "CHILD");
  __imp__sub_82217B90(ctx, base);
}

// [COD4MP-NAMEPROBE] Diagnose the multi-bot-spawn freeze (see memory cod4-mp-bot-spawn-hang): the GSC
// Server thread grinds O(n^2) in the canonical NAME-table. sub_8221FA70 = find-or-add a name (r3=char*);
// sub_8221F1F0 = the expensive sorted-bucket INSERT that only runs for a genuinely NEW name. We count
// inserts (the O(n^2) driver) and sample the strings flowing through find-or-add, to learn WHAT is
// flooding the table (avoidable BW behaviour?) vs. generic field names (structural). Gated COD4_NAMEPROBE.
static std::atomic<uint64_t> g_name_inserts{0}, g_name_finds{0};
extern "C" void __imp__sub_8221F1F0(PPCContext& ctx, uint8_t* base);  // name-table self-adjusting reorder (MTF)
static std::atomic<uint64_t> g_f1f0_total_ns{0}, g_f1f0_max_ns{0};
REX_FUNC(sub_8221F1F0) {
  // NOTE: this reorder is LOAD-BEARING — skipping it (an earlier COD4_SKIPMTF experiment) deterministically
  // breaks boot-time GSC init. So we can only speed it up, not bypass it. Here we TIME it to learn whether
  // the multi-bot-spawn grind is few-but-slow calls (long degenerate chains) or many-fast calls (volume).
  if (env_on("COD4_NAMEPROBE")) {
    auto t0 = std::chrono::steady_clock::now();
    __imp__sub_8221F1F0(ctx, base);
    uint64_t ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now() - t0).count();
    uint64_t n = g_name_inserts.fetch_add(1) + 1;
    g_f1f0_total_ns.fetch_add(ns);
    uint64_t mx = g_f1f0_max_ns.load();
    while (ns > mx && !g_f1f0_max_ns.compare_exchange_weak(mx, ns)) {}
    if ((n & 0x3FF) == 0)                           // every 1024 calls: avg + max per-call time
      std::fprintf(stderr, "[COD4MP-NAMEPROBE] reorder calls=%llu avg=%lluns max=%lluns finds=%llu\n",
                   (unsigned long long)n, (unsigned long long)(g_f1f0_total_ns.load() / n),
                   (unsigned long long)g_f1f0_max_ns.load(), (unsigned long long)g_name_finds.load()),
        std::fflush(stderr);
    return;
  }
  __imp__sub_8221F1F0(ctx, base);
}
extern "C" void __imp__sub_8221FA70(PPCContext& ctx, uint8_t* base);  // Scr_FindOrAddCanonicalName (r3 = char*)
REX_FUNC(sub_8221FA70) {
  if (env_on("COD4_NAMEPROBE")) {
    uint64_t n = g_name_finds.fetch_add(1) + 1;
    if ((n & 0x1FF) == 0) {                        // sample 1 in 512 to see the string patterns
      std::fprintf(stderr, "[COD4MP-NAMEPROBE] find#%llu name='%.48s'\n",
                   (unsigned long long)n, gstr(base, ctx.r3.u32)); std::fflush(stderr);
    }
  }
  __imp__sub_8221FA70(ctx, base);
}

// [COD4MP-MAXCLIENTS] sub_821FFC90 registers the sv_maxclients dvar (the local/splitscreen match
// defaults it to 4; its registered max is 24) and stashes the resulting dvar_t* at guest
// 0x82EE1D78. To run a full lobby of bots (e.g. 6v6 = 1 human + 11 bots) we force the current
// value to COD4_ADDBOTS+1 right after registration, BEFORE the server sizes svs.clients. The
// dvar_t int value is at +0x0C (big-endian guest). No env -> untouched (stays 4).
extern "C" void __imp__sub_821FFC90(PPCContext& ctx, uint8_t* base);
REX_FUNC(sub_821FFC90) {
  __imp__sub_821FFC90(ctx, base);
  const char* v = std::getenv("COD4_ADDBOTS");
  const char* mcenv = std::getenv("COD4_MAXCLIENTS");   // independent of our bot injection (for BW-managed bots)
  int want = mcenv ? (std::atoi(mcenv) - 1) : (v ? std::atoi(v) : 0);
  if (want > 0) {
    int mc = want + 1; if (mc < 4) mc = 4; if (mc > 24) mc = 24;
    uint32_t dptr = rd32(base, 0x82EE1D78u);          // sv_maxclients dvar_t*
    if (dptr > 0x10000u && dptr < 0x86000000u) {
      uint32_t be = __builtin_bswap32((uint32_t)mc);
      std::memcpy(base + dptr + 0x0C, &be, 4);        // current int value (guest BE)
      std::fprintf(stderr, "[COD4MP-MAXCLIENTS] forced sv_maxclients=%d (dvar@%08X)\n", mc, dptr);
      std::fflush(stderr);
    }
  }
}

// [COD4MP-LIVE] Fake the dead Xbox Live online-storage backend so signin=2 (SignedInToLive)
// boots. At signin=2 the Live init builds storage paths for motd/playlist/game-settings/stats via
// sub_8210AC38 = XStorageBuildServerPath: it calls XMsgInProcessCall(app=0xFC, msg=0x00058035,
// &argstruct, 0). That message is unimplemented (E_FAIL) so the wrapper maps it to error 1627 ->
// fatal "Error 1627 building storage path to the playlist file" at init. We replace the wrapper
// outright: write a dummy server path into the caller's output buffer (incoming r8; the buffer
// size lives at *r9, =256) and return 0 (success). Gated on COD4_LIVE so the default signin=1
// build is untouched. The downstream download (sub_821094B8) is the next wall — handled separately.
extern "C" void __imp__sub_8210AC38(PPCContext& ctx, uint8_t* base);
REX_FUNC(sub_8210AC38) {
  if (!env_on("COD4_LIVE")) { __imp__sub_8210AC38(ctx, base); return; }
  uint32_t outbuf = ctx.r8.u32;   // caller's output path buffer (arg7)
  uint32_t filety = ctx.r4.u32;   // storage file type (motd/playlist/settings/stats)
  if (outbuf > 0x10000u && outbuf < 0x90000000u) {
    char path[64];
    int n = std::snprintf(path, sizeof(path), "/cod4/storage/%u", (unsigned)filety);
    std::memcpy(base + outbuf, path, (size_t)n + 1);
  }
  std::fprintf(stderr, "[COD4MP-LIVE] BuildServerPath faked: type=%u outbuf=%08X -> 0\n",
               (unsigned)filety, outbuf);
  std::fflush(stderr);
  ctx.r3.u32 = 0;  // success; no XMsgInProcessCall, no 1627
}

// [COD4MP-LIVE] LSP (Live Server Platform) title-server enumeration. sub_821AB250 builds an
// enumerator then calls sub_821032F8 (= the XamEnumerate import thunk) at return addr 0x821AB344;
// it treats result 0 or 997(IO_PENDING) as success, anything else as fatal ("Xbox 360 Error 6
// while trying to enumerate lsp title servers"). On the dead Live backend the enumerator handle is
// bogus so XamEnumerate returns 6. sub_821032F8 is the SHARED XamEnumerate thunk (also used for
// content/device enumeration), so we only fake it for this exact caller: return 997 (pending) so
// the LSP path returns cleanly with zero servers. All other XamEnumerate callers pass through.
// NOTE: sub_821AB250 MUST still run (don't no-op it) — the Live-init state machine that clears the
// "Downloading game settings" modal depends on it executing. The 997 leaves the enumeration
// perpetually "pending", so the routine re-logs "Aborting redundant lsp enumeration" every frame;
// that spam is suppressed at its only sink (the [COD4MP-CONSOLE] echo) below.
extern "C" void __imp__sub_821032F8(PPCContext& ctx, uint8_t* base);
REX_FUNC(sub_821032F8) {
  if (env_on("COD4_LIVE") && (uint32_t)ctx.lr == 0x821AB344u) {
    log_once("[COD4MP-LIVE] LSP title-server XamEnumerate faked -> 997 (0 servers)");
    ctx.r3.u32 = 997;  // ERROR_IO_PENDING -> caller treats as OK
    return;
  }
  __imp__sub_821032F8(ctx, base);
}


// [COD4MP-SVPROBE] sub_82205CB8 = SV_ExecuteClientCommand(client_t* cl, char* cmd, int clientOK).
// Found empirically: the human's Choose-Team -> Auto-Assign sent "mr 16 4 autoassign" through it
// (mr = menuresponse short form). Commands not matching a server cmd (incl. "mr") fall through to
// ClientCommand(clientNum), where clientNum = (cl - svs.clients)/0xA2C08 and
// svs.clients = *(0x82F82D8C). Log every client command + its clientNum to capture the full join
// sequence (auto-assign + class). Gated COD4_SVPROBE. This is also the bot-join INJECTION point
// (see cod4_client_cmd above): call sub_82205CB8(svs.clients + N*0xA2C08, "<cmd>", 1) for bot N.
REX_FUNC(sub_82205CB8) {
  uint32_t cl = ctx.r3.u32, cmd = ctx.r4.u32, ok = ctx.r5.u32;
  uint32_t svc = rd32(base, 0x82F82D8Cu);
  int num = (svc >= 0x10000u && cl >= svc) ? (int)((cl - svc) / 0xA2C08u) : -1;
  const char* s = (cmd > 0x10000u && cmd < 0x90000000u) ? (const char*)(base + cmd) : "";
  // [COD4MP-BOTS] Arm the bot add+join sequence when the HUMAN (cl0) issues any team/class menu
  // response ("mr 16 ...") — i.e. is joining the match. Reliable trigger (connstate 0x82435780 reads
  // 0 in-game). Only the human's command arms it (num==0); bot-injected commands are num>=1.
  if (num == 0 && std::strncmp(s, "mr 16", 5) == 0 && g_bot_trigger.load() < 0) {
    g_bot_trigger.store(0);
  }
  if (env_on("COD4_SVPROBE")) {
    std::fprintf(stderr, "[COD4MP-SVPROBE] ExecClientCmd cl#%d (cl=%08X svc=%08X) ok=%u cmd='%.96s'\n",
                 num, cl, svc, ok, s);
    std::fflush(stderr);
  }
  __imp__sub_82205CB8(ctx, base);
}

// [COD4MP-SVPROBE] Trace the bot connect state machine: which clients reach SV_SendClientGameState
// (CONNECTED->CLIENTLOADING, sub_822052B0) and SV_ClientEnterWorld (CLIENTLOADING->CS_ACTIVE = spawn,
// sub_822042E0). Both take client_t* in r3; clientNum = (cl - svs.clients)/0xA2C08. If bots (cl>=1)
// don't reach EnterWorld, that's why they don't spawn. Gated COD4_SVPROBE.
static int cod4_clnum(uint8_t* base, uint32_t cl) {
  uint32_t svc = rd32(base, 0x82F82D8Cu);
  return (svc >= 0x10000u && cl >= svc) ? (int)((cl - svc) / 0xA2C08u) : -1;
}
extern "C" void __imp__sub_822052B0(PPCContext& ctx, uint8_t* base);
REX_FUNC(sub_822052B0) {
  if (env_on("COD4_SVPROBE"))
    std::fprintf(stderr, "[COD4MP-SVPROBE] SV_SendClientGameState cl#%d\n", cod4_clnum(base, ctx.r3.u32)), std::fflush(stderr);
  __imp__sub_822052B0(ctx, base);
}
extern "C" void __imp__sub_822042E0(PPCContext& ctx, uint8_t* base);
REX_FUNC(sub_822042E0) {
  if (env_on("COD4_SVPROBE"))
    std::fprintf(stderr, "[COD4MP-SVPROBE] SV_ClientEnterWorld cl#%d\n", cod4_clnum(base, ctx.r3.u32)), std::fflush(stderr);
  __imp__sub_822042E0(ctx, base);
}

// [COD4MP-BOTMOVE] Bot movement. The per-frame server bot path (statically traced):
//   SV_Frame(sub_821FEAC8) -> SV_RunFrame(sub_821FE7D0) -> SV_BotFrame(sub_821FE6D8)
//     -> SV_BotUserMove(sub_821FE3F8) -> SV_ClientThink(sub_82206078).
// SV_BotFrame loops svs.clients (stride 0xA2C08) and, for each client with state!=0 (CS_FREE
// excluded) AND cl+32==0 (no netchan => a bot/test client), calls SV_BotUserMove. SV_BotUserMove
// zeroes a 32-byte usercmd on its stack and, IF *(cl+0x21280)!=0 (a per-client entity ptr set by
// SV_ClientEnterWorld), fills it with RANDOM forward/right/buttons/angles and calls SV_ClientThink.
// usercmd layout (32 bytes): +0 serverTime, +4 buttons, +8/12/16 angles[3], +20 weapon,
// +22 forwardmove(signed char), +23 rightmove(signed char). SV_ClientThink(cl /*r3*/, cmd /*r4*/)
// copies the 32-byte cmd into cl+0x20E5C and, if cl->state(cl+0)==CS_ACTIVE(4), runs the client's
// movement think (sub_822842C8(clientNum)).
//
// Strategy: drive movement at SV_ClientThink. For a bot client (cl+32==0, clientNum>=1) we overwrite
// the incoming cmd's forwardmove (+22) so the bot walks instead of jittering randomly. This keeps ALL
// of the engine's serverTime/think bookkeeping intact (we only change movement intent). COD4_BOTMOVE
// is the forwardmove byte to force (e.g. 127 = full forward; default 127 if "1"); rightmove is zeroed.
// COD4_BOTPROBE logs the whole path (once/sec per client) to verify the gate + that ClientThink fires.
static bool throttle_1s(time_t* last) {
  time_t now = std::time(nullptr);
  if (now == *last) return false;
  *last = now; return true;
}
// [COD4MP-BOTAI] Diagnostic: count setplayerangles (handler sub_8227D360) calls on BOT entities
// (entnum = r3>>16 >= 1). BW's combat AI calls `self setplayerangles(...)` to aim; if this fires for
// bots, BW's per-bot AI loop is running (so any "bots don't move" is a botmoveto/waypoint issue, not an
// AI-not-running issue). Gated COD4_BOTDUMP.
extern "C" void __imp__sub_8227D360(PPCContext& ctx, uint8_t* base);  // setplayerangles
REX_FUNC(sub_8227D360) {
  if (env_on("COD4_BOTDUMP")) {
    int ent = (int)(ctx.r3.u32 >> 16);
    if (ent >= 1 && ent < 18) {
      static std::atomic<int> c{0}; int n = c.fetch_add(1);
      if (n < 4 || n % 300 == 0)
        std::fprintf(stderr, "[COD4MP-BOTAI] BW setplayerangles on bot cl#%d [#%d]\n", ent, n), std::fflush(stderr);
    }
  }
  __imp__sub_8227D360(ctx, base);
}
// [COD4MP-BOTPING] Bots are connectionless test clients, so SV_CalcPings (sub_821FC908) computes a
// garbage/999 ping for them -> "red bar" on the scoreboard + glitchy killcam (lag-comp has nothing real).
// Hook it: after the real calc, stamp a clean low ping into each active bot's ping field (client_t+0x803E0)
// so they show a good connection. Bot = state(cl+0)==4 && netchan(cl+32)==0. Env COD4_BOTPING (default 42).
extern "C" void __imp__sub_821FC908(PPCContext& ctx, uint8_t* base);
REX_FUNC(sub_821FC908) {
  __imp__sub_821FC908(ctx, base);
  if (env_on("COD4_BOTPINGFIX_OFF")) return;   // default ON now (validated stable; gives bots a clean ping)
  int ping = 42; const char* pv = std::getenv("COD4_BOTPING"); if (pv && pv[0]) ping = std::atoi(pv);
  uint32_t svc = rd32(base, 0x82F82D8Cu);
  uint32_t mcp = rd32(base, 0x82EE1D78u);
  int maxc = (mcp > 0x10000u) ? (int)rd32(base, mcp + 12) : 0;
  if (svc < 0x10000u) return;
  for (int i = 1; i < maxc && i < 24; i++) {
    uint32_t clp = svc + (uint32_t)i * 0xA2C08u;
    if (rd32(base, clp + 0) == 4 && rd32(base, clp + 32) == 0) {  // active bot
      uint32_t be = __builtin_bswap32((uint32_t)ping);
      std::memcpy(base + clp + 0x803E0u, &be, 4);
    }
  }
}
static uint32_t g_sv_frame = 0;     // monotonic per-server-frame counter (ticked in SV_BotFrame below)
extern "C" void __imp__sub_821FE6D8(PPCContext& ctx, uint8_t* base);  // SV_BotFrame
REX_FUNC(sub_821FE6D8) {
  ++g_sv_frame;   // once per server frame: freshness clock for BW's botmovement output
  if (env_on("COD4_BOTPROBE")) {
    static time_t last = 0;
    if (throttle_1s(&last)) { std::fprintf(stderr, "[COD4MP-BOTPROBE] SV_BotFrame tick\n"); std::fflush(stderr); }
  }
  __imp__sub_821FE6D8(ctx, base);
}
extern "C" void __imp__sub_821FE3F8(PPCContext& ctx, uint8_t* base);  // SV_BotUserMove
REX_FUNC(sub_821FE3F8) {
  if (env_on("COD4_BOTPROBE")) {
    static time_t last = 0;
    if (throttle_1s(&last)) {
      uint32_t cl = ctx.r3.u32;
      int num = cod4_clnum(base, cl);
      uint32_t state = rd32(base, cl + 0), netchan = rd32(base, cl + 32), gate = rd32(base, cl + 0x21280u);
      std::fprintf(stderr, "[COD4MP-BOTPROBE] SV_BotUserMove cl#%d state=%u netchan=%08X gate(+0x21280)=%08X %s\n",
                   num, state, netchan, gate, gate ? "(MOVES)" : "(early-return: stands still)");
      std::fflush(stderr);
    }
  }
  __imp__sub_821FE3F8(ctx, base);
}
// [COD4MP-GSCPROBE] Map the live GSC script-load sequence to plan Bot Warfare injection. The engine
// loads scripts BY NAME (Scr_LoadScript=sub_82220780, r3=name) and resolves GSC funcs BY NAME
// (Scr_GetFunctionHandle=sub_82220048, r3=scriptId r4=funcName). The bootstrap (sub_822634A0) loads the
// gametype + _callbacksetup and resolves the CodeCallback_* handles. Log all three (names as guest
// strings) so we can see exactly which scripts/functions flow through and design the source-inject hook.
// Gated COD4_GSCPROBE. Diagnostic only — passes everything through untouched.
// [COD4MP-BUILTIN] Keystone B: register a CUSTOM GSC builtin so injected GSC can call native code. The
// compiler resolves builtin call names via sub_82254D50(struct* r3, int* outType r4): r3[0] = name
// string; it chains category resolvers (functions sub_8227C690 / methods sub_82274F28 / ...) and returns
// the first nonzero handler address (which the VM later invokes via the indirect-call table). We hook it:
// for our custom name, return a guest thunk address (minted by FunctionDispatcher::AllocateThunk) that the
// recomp's indirect-call dispatch routes to our native PPCFunc. This is the mechanism for botmoveto/
// botaction/botstop. Proof builtin: `botwarproof()` -> logs from native. Gated COD4_GSCINJECT.
// Native bot-movement state (per clientNum; server thread only). botmoveto sets a world target; the
// per-frame SV_ClientThink hook drives the bot toward it (see [COD4MP-BOTMOVE] there).
static float g_bm_tx[24], g_bm_ty[24], g_bm_tz[24];
static int g_bm_active[24];
static uint32_t g_bm_buttons[24];   // per-bot usercmd button mask (set by botaction)
// [COD4MP-BOTMOVE] BW's full per-frame movement output (botmovement forward/right, already rotated into the
// usercmd's local convention by doBotMovement_loop). When fresh, this DRIVES the usercmd directly so BW
// owns navigation (path follow + combat strafe + sprint) instead of our straight-line botmoveto fallback.
static int8_t g_mv_fwd[24], g_mv_rgt[24];
static uint32_t g_mv_tick[24];      // server-frame at which botmovement was last set (freshness)
// g_sv_frame is declared earlier (above the SV_BotFrame hook that ticks it).

// [COD4MP-SCRPARAM] Read GSC-builtin parameters DIRECTLY from the VM param block instead of reentrantly
// calling the guest Scr_GetParam* helpers (which needed a ctx save/restore + a write into a guest scratch
// page — the latter a SIGBUS risk). Layout reverse-engineered from sub_8220D4C0 (Scr_GetType), sub_8220D6E8
// (Scr_GetVector) and sub_8220E640 (Scr_GetString):
//   descriptor @ 0x82E399A8:  +0x1c = numParams (u32),  +0x10 = param-stack top (guest VA).
//   param[i]:  entry = top - i*8;  entry+4 = type (1=int, 4=vector; string idx lives at +0 too);
//              entry+0 = value (int/float inline; vector = guest ptr to 3 BE floats; string = string index).
// All reads are bounds-checked against numParams, so a short-arg call returns the default rather than
// faulting. Valid only while a builtin is executing (the VM has the params pushed) — exactly our context.
static const uint32_t SCR_VMPARAM = 0x82E399A8u;
static bool scr_param_entry(uint8_t* base, int idx, uint32_t* entry) {
  int n = (int)rd32(base, SCR_VMPARAM + 0x1c);
  if (idx < 0 || idx >= n) return false;
  uint32_t top = rd32(base, SCR_VMPARAM + 0x10);
  if (top < 0x10000u || top >= 0x90000000u) return false;
  *entry = top - (uint32_t)idx * 8u;
  return true;
}
static int scr_get_int(uint8_t* base, int idx, int defv) {
  uint32_t e; if (!scr_param_entry(base, idx, &e)) return defv;
  uint32_t t = rd32(base, e + 4), v = rd32(base, e + 0);
  if (t == 6) return (int)v;                          // type 6 = integer (confirmed live; vector=4)
  if (t == 7) { float f; std::memcpy(&f, &v, 4); return (int)f; }  // type 7 = float
  return defv;
}
static bool scr_get_vector(uint8_t* base, int idx, float out[3]) {
  uint32_t e; if (!scr_param_entry(base, idx, &e)) return false;
  if (rd32(base, e + 4) != 4) return false;          // type 4 = vector
  uint32_t p = rd32(base, e + 0);
  if (p < 0x10000u || p >= 0x90000000u) return false;
  for (int k = 0; k < 3; k++) { uint32_t b = rd32(base, p + (uint32_t)k * 4u); std::memcpy(&out[k], &b, 4); }
  return true;
}
static const char* scr_get_string(uint8_t* base, int idx) {
  uint32_t e; if (!scr_param_entry(base, idx, &e)) return "";
  uint32_t si = rd32(base, e + 0);                   // string index (same value Scr_GetString returns)
  uint32_t pool = rd32(base, 0x82B8405Cu);
  uint32_t sp = pool + si * 12u + 4u;
  if (si && pool > 0x10000u && sp > 0x10000u) return (const char*)(base + sp);
  return "";
}

// `self botmoveto(where)`: self entity ref in r3 (entnum = r3>>16 = clientNum); read vec3 param 0 directly.
static void cod4_bm_botmoveto(PPCContext& ctx, uint8_t* base) {
  int ent = (int)(ctx.r3.u32 >> 16);
  if (ent < 0 || ent >= 24) return;
  float v[3];
  if (!scr_get_vector(base, 0, v)) return;    // bad/missing arg -> leave last target untouched
  g_bm_tx[ent] = v[0]; g_bm_ty[ent] = v[1]; g_bm_tz[ent] = v[2];
  g_bm_active[ent] = 1;
  if (env_on("COD4_BOTDUMP")) {
    static std::atomic<int> calls{0};
    int n = calls.fetch_add(1);
    if (n < 6 || n % 200 == 0)
      std::fprintf(stderr, "[COD4MP-BUILTIN] BW called botmoveto cl#%d -> (%.0f,%.0f,%.0f) [#%d]\n",
                   ent, g_bm_tx[ent], g_bm_ty[ent], g_bm_tz[ent], n), std::fflush(stderr);
  }
}
// `self botmovement(int forward, int right)`: BW's full per-frame movement output (path-follow + combat
// strafe + sprint), already rotated into the usercmd's local forward/right convention. We store it; the
// SV_ClientThink hook applies it directly (preferred over the botmoveto straight-line fallback).
static void cod4_bm_botmovement(PPCContext& ctx, uint8_t* base) {
  int ent = (int)(ctx.r3.u32 >> 16);
  if (ent < 0 || ent >= 24) return;
  int f = scr_get_int(base, 0, 0), r = scr_get_int(base, 1, 0);
  if (f > 127) f = 127; if (f < -127) f = -127;
  if (r > 127) r = 127; if (r < -127) r = -127;
  g_mv_fwd[ent] = (int8_t)f; g_mv_rgt[ent] = (int8_t)r;
  g_mv_tick[ent] = g_sv_frame;
  if (env_on("COD4_BOTDUMP")) {
    static std::atomic<int> calls{0};
    int n = calls.fetch_add(1);
    if (n < 12 || n % 200 == 0) {
      // RAW param probe: dump numParams + each entry's type id and raw u32, so we can confirm the int
      // decode (vector worked, ints read 0 -> suspect a wrong type id). Float bits printed too.
      int np = (int)rd32(base, SCR_VMPARAM + 0x1c);
      uint32_t e0 = 0, e1 = 0; bool h0 = scr_param_entry(base, 0, &e0), h1 = scr_param_entry(base, 1, &e1);
      uint32_t t0 = h0 ? rd32(base, e0 + 4) : 0xFFFF, v0 = h0 ? rd32(base, e0 + 0) : 0;
      uint32_t t1 = h1 ? rd32(base, e1 + 4) : 0xFFFF, v1 = h1 ? rd32(base, e1 + 0) : 0;
      float fv0, fv1; std::memcpy(&fv0,&v0,4); std::memcpy(&fv1,&v1,4);
      std::fprintf(stderr,
          "[COD4MP-BUILTIN] BW botmovement cl#%d fwd=%d right=%d [#%d] | np=%d p0(t=%u v=%d f=%.1f) p1(t=%u v=%d f=%.1f)\n",
          ent, f, r, n, np, t0, (int)v0, fv0, t1, (int)v1, fv1);
      std::fflush(stderr);
    }
  }
}
static void cod4_bm_botstop(PPCContext& ctx, uint8_t* base) {
  int ent = (int)(ctx.r3.u32 >> 16);
  if (ent >= 0 && ent < 24) {
    g_bm_active[ent] = 0; g_bm_buttons[ent] = 0;
    g_mv_fwd[ent] = 0; g_mv_rgt[ent] = 0; g_mv_tick[ent] = 0;
  }
}
// usercmd button bits (iw3). 0x1=attack confirmed (the engine's random SV_BotUserMove sets it). Others
// are best-effort and easy to tune. BW sends "+fire"/"-fire", "+ads", "+reload", "+melee", etc.
static const char* cod4_param_string(PPCContext& ctx, uint8_t* base, int idx) {
  (void)ctx;
  return scr_get_string(base, idx);
}
// `self botaction(action)`: action = "+x"/"-x" (press/release). Maps to a per-bot usercmd button mask
// (applied each frame in the SV_ClientThink hook, replacing the engine's random buttons).
static void cod4_bm_botaction(PPCContext& ctx, uint8_t* base) {
  int ent = (int)(ctx.r3.u32 >> 16);
  if (ent < 0 || ent >= 18) return;
  const char* a = cod4_param_string(ctx, base, 0);
  if (!a[0]) return;
  bool press = (a[0] == '+');
  const char* act = a + ((a[0] == '+' || a[0] == '-') ? 1 : 0);
  uint32_t bit = 0;
  if (!std::strcmp(act, "fire") || !std::strcmp(act, "attack")) bit = 0x1;     // attack (confirmed)
  else if (!std::strcmp(act, "ads")) bit = 0x800;                               // ADS/aim (confirmed: playerads ps+0xf4 -> 1.0)
  else if (!std::strcmp(act, "sprint")) bit = 0x2;                              // sprint (confirmed: 311 vs 187 ups)
  else if (!std::strcmp(act, "melee")) bit = 0x4;                               // melee (knife lunge speed-spikes to ~271)
  else if (!std::strcmp(act, "reload")) bit = 0x10;                             // reload (speed-neutral)
  // Stance/jump bits (confirmed live via COD4_BTNSWEEP, getstance field *(*(ent+0x15c)+0xc)):
  //   0x200 -> crouch, 0x100 -> prone, 0x400 -> raise/stand (BW's jump = a brief +gostand). These let bots
  //   crouch under / prone through / jump over obstacles on the waypoint graph = full nav traversal.
  else if (!std::strcmp(act, "gocrouch")) bit = 0x200;
  else if (!std::strcmp(act, "goprone")) bit = 0x100;
  else if (!std::strcmp(act, "gostand")) bit = 0x400;
  else return;                                                                  // others: ignore for now
  if (press) g_bm_buttons[ent] |= bit; else g_bm_buttons[ent] &= ~bit;
  if (env_on("COD4_BOTDUMP")) {
    static std::atomic<int> c{0}; int n = c.fetch_add(1);
    if (n < 8 || n % 100 == 0)
      std::fprintf(stderr, "[COD4MP-BUILTIN] BW botaction cl#%d '%s' -> buttons=%X [#%d]\n",
                   ent, a, g_bm_buttons[ent], n), std::fflush(stderr);
  }
}
// [COD4MP-WP] Current map name, captured at gametype-bootstrap time (see cod4_bm_setmap) so the waypoint
// CSV->GSC converter can pick the right bundled file. Declared here (before the builtin that writes it).
static char g_current_map[64] = "";
// `level cod4setmap(getdvar("mapname"))`: a VOID builtin (no GSC return value needed) injected at the top
// of the gametype bootstrap. It runs BEFORE BW compiles + requests _custom_map, so g_current_map is set in
// time for the converter to serve the real waypoints. Sidesteps needing Scr_AddString.
static void cod4_bm_setmap(PPCContext& ctx, uint8_t* base) {
  const char* m = cod4_param_string(ctx, base, 0);
  std::fprintf(stderr, "[COD4MP-WP] cod4setmap called, mapname='%s'\n", m ? m : "(null)");
  std::fflush(stderr);
  if (m && m[0]) {
    std::strncpy(g_current_map, m, sizeof(g_current_map) - 1);
    g_current_map[sizeof(g_current_map) - 1] = 0;
  }
}
// Custom GSC builtin registry: name -> native handler -> lazily-minted guest thunk (AllocateThunk).
struct Cod4Builtin { const char* name; PPCFunc* fn; uint32_t thunk; };
static Cod4Builtin g_cod4_builtins[] = {
    {"botmoveto", cod4_bm_botmoveto, 0},
    {"botmovement", cod4_bm_botmovement, 0},
    {"botstop", cod4_bm_botstop, 0},
    {"botaction", cod4_bm_botaction, 0},
    {"cod4setmap", cod4_bm_setmap, 0},
};
static uint32_t cod4_lookup_builtin(const char* nm) {
  for (auto& b : g_cod4_builtins) {
    if (std::strcmp(nm, b.name) != 0) continue;
    if (!b.thunk) {
      auto* ks = rex::runtime::current_kernel_state();
      if (ks && ks->function_dispatcher()) {
        b.thunk = ks->function_dispatcher()->AllocateThunk(b.fn, 0x820A0000u);
        std::fprintf(stderr, "[COD4MP-BUILTIN] registered '%s' -> thunk %08X\n", b.name, b.thunk);
        std::fflush(stderr);
      }
    }
    return b.thunk;
  }
  return 0;
}
extern "C" void __imp__sub_82254D50(PPCContext& ctx, uint8_t* base);  // ResolveBuiltinName(struct* r3, int* r4)
REX_FUNC(sub_82254D50) {
  if (env_on("COD4_GSCINJECT") || env_on("COD4_GSCPROBE")) {
    uint32_t p = ctx.r3.u32;
    uint32_t np = (p > 0x10000u) ? rd32(base, p) : 0;
    const char* nm = (np > 0x10000u) ? (const char*)(base + np) : "";
    if (env_on("COD4_GSCPROBE")) {
      static std::atomic<int> c{0}; int n = c.fetch_add(1);
      if (n < 60 || std::strstr(nm, "bot"))
        std::fprintf(stderr, "[COD4MP-RESOLVE] sub_82254D50 name='%.48s'\n", nm), std::fflush(stderr);
    }
    uint32_t t = cod4_lookup_builtin(nm);
    if (t) { ctx.r3.u32 = t; return; }   // return our native handler; skip the original table scan
  }
  __imp__sub_82254D50(ctx, base);
}

extern "C" void __imp__sub_82220780(PPCContext& ctx, uint8_t* base);  // Scr_LoadScript
REX_FUNC(sub_82220780) {
  const char* nm = gstr(base, ctx.r3.u32);
  if (env_on("COD4_GSCPROBE"))
    std::fprintf(stderr, "[COD4MP-GSCPROBE] Scr_LoadScript('%.96s')\n", nm), std::fflush(stderr);
  // [COD4MP-WP] Capture the current map name from the map-SCRIPT load here (not just the source-fetch hook
  // sub_8221EF90): the map GSC arrives as fastfile BYTECODE so it may skip the source hook entirely, and
  // BW can request _custom_map BEFORE it — a race that fell back to the sparse disk stub. Scr_LoadScript is
  // called by name for EVERY script incl. the map, reliably before _bot::init/load_waypoints. Basename of
  // "maps/mp|maps\mp\mp_<map>" that starts "mp_" and isn't the "_fx" variant.
  if (env_on("COD4_GSCINJECT") && nm && nm[0]) {
    const char* b = nm;
    for (const char* p = nm; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;  // basename
    size_t bl = std::strlen(b);
    if (bl > 3 && std::strncmp(b, "mp_", 3) == 0 && !(bl >= 3 && std::strcmp(b + bl - 3, "_fx") == 0)) {
      if (std::strcmp(g_current_map, b) != 0) {
        std::strncpy(g_current_map, b, sizeof(g_current_map) - 1);
        g_current_map[sizeof(g_current_map) - 1] = 0;
        std::fprintf(stderr, "[COD4MP-WP] current map = '%s' (from Scr_LoadScript)\n", g_current_map);
        std::fflush(stderr);
      }
    }
  }
  __imp__sub_82220780(ctx, base);
}
extern "C" void __imp__sub_82220048(PPCContext& ctx, uint8_t* base);  // Scr_GetFunctionHandle
REX_FUNC(sub_82220048) {
  if (env_on("COD4_GSCPROBE"))
    std::fprintf(stderr, "[COD4MP-GSCPROBE] Scr_GetFunctionHandle(script=%08X, '%.64s')\n",
                 (unsigned)ctx.r3.u32, gstr(base, ctx.r4.u32)), std::fflush(stderr);
  __imp__sub_82220048(ctx, base);
}
// [COD4MP-GSCINJECT] Bot Warfare integration. sub_8221EF90(char* scriptName /*r3*/, ...) -> char*
// sourceText (NUL-terminated; guest VA; 0 = not found). Two jobs (gated COD4_GSCINJECT):
//   (1) SERVE NEW/OVERRIDE scripts from disk: if <COD4_GSCDIR>/<name>.gsc exists (default repo
//       gsc_inject/), return its contents — this loads iw3_bot_warfare's files (and our adapter) when
//       referenced. New names the fastfile lacks (e.g. maps/mp/bots/_bot) are auto-fetched once any
//       loaded script references them (maps\mp\bots\_bot::init()).
//   (2) BOOTSTRAP: patch war.gsc onStartGameType() to `thread maps\mp\bots\_bot::init();` so the bot
//       brain starts, plus a banner thread as a compiled-OK signal.
// Buffers are staged via a bump allocator in the free, writable, linearly-mapped guest gap at
// 0xA0000000 (between image end 0x853D0000 and the script zone ~0xB0000000).
static uint32_t g_gsc_bump = 0xA0000000u;
static uint32_t gsc_stage(uint8_t* base, const char* data, size_t n) {
  uint32_t va = g_gsc_bump;
  if (va + n + 1 > 0xAF000000u) return 0;            // out of staging room
  std::memcpy(base + va, data, n);
  base[va + n] = 0;
  g_gsc_bump = (va + (uint32_t)n + 1 + 15u) & ~15u;  // 16-byte aligned bump
  return va;
}
static const char* gsc_dir() {
  const char* d = std::getenv("COD4_GSCDIR");
  return (d && d[0]) ? d : "/home/dlynch/dev/mw-recomp-mp/gsc_inject";
}
// Read <gsc_dir>/<name>.gsc into buf; returns length or -1 if missing.
static long gsc_read_disk(const char* name, char* buf, size_t cap) {
  char path[512];
  std::snprintf(path, sizeof(path), "%s/%s.gsc", gsc_dir(), name);
  FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  size_t n = std::fread(buf, 1, cap - 1, f);
  std::fclose(f);
  buf[n] = 0;
  return (long)n;
}
// [COD4MP-WP] Bundled BW waypoints. Bot Warfare ships hand-made scriptdata/waypoints/<map>_wp.csv for
// Backlot + all stock maps. Rather than implement BW's fs_* file API (which needs the GSC return-value
// builtins Scr_AddString etc.), we convert the CSV to _custom_map.gsc NATIVELY, keyed to the current map
// (g_current_map, captured from the map-script load). Works for EVERY bundled map, automatically.
// CSV: line0 = count; then per waypoint "x y z,childA childB ...,type,...". g_current_map is set by the
// cod4setmap builtin at bootstrap time (declared earlier in this file).
static const char* wp_dir() {
  const char* d = std::getenv("COD4_WPDIR");
  return (d && d[0]) ? d : "/home/dlynch/dev/mw-recomp-mp/iw3_bot_warfare/scriptdata/waypoints";
}
static long wp_csv_to_gsc(const char* map, char* out, size_t cap) {
  if (!map[0]) return -1;
  char path[512]; std::snprintf(path, sizeof(path), "%s/%s_wp.csv", wp_dir(), map);
  FILE* f = std::fopen(path, "rb"); if (!f) return -1;
  static char csv[1 << 20];
  size_t clen = std::fread(csv, 1, sizeof(csv) - 1, f); std::fclose(f); csv[clen] = 0;
  char* cur = csv;
  long count = std::strtol(cur, &cur, 10);
  if (count <= 0 || count > 4096) return -1;
  cur = std::strchr(csv, '\n'); if (!cur) return -1; cur++;
  size_t o = 0;
  auto put = [&](const char* s) { size_t l = std::strlen(s); if (o + l < cap) { std::memcpy(out + o, s, l); o += l; } };
  char buf[512];
  const int per = 10; int nch = (int)((count + per - 1) / per);
  put("main( mapname )\n{\n\tlevel.waypoints = [];\n");
  for (int c = 0; c < nch; c++) { std::snprintf(buf, sizeof(buf), "\taddwp_%d();\n", c); put(buf); }
  put("}\n");
  for (int c = 0; c < nch; c++) {
    std::snprintf(buf, sizeof(buf), "addwp_%d()\n{\n", c); put(buf);
    for (int k = 0; k < per; k++) {
      int i = c * per + k; if (i >= count) break;
      char line[512]; char* nl = std::strchr(cur, '\n');
      size_t ll = nl ? (size_t)(nl - cur) : std::strlen(cur);
      if (ll >= sizeof(line)) ll = sizeof(line) - 1;
      std::memcpy(line, cur, ll); line[ll] = 0; cur = nl ? nl + 1 : cur + ll;
      char* fields[6] = {0}; int nf = 0;
      for (char* tk = line; nf < 6;) { fields[nf++] = tk; char* cm = std::strchr(tk, ','); if (!cm) break; *cm = 0; tk = cm + 1; }
      float x = 0, y = 0, z = 0;
      if (nf > 0) std::sscanf(fields[0], "%f %f %f", &x, &y, &z);
      const char* type = (nf > 2 && fields[2] && fields[2][0]) ? fields[2] : "stand";
      std::snprintf(buf, sizeof(buf),
          "\twp = spawnstruct();\n\twp.origin = ( %.1f, %.1f, %.1f );\n\twp.type = \"%s\";\n\twp.children = [];\n",
          x, y, z, type);
      put(buf);
      if (nf > 1 && fields[1] && fields[1][0]) {
        int ci = 0;
        for (char* tk = std::strtok(fields[1], " "); tk; tk = std::strtok(nullptr, " ")) {
          std::snprintf(buf, sizeof(buf), "\twp.children[%d] = %d;\n", ci++, std::atoi(tk)); put(buf);
        }
      }
      std::snprintf(buf, sizeof(buf), "\tlevel.waypoints[%d] = wp;\n", i); put(buf);
    }
    put("}\n");
  }
  put("doTheCheck_()\n{\n}\n");   // BW calls _custom_map::doTheCheck_ (credit print) — stub it
  if (o >= cap) o = cap - 1;
  out[o] = 0;
  return (long)o;
}
// [COD4MP-WP] Deterministically resolve the current map from the engine's "mapname" dvar. The map name is
// set at SERVER SPAWN, before GSC compiles — so this works even though BW fetches _custom_map before the map
// script loads (a compile-order race the name-capture hooks can lose). sub_821CF6D0(name,default,flags,desc)
// is the dvar register/lookup (RE'd from sub_82263428, which loads the map script the same way) → dvar_t* in
// r3; the live string value is at *(dvar+0xc). Identical args make an already-registered dvar a no-op return.
// One-shot at fetch time (not per-frame); a normal guest call (no VM-param stack involved).
extern "C" void __imp__sub_821CF6D0(PPCContext& ctx, uint8_t* base);
static void cod4_capture_mapname_from_dvar(PPCContext& ctx, uint8_t* base) {
  PPCContext save = ctx;
  ctx.r3.u32 = 0x8204E4D0u;   // "mapname"
  ctx.r4.u32 = 0x8204B37Bu;   // default string (engine's arg)
  ctx.r5.u32 = 0x44u;         // flags
  ctx.r6.u32 = 0x82061BB4u;   // description
  __imp__sub_821CF6D0(ctx, base);
  uint32_t dvar = ctx.r3.u32;
  ctx = save;
  if (dvar <= 0x10000u) return;
  uint32_t sva = rd32(base, dvar + 0xc);
  if (sva <= 0x10000u || sva >= 0x90000000u) return;
  const char* m = (const char*)(base + sva);
  if (m[0]) {
    std::strncpy(g_current_map, m, sizeof(g_current_map) - 1);
    g_current_map[sizeof(g_current_map) - 1] = 0;
    std::fprintf(stderr, "[COD4MP-WP] current map = '%s' (from mapname dvar)\n", g_current_map);
    std::fflush(stderr);
  }
}
extern "C" void __imp__sub_8221EF90(PPCContext& ctx, uint8_t* base);
REX_FUNC(sub_8221EF90) {
  char name[128] = {0};
  bool probe = env_on("COD4_GSCPROBE"), inject = env_on("COD4_GSCINJECT");
  if (probe || inject) std::strncpy(name, gstr(base, ctx.r3.u32), sizeof(name) - 1);

  // Capture the current map from the map script ("maps/mp/mp_<map>", excluding the "_fx" variant) so we
  // can pick the right bundled waypoint CSV for _custom_map below.
  if (inject && std::strncmp(name, "maps/mp/mp_", 11) == 0) {
    const char* m = name + 8;                       // "mp_<map>"
    size_t ml = std::strlen(m);
    if (!(ml >= 3 && std::strcmp(m + ml - 3, "_fx") == 0))
      std::strncpy(g_current_map, m, sizeof(g_current_map) - 1);
  }

  // (1a) _custom_map: serve BW's bundled waypoints for the current map, converted from CSV -> GSC.
  if (inject && std::strstr(name, "bots/waypoints/_custom_map")) {
    if (!g_current_map[0]) cod4_capture_mapname_from_dvar(ctx, base);   // deterministic fallback
   if (g_current_map[0]) {
    static char wpgsc[262144];
    long n = wp_csv_to_gsc(g_current_map, wpgsc, sizeof(wpgsc));
    if (n > 0) {
      uint32_t va = gsc_stage(base, wpgsc, (size_t)n);
      if (va) {
        std::fprintf(stderr, "[COD4MP-GSCINJECT] waypoints '%s' from CSV (%ld bytes) -> %08X\n", g_current_map, n, va);
        std::fflush(stderr);
        ctx.r3.u32 = va;
        return;
      }
    }
    // no CSV for this map -> fall through to disk-serve / FrontLines fallback
   }
  }

  // (1) Disk-serve BEFORE the original: lets us provide scripts the fastfile lacks (the original would
  // return 0) and override existing ones. Only when a matching .gsc file exists on disk.
  if (inject && name[0]) {
    static char disk[262144];
    long n = gsc_read_disk(name, disk, sizeof(disk));
    if (n >= 0) {
      uint32_t va = gsc_stage(base, disk, (size_t)n);
      if (va) {
        std::fprintf(stderr, "[COD4MP-GSCINJECT] disk-served '%s' (%ld bytes) -> %08X\n", name, n, va);
        std::fflush(stderr);
        ctx.r3.u32 = va;
        return;   // skip original; our source wins
      }
    }
  }

  __imp__sub_8221EF90(ctx, base);
  uint32_t src = ctx.r3.u32;
  // The ACTIVE gametype script is "maps/mp/gametypes/<mode>" where <mode> = war/dom/sd/sab/koth/dm/hq/...
  // (the system scripts under gametypes/ all start with '_': _globallogic, _teams, _callbacksetup, ...).
  // Bootstrap BW from whichever gametype loads, so ALL game modes get bots (not just war/TDM).
  bool is_gametype = false;
  if (name[0] && src > 0x10000u) {
    const char* gt = std::strstr(name, "gametypes/");
    if (gt && gt[10] && gt[10] != '_') is_gametype = true;
  }
  if (probe && is_gametype) {
    const char* p = (const char*)(base + src);
    size_t len = 0; while (len < 262144 && p[len]) len++;
    std::fprintf(stderr, "[COD4MP-GSCPROBE] gametype '%s' src=%08X len=%zu\n", name, src, len);
    std::fflush(stderr);
  }
  // (2) Bootstrap: patch the gametype's onStartGameType() to start Bot Warfare with N bots (env COD4_BOTS,
  // default 11). bots_manage_add adds exactly N bots once.
  if (inject && is_gametype) {
    const char* p = (const char*)(base + src);
    size_t len = 0; while (len < 262144 && p[len]) len++;
    size_t brace = 0;
    for (size_t i = 0; i + 16 <= len && !brace; i++)
      if (std::memcmp(p + i, "onStartGameType(", 16) == 0)
        for (size_t j = i; j < len; j++) if (p[j] == '{') { brace = j + 1; break; }
    if (brace) {
      int nbots = 11; const char* nb = std::getenv("COD4_BOTS"); if (nb && nb[0]) nbots = std::atoi(nb);
      char skill[80] = "";   // optional bots_skill (1=easy..7=hard, 8=custom, 9=fully random); env COD4_BOTSKILL
      const char* sk = std::getenv("COD4_BOTSKILL");
      if (sk && sk[0]) std::snprintf(skill, sizeof(skill), "\tsetdvar( \"bots_skill\", %d );\n", std::atoi(sk));

      // [COD4MP-DIET] Per-bot thread trim to fit more bots under the 0x8000 GSC-variable wall (see memory).
      // BW gates many per-bot AI threads behind bots_play_* dvars; we disable the heaviest non-core ones so
      // each bot registers fewer endon/threads. KEEP the humanizing core: move, aim, weapon/reload, revenge,
      // uav awareness, listen-to-steps, follow-target (always-on, no dvar). Each is env-overridable.
      //   bots_play_obj  : the 7 objective threads (dom/hq/sab/sd) — useless on non-obj gametypes, so we
      //                    enable it ONLY for objective gametypes. Biggest single saving on war/dm.
      //   bots_play_target_other : vehicle/equipment targeting — little value in CoD4, off by default.
      //   bots_play_killstreak   : killstreak management — heavy, off by default (COD4_BOTKILLSTREAK=1 to keep).
      //   bots_play_nade / bots_play_camp : humanizing — KEPT on (toggle off via env if more headroom needed).
      const char* gt2 = std::strstr(name, "gametypes/");
      const char* gn = gt2 ? gt2 + 10 : "";
      bool is_obj = gn[0] && (
          !std::strncmp(gn, "dom", 3) || !std::strncmp(gn, "sd", 2)  || !std::strncmp(gn, "sab", 3) ||
          !std::strncmp(gn, "koth", 4)|| !std::strncmp(gn, "hq", 2)  || !std::strncmp(gn, "ctf", 3) ||
          !std::strncmp(gn, "dd", 2)  || !std::strncmp(gn, "oneflag", 7));
      auto envd = [](const char* k, int dflt) { const char* v = std::getenv(k); return (v && v[0]) ? std::atoi(v) : dflt; };
      int play_obj   = envd("COD4_BOTOBJ", is_obj ? 1 : 0);
      int play_other = envd("COD4_BOTVEHICLE", 0);
      int play_ks    = envd("COD4_BOTKILLSTREAK", 0);
      int play_nade  = envd("COD4_BOTNADE", 1);
      int play_camp  = envd("COD4_BOTCAMP", 1);

      char boot[768];
      int bn = std::snprintf(boot, sizeof(boot),
          "\n\tsetdvar( \"bots_manage_fill\", %d );\n"   // fill to (bots + 1 host) total
          "\tsetdvar( \"bots_team_force\", 1 );\n"
          "\tsetdvar( \"bots_play_obj\", %d );\n"
          "\tsetdvar( \"bots_play_target_other\", %d );\n"
          "\tsetdvar( \"bots_play_killstreak\", %d );\n"
          "\tsetdvar( \"bots_play_nade\", %d );\n"
          "\tsetdvar( \"bots_play_camp\", %d );\n"
          "%s"
          "\tthread scripts\\mp\\bots_adapter_cod4x::init();\n"
          "\tthread scripts\\mp\\bots::init();\n",
          nbots + 1, play_obj, play_other, play_ks, play_nade, play_camp, skill);
      static char out[262144 + 1024];
      size_t o = 0;
      std::memcpy(out + o, p, brace); o += brace;
      std::memcpy(out + o, boot, (size_t)bn); o += (size_t)bn;
      std::memcpy(out + o, p + brace, len - brace); o += (len - brace);
      uint32_t va = gsc_stage(base, out, o);
      if (va) {
        ctx.r3.u32 = va;
        std::fprintf(stderr, "[COD4MP-GSCINJECT] gametype '%s' bootstrapped (%zu -> %zu bytes) -> %08X\n", name, len, o, va);
        std::fflush(stderr);
      }
    } else {
      std::fprintf(stderr, "[COD4MP-GSCINJECT] FAILED to locate onStartGameType brace\n");
      std::fflush(stderr);
    }
  }
}
extern "C" void __imp__sub_822634A0(PPCContext& ctx, uint8_t* base);  // GSC bootstrap (StartGameType setup)
REX_FUNC(sub_822634A0) {
  if (env_on("COD4_GSCPROBE")) { log_once("[COD4MP-GSCPROBE] GSC bootstrap sub_822634A0 entered"); }
  __imp__sub_822634A0(ctx, base);
  if (env_on("COD4_GSCPROBE")) { std::fprintf(stderr, "[COD4MP-GSCPROBE] GSC bootstrap returned\n"); std::fflush(stderr); }
}

extern "C" void __imp__sub_82206078(PPCContext& ctx, uint8_t* base);  // SV_ClientThink
REX_FUNC(sub_82206078) {
  uint32_t cl = ctx.r3.u32, cmd = ctx.r4.u32;
  int num = cod4_clnum(base, cl);
  uint32_t netchan = rd32(base, cl + 32);
  // [COD4MP-BOTCMDTIME] The killcam for a bot killer WAS degenerate (frozen 0:00.0 timer, red-tinted, no
  // replay) because bots' usercmd serverTime is ALWAYS 0 (confirmed: botCmdTime=0 vs host's real clock), so
  // the kill timestamp is 0 and the killcam replay window collapses to zero duration. FIX (default ON,
  // user-validated "killcams look good"): cache the human host's usercmd serverTime (cmd+0, BE) and stamp it
  // into each bot's usercmd so bots ride the real server timeline. The engine clamps the first-frame jump.
  static std::atomic<uint32_t> g_host_stime{0};
  if (cmd > 0x10000u && cmd < 0x90000000u) {
    uint32_t st = rd32(base, cmd + 0);
    if (num == 0) {
      g_host_stime.store(st);
    } else if (num >= 1 && netchan == 0) {
      uint32_t host = g_host_stime.load();
      if (env_on("COD4_BOTPROBE")) {
        static time_t last = 0;
        if (throttle_1s(&last))
          std::fprintf(stderr, "[COD4MP-BOTCMDTIME] cl#%d botCmdTime=%u hostCmdTime=%u delta=%d\n",
                       num, st, host, (int)(st - host)), std::fflush(stderr);
      }
      if (host && !env_on("COD4_BOTCMDTIME_OFF")) {   // default ON (validated: fixes the killcam)
        uint32_t be = __builtin_bswap32(host);
        std::memcpy(base + cmd + 0, &be, 4);   // stamp the real server time into the bot's usercmd
      }
    }
  }
  // [COD4MP-WPREC] Waypoint sampler: record every live player's world position (they trace the walkable
  // corridors) to a host CSV, ~4x/sec/client. Post-processed into a BW waypoint graph injected via
  // _custom_map. Gated COD4_WPREC. (Host file I/O is free in the recomp's C++ layer.)
  if (env_on("COD4_WPREC") && num >= 0 && num < 24) {
    static FILE* wf = nullptr;
    static int tick[18] = {0};
    if (!wf) wf = std::fopen("/tmp/wp_samples.csv", "w");
    if (wf && (++tick[num] % 15) == 0) {            // ~4/sec at 60fps
      uint32_t ent = rd32(base, cl + 0x21280u);
      if (ent > 0x10000u) {
        uint32_t ux = rd32(base, ent + 0x18), uy = rd32(base, ent + 0x1C), uz = rd32(base, ent + 0x20);
        float x, y, z; std::memcpy(&x,&ux,4); std::memcpy(&y,&uy,4); std::memcpy(&z,&uz,4);
        if (x != 0.0f || y != 0.0f) { std::fprintf(wf, "%d,%.1f,%.1f,%.1f\n", num, x, y, z); std::fflush(wf); }
      }
    }
  }
  // [COD4MP-BOTDUMP] team distribution for ALL clients (incl. human cl#0) to verify opposing teams.
  if (env_on("COD4_BOTDUMP") && num >= 0 && num < 16) {
    static time_t tlast[16] = {0};
    if (throttle_1s(&tlast[num])) {
      uint32_t ent = rd32(base, cl + 0x21280u);
      if (ent > 0x10000u) {
        uint16_t tb; std::memcpy(&tb, base + ent + 368, 2);
        std::fprintf(stderr, "[COD4MP-TEAM] cl#%d team=%u\n", num, __builtin_bswap16(tb));
        std::fflush(stderr);
      }
    }
  }
  bool is_bot = (num >= 1) && (netchan == 0);
  if (is_bot && cmd > 0x10000u && cmd < 0x90000000u) {
    if (env_on("COD4_BOTPROBE")) {
      static time_t last = 0;
      if (throttle_1s(&last)) {
        int8_t fwd = (int8_t)base[cmd + 22], rgt = (int8_t)base[cmd + 23];
        std::fprintf(stderr, "[COD4MP-BOTPROBE] SV_ClientThink cl#%d state=%u fwd=%d right=%d\n",
                     num, rd32(base, cl + 0), fwd, rgt);
        std::fflush(stderr);
      }
    }
    // [COD4MP-BOTDUMP] find the bot's world origin: dump plausible-looking floats from the entity
    // pointer (*(cl+0x21280), set by SV_ClientEnterWorld) so we can identify origin[3] and confirm
    // it changes with forwardmove. Once/sec per the shared throttle. Gated COD4_BOTDUMP.
    if (env_on("COD4_BOTDUMP")) {
      static time_t last[16] = {0};
      if (num >= 0 && num < 16 && throttle_1s(&last[num])) {
        uint32_t ent = rd32(base, cl + 0x21280u);
        if (ent > 0x10000u && ent < 0x90000000u) {
          uint32_t ox = rd32(base, ent + 0x18), oy = rd32(base, ent + 0x1C), oz = rd32(base, ent + 0x20);
          uint32_t a0 = rd32(base, ent + 0x3C), a1 = rd32(base, ent + 0x40), a2 = rd32(base, ent + 0x44);
          float fx, fy, fz, fa0, fa1, fa2;
          std::memcpy(&fx,&ox,4); std::memcpy(&fy,&oy,4); std::memcpy(&fz,&oz,4);
          std::memcpy(&fa0,&a0,4); std::memcpy(&fa1,&a1,4); std::memcpy(&fa2,&a2,4);
          // team = *(u16)(ent+368) (setteam compares it to the allies/axis team-id constants); health = ent+0x174?
          uint16_t teambe; std::memcpy(&teambe, base + ent + 368, 2);
          uint16_t team = __builtin_bswap16(teambe);
          // stance (getstance field): bit0=prone, bit1=crouch, else stand — confirms bots use the new buttons.
          uint32_t ps = rd32(base, ent + 0x15Cu);
          uint32_t sf = (ps > 0x10000u && ps < 0x90000000u) ? rd32(base, ps + 0xCu) : 0;
          const char* st = (sf & 1u) ? "prone" : ((sf & 2u) ? "crouch" : "stand");
          float spd = 0.0f;   // horizontal speed (getvelocity = ps+0x28); >~250 = sprinting
          float ads = 0.0f;   // ADS fraction (playerads = ps+0xf4); >0 = aiming down sights
          if (ps > 0x10000u && ps < 0x90000000u) {
            uint32_t vx = rd32(base, ps + 0x28u), vy = rd32(base, ps + 0x2Cu), av = rd32(base, ps + 0xF4u);
            float fvx, fvy; std::memcpy(&fvx, &vx, 4); std::memcpy(&fvy, &vy, 4); std::memcpy(&ads, &av, 4);
            spd = std::sqrt(fvx * fvx + fvy * fvy);
          }
          std::fprintf(stderr, "[COD4MP-BOTDUMP] cl#%d team=%u stance=%s spd=%.0f ads=%.2f origin=(%.1f,%.1f,%.1f) entAng=(%.1f,%.1f,%.1f)\n",
                       num, team, st, spd, ads, fx, fy, fz, fa0, fa1, fa2);
          std::fflush(stderr);
        }
      }
    }
    // [COD4MP-BOTMOVE] Debug: force a fixed forwardmove (COD4_BOTMOVE=<byte>). Overrides everything below.
    const char* mv = std::getenv("COD4_BOTMOVE");
    if (mv && mv[0] && mv[0] != '0') {
      int f = std::atoi(mv); if (f == 1) f = 127; if (f > 127) f = 127; if (f < -127) f = -127;
      base[cmd + 22] = (uint8_t)(int8_t)f;   // forwardmove
      base[cmd + 23] = 0;                    // rightmove
    } else if (env_on("COD4_BOTAI") && num >= 0 && num < 24) {
      // Bot Warfare mode. Always kill the engine's random SV_BotUserMove view jitter (cmd.angles=0 ->
      // view = ps.delta_angles, which BW controls via setplayerangles) so bots don't spaz, and replace
      // the random buttons with BW's botaction mask (so bots fire only when BW targets an enemy, instead
      // of mashing buttons randomly + wasting ammo). Then drive movement toward the botmoveto target.
      std::memset(base + cmd + 8, 0, 12);        // zero cmd.angles[3]
      uint32_t btn = g_bm_buttons[num];
      if (env_on("COD4_BOTFIRE")) btn |= 0x1;    // debug: force the attack bit to verify firing
      uint32_t be_btn = __builtin_bswap32(btn);
      std::memcpy(base + cmd + 4, &be_btn, 4);   // usercmd.buttons = BW's botaction mask
      // Movement: prefer BW's botmovement output (full nav: path-follow + combat strafe + sprint, already
      // in usercmd local convention) when it was set this frame. Fall back to the straight-line botmoveto
      // derivation only when BW didn't drive botmovement (older adapter path / between BW think ticks).
      // Prefer BW's botmovement output only when it's fresh AND nonzero — BW emits (0,0) both for "stop"
      // and (on this engine) for most frames, so blindly trusting it freezes bots. A nonzero value is a
      // real move command (sprint/strafe) we honor; otherwise fall back to the live botmoveto derivation
      // (recomputed from current origin/yaw every server frame — smoother for plain path-following).
      bool mv_fresh = (g_mv_tick[num] != 0) && (g_sv_frame - g_mv_tick[num] <= 2) &&
                      (g_mv_fwd[num] != 0 || g_mv_rgt[num] != 0);
      if (mv_fresh) {
        base[cmd + 22] = (uint8_t)g_mv_fwd[num];
        base[cmd + 23] = (uint8_t)g_mv_rgt[num];
      } else if (g_bm_active[num]) {
        uint32_t ent = rd32(base, cl + 0x21280u);
        if (ent > 0x10000u) {
          uint32_t ux = rd32(base, ent + 0x18), uy = rd32(base, ent + 0x1C), uyaw = rd32(base, ent + 0x40);
          float ox, oy, yaw; std::memcpy(&ox,&ux,4); std::memcpy(&oy,&uy,4); std::memcpy(&yaw,&uyaw,4);
          float dx = g_bm_tx[num] - ox, dy = g_bm_ty[num] - oy;
          float dist = std::sqrt(dx * dx + dy * dy);
          if (dist < 16.0f) { base[cmd + 22] = 0; base[cmd + 23] = 0; }
          else {
            float r = yaw * 0.01745329252f;       // deg->rad
            float cf = std::cos(r), sf = std::sin(r);
            float fl = (dx * cf + dy * sf) / dist; // forward component (dir . forward)
            float rl = (dx * sf - dy * cf) / dist; // right component   (dir . right=(sin,-cos))
            int fm = (int)(fl * 127.0f), rm = (int)(rl * 127.0f);
            if (fm > 127) fm = 127; if (fm < -127) fm = -127;
            if (rm > 127) rm = 127; if (rm < -127) rm = -127;
            base[cmd + 22] = (uint8_t)(int8_t)fm;
            base[cmd + 23] = (uint8_t)(int8_t)rm;
          }
        }
      } else {
        base[cmd + 22] = 0; base[cmd + 23] = 0;    // no BW target -> stand still (not spaz)
      }
    }
    // [COD4MP-BTNSWEEP] Stance/jump button-bit finder. For probe bot cl#1 only: freeze movement + view, and
    // force exactly ONE candidate usercmd.button bit, cycled by wall-clock (~2s each: slot0=none baseline,
    // then 0x1,0x2,...0x8000). Log the resulting stance flags — getstance reads *(*(ent+0x15c)+0xc): bit0 &
    // bit1 select crouch/prone (else stand) — plus origin Z (a jump shows as a Z spike). One run reveals
    // which button bit drives crouch/prone (held → stance flag stays set). Gated COD4_BTNSWEEP.
    // Only sweep when cl#1 is fully CS_ACTIVE with a valid entity — forcing buttons / reading ps during the
    // spawn transient is what crashed an earlier run.
    if (env_on("COD4_BTNSWEEP") && num == 1 && rd32(base, cl + 0) == 4) {
      uint32_t ent = rd32(base, cl + 0x21280u);
      uint32_t ps = (ent > 0x10000u && ent < 0x90000000u) ? rd32(base, ent + 0x15Cu) : 0;
      if (ps > 0x10000u && ps < 0x90000000u) {
        // Hold each candidate bit 3s; resolve the stance NAME as getstance does (sub_8225A4D0): table
        // @0x82A21808, GSC string idx at +0x62(bit0)/+0x18(bit1)/+0x7e(else) → pool *(0x82B8405C)+idx*12+4.
        static const uint32_t cand[] = {0u, 0x100u, 0x200u, 0x400u, 0x800u, 0x1000u, 0x40u, 0x80u};
        static time_t t0 = 0; if (!t0) t0 = std::time(nullptr);
        int slot = (int)((std::time(nullptr) - t0) / 3) % (int)(sizeof(cand) / sizeof(cand[0]));
        uint32_t bit = cand[slot];
        uint32_t be = __builtin_bswap32(bit);
        std::memcpy(base + cmd + 4, &be, 4);
        base[cmd + 22] = 0; base[cmd + 23] = 0;
        std::memset(base + cmd + 8, 0, 12);
        static time_t last = 0;
        if (throttle_1s(&last)) {
          uint32_t sf = rd32(base, ps + 0xCu);
          uint32_t uz = rd32(base, ent + 0x20u); float z; std::memcpy(&z, &uz, 4);
          uint32_t off = (sf & 1u) ? 0x62u : ((sf & 2u) ? 0x18u : 0x7Eu);
          uint16_t sidx = rd16be(base, 0x82A21808u + off);
          uint32_t pool = rd32(base, 0x82B8405Cu);
          uint32_t sva = pool + (uint32_t)sidx * 12u + 4u;
          const char* sstr = (pool > 0x10000u && pool < 0x90000000u && sidx) ? (const char*)(base + sva) : "?";
          std::fprintf(stderr, "[COD4MP-BTNSWEEP] bit=0x%04X stance='%s' (b0=%u b1=%u flags=0x%X) z=%.1f\n",
                       bit, sstr, sf & 1u, (sf >> 1) & 1u, sf, z);
          std::fflush(stderr);
        }
      }
    }
    // [COD4MP-SPRINTSWEEP] Sprint-bit finder. Sprint doesn't change stance, so observe SPEED instead: make
    // cl#1 RUN forward (forwardmove=127) while forcing one candidate button bit (~3s each), and track peak
    // horizontal velocity (getvelocity = sub_8227F850 reads vec3 at ps+0x28). The sprint bit shows ~290 ups
    // vs ~190 for a normal run. Gated on CS_ACTIVE + valid ps (same crash-avoidance as BTNSWEEP).
    if (env_on("COD4_SPRINTSWEEP") && num == 1 && rd32(base, cl + 0) == 4) {
      uint32_t ent = rd32(base, cl + 0x21280u);
      uint32_t ps = (ent > 0x10000u && ent < 0x90000000u) ? rd32(base, ent + 0x15Cu) : 0;
      if (ps > 0x10000u && ps < 0x90000000u) {
        static const uint32_t cand[] = {0u, 0x2u, 0x4u, 0x10u, 0x100000u, 0x200000u, 0x400000u, 0x800000u};
        static time_t t0 = 0; if (!t0) t0 = std::time(nullptr);
        int slot = (int)((std::time(nullptr) - t0) / 3) % (int)(sizeof(cand) / sizeof(cand[0]));
        uint32_t bit = cand[slot];
        uint32_t be = __builtin_bswap32(bit);
        std::memcpy(base + cmd + 4, &be, 4);     // force ONLY this bit
        base[cmd + 22] = 127; base[cmd + 23] = 0; // run forward (BW's setplayerangles still aims the facing)
        static float peak[8] = {0};
        uint32_t vx = rd32(base, ps + 0x28u), vy = rd32(base, ps + 0x2Cu);
        float fvx, fvy; std::memcpy(&fvx, &vx, 4); std::memcpy(&fvy, &vy, 4);
        float sp = std::sqrt(fvx * fvx + fvy * fvy);
        if (sp > peak[slot]) peak[slot] = sp;
        static time_t last = 0;
        if (throttle_1s(&last)) {
          std::fprintf(stderr, "[COD4MP-SPRINTSWEEP] bit=0x%04X speed=%.0f peakForBit=%.0f\n", bit, sp, peak[slot]);
          std::fflush(stderr);
        }
      }
    }
    // [COD4MP-ADSSWEEP] ADS-bit finder. ADS is a float fraction (0=hip..1=aiming) at ps+0xf4 (playerads =
    // sub_8227F238). Force one candidate bit ~3s each with the bot STANDING STILL (ADS engages while
    // stationary) and track the peak ADS fraction — the ADS bit ramps it toward 1.0. (0x40 slowed movement
    // in the sprint sweep, a known ADS side effect, so it's the prime suspect.) Gated COD4_ADSSWEEP.
    if (env_on("COD4_ADSSWEEP") && num == 1 && rd32(base, cl + 0) == 4) {
      uint32_t ent = rd32(base, cl + 0x21280u);
      uint32_t ps = (ent > 0x10000u && ent < 0x90000000u) ? rd32(base, ent + 0x15Cu) : 0;
      if (ps > 0x10000u && ps < 0x90000000u) {
        static const uint32_t cand[] = {0u, 0x8u, 0x20u, 0x40u, 0x80u, 0x800u, 0x1000u, 0x2000u};
        static time_t t0 = 0; if (!t0) t0 = std::time(nullptr);
        int slot = (int)((std::time(nullptr) - t0) / 3) % (int)(sizeof(cand) / sizeof(cand[0]));
        uint32_t bit = cand[slot];
        uint32_t be = __builtin_bswap32(bit);
        std::memcpy(base + cmd + 4, &be, 4);     // force ONLY this bit
        base[cmd + 22] = 0; base[cmd + 23] = 0;  // stand still
        static float peak[8] = {0};
        uint32_t av = rd32(base, ps + 0xF4u); float ads; std::memcpy(&ads, &av, 4);
        if (ads > peak[slot]) peak[slot] = ads;
        static time_t last = 0;
        if (throttle_1s(&last)) {
          std::fprintf(stderr, "[COD4MP-ADSSWEEP] bit=0x%04X ads=%.2f peakForBit=%.2f\n", bit, ads, peak[slot]);
          std::fflush(stderr);
        }
      }
    }
  }
  __imp__sub_82206078(ctx, base);
}
