// Mods on disk: one folder each under the mods directory.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rexfabric {

struct ModFolder {
  std::string name;
  std::filesystem::path dir;
};

// Every mod folder under `root`, sorted by name. Folders starting with "." are skipped.
std::vector<ModFolder> FindMods(const std::filesystem::path& root);

// The native plugins a mod ships: .asi and the platform's library files, in the mod folder itself or in its
// plugins folder, sorted by name.
std::vector<std::filesystem::path> NativePlugins(const ModFolder& mod);

}  // namespace rexfabric
