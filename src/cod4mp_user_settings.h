// Host-side local-profile store for cod4_mp, modeled on the Skate 3 recomp profile system
// (mchughalex/skate3recomp src/skate3_user_settings). A simple profiles.toml under the user-data
// root holds one or more named profiles; the selected one is applied to the SDK profile cvars
// (user_profile_name / user_profile_xuid / signin) before kernel init, so each profile gets its
// own per-profile content dir and therefore its own title-specific save blobs (rank/XP/unlocks/
// custom classes in XPROFILE_TITLE_SPECIFIC1 = 0x63E83FFF).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cod4mp {

struct LocalProfile {
  std::string id;
  std::string gamertag;
  uint64_t xuid = 0;
  bool signed_in = true;
  bool live_signed_in = false;
};

struct LocalProfileStore {
  std::string selected_profile;
  std::vector<LocalProfile> profiles;
};

std::filesystem::path ProfilesFilePath(const std::filesystem::path& user_data_root);
LocalProfileStore LoadProfiles(const std::filesystem::path& profiles_path);
bool SaveProfiles(const std::filesystem::path& profiles_path, const LocalProfileStore& store);
LocalProfile MakeDefaultProfile(std::string gamertag);
LocalProfile* FindSelectedProfile(LocalProfileStore& store);
const LocalProfile* FindSelectedProfile(const LocalProfileStore& store);
void EnsureUsableProfileStore(LocalProfileStore& store, std::string default_gamertag);
void ApplyProfileCvars(const LocalProfile& profile);
std::string FormatXuid(uint64_t xuid);

}  // namespace cod4mp
