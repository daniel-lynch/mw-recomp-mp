// cod4_mp - ReXGlue Recompiled Project (Call of Duty 4 / IW3 MULTIPLAYER, iw3mp)
//
// Mirrors mw-recomp/src/cod4_app.h. Override virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/filesystem.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

class Cod4MpApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Cod4MpApp>(new Cod4MpApp(ctx, "cod4_mp",
        PPCImageConfig));
  }

  // CRITICAL: the SDK defaults the entrypoint XEX to "game:\default.xex" (the SP exe).
  // This binary is recompiled from iw3mp, so it MUST load default_mp.xex — otherwise the
  // SP XEX's entry address (0x820E4600) is looked up in the MP function table (entry
  // 0x82103AC8) and never resolves, so the guest main thread exits without running.
  void OnLoadXexImage(std::string& xex_image) override {
    xex_image = "game:\\default_mp.xex";
  }

  // --- Boot lifecycle probes [COD4MP-*] -----------------------------------
  void OnPostLoadXexImage() override { Mark("post-load-xex"); }
  void OnPreLaunchModule() override { Mark("pre-launch"); }
  void OnGuestThreadExit(rex::system::XThread* thread) override {
    (void)thread;
    Mark("guest-thread-exit");
  }

  // SDK >=0.8.0 dropped the implicit exe_dir/assets game-data default. Restore it so
  // a bare `./cod4_mp` works: `assets` is a symlink to the extracted disc dir
  // (/home/dlynch/Games/cod4 — default_mp.xex + *_mp.ff fastfiles).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty()) {
      paths.game_data_root = rex::filesystem::GetExecutableFolder() / "assets";
    }
  }

 private:
  void Mark(const char* when) {
    std::fprintf(stderr, "[COD4MP-LIFE] %s\n", when);
    std::fflush(stderr);
  }

  [[maybe_unused]] uint32_t Rd32(uint32_t ga) {
    uint8_t* mb = runtime()->virtual_membase();
    uint32_t v;
    std::memcpy(&v, mb + ga, 4);
    return __builtin_bswap32(v);
  }
};
