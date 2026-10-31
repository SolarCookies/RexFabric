// Mods on disk: one folder each under the mods directory, with their load order.
//
// The order lives in mods.json in the mods directory: the enabled mods in load order, and the ones turned
// off. A folder the file doesn't mention is new, and joins the end of the load order enabled, so a mod
// dropped into the folder or installed from Thunderstore just works.
//
//   { "load_order": ["UnlimitedGardenSpace", "FreeCam"], "disabled": ["Test"] }

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rexfabric {

struct ModInfo {
  std::string folder;
  std::filesystem::path dir;
  // From mod.toml, else Thunderstore's manifest.json. The name falls back to the folder.
  std::string name;
  std::string description;
  std::string version;
  std::string author;
  bool enabled = true;
};

class ModList {
 public:
  explicit ModList(std::filesystem::path root) : root_(std::move(root)) {}

  // Rereads the folders and the order file.
  void Scan();
  bool Save() const;

  // The enabled mods in load order, then the disabled ones by name.
  const std::vector<ModInfo>& mods() const { return mods_; }
  size_t EnabledCount() const;
  const std::filesystem::path& root() const { return root_; }
  const ModInfo* Find(std::string_view folder) const;

  // Enabling puts a mod last in the load order; disabling takes it out. Both save.
  void SetEnabled(size_t index, bool enabled);
  // Moves an enabled mod `delta` places in the load order, and saves.
  void Move(size_t index, int delta);

 private:
  void SortDisabled();

  std::filesystem::path root_;
  std::vector<ModInfo> mods_;
};

bool SameModName(std::string_view a, std::string_view b);

// The native plugins a mod ships: .asi and the platform's library files, in the mod folder itself or in its
// plugins folder, sorted by name.
std::vector<std::filesystem::path> NativePlugins(const std::filesystem::path& dir);

// The Lua files a mod runs: every .lua directly in its scripts folder, by name. Subfolders are for require.
std::vector<std::filesystem::path> LuaScripts(const std::filesystem::path& dir);

}  // namespace rexfabric
