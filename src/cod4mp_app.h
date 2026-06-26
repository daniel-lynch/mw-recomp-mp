// cod4_mp - ReXGlue Recompiled Project (Call of Duty 4 / IW3 MULTIPLAYER, iw3mp)
//
// Mirrors mw-recomp/src/cod4_app.h. Override virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/filesystem.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "cod4mp_user_settings.h"

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

  // [COD4MP-PROFILE] Local-profile selection (modeled on the Skate 3 recomp). Runs after path
  // defaults, before Runtime/UserProfile construction: load profiles.toml, ensure at least one
  // usable profile, and apply the selected one to the SDK profile cvars (name/xuid/signin). The
  // SDK keys its per-profile content dir on the profile name, so each profile keeps its own
  // title-specific save blobs (rank/XP/unlocks/custom classes). On first run we persist a default
  // "Player" profile so the file exists for editing. COD4_PROFILE selects a gamertag by env.
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override {
    (void)resume;
    auto profiles_path = cod4mp::ProfilesFilePath(defaults.user_data_root);
    const bool had_file = std::filesystem::exists(profiles_path);

    auto store = cod4mp::LoadProfiles(profiles_path);
    const char* env_tag = std::getenv("COD4_PROFILE");
    std::string default_tag = (env_tag && env_tag[0]) ? env_tag : "Player";
    cod4mp::EnsureUsableProfileStore(store, default_tag);

    // COD4_PROFILE also switches the active profile if one with that gamertag exists / create it.
    if (env_tag && env_tag[0]) {
      auto desired = cod4mp::MakeDefaultProfile(default_tag);
      bool found = false;
      for (const auto& p : store.profiles) {
        if (p.id == desired.id) { found = true; break; }
      }
      if (!found) store.profiles.push_back(desired);
      store.selected_profile = desired.id;
    }

    if (const auto* profile = cod4mp::FindSelectedProfile(store)) {
      cod4mp::ApplyProfileCvars(*profile);
      std::fprintf(stderr, "[COD4MP-PROFILE] active profile '%s' (id=%s xuid=%s) -> %s\n",
                   profile->gamertag.c_str(), profile->id.c_str(),
                   cod4mp::FormatXuid(profile->xuid).c_str(), profiles_path.string().c_str());
      std::fflush(stderr);
    }
    if (!had_file) {
      cod4mp::SaveProfiles(profiles_path, store);
    }
    return defaults;
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
