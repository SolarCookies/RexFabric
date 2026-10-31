// Loads native mods (.asi, .dll, .so) and runs their RexFabricModInit.

#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <rexfabric/api.h>

#include "library.h"
#include "mod.h"
#include "mods.h"

namespace rexfabric {

class NativeLoader {
 public:
  void Load(const std::vector<ModFolder>& mods);
  // Runs each plugin's shutdown, last loaded first, and removes its hooks.
  void Unload();

 private:
  struct Plugin {
    std::unique_ptr<RexFabricMod> mod;
    std::unique_ptr<Library> library;
    RexFabricModShutdownFn shutdown;
  };

  void LoadPlugin(const ModFolder& folder, const std::filesystem::path& file);

  std::vector<Plugin> plugins_;
};

}  // namespace rexfabric
