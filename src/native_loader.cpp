#include "native_loader.h"

#include <ranges>

#include <rexfabric/rexfabric.h>

#include "hooks.h"
#include "log.h"

namespace rexfabric {

void NativeLoader::Load(const std::vector<ModFolder>& mods) {
  for (const ModFolder& folder : mods) {
    for (const std::filesystem::path& file : NativePlugins(folder)) LoadPlugin(folder, file);
  }
}

void NativeLoader::LoadPlugin(const ModFolder& folder, const std::filesystem::path& file) {
  auto mod = std::make_unique<RexFabricMod>(RexFabricMod{folder.name + "/" + file.filename().string()});
  std::string error;
  std::unique_ptr<Library> library = Library::Open(file, error);
  if (!library) {
    RF_ERROR("[{}] failed to load: {}", mod->name, error);
    return;
  }
  auto init = library->Function<RexFabricModInitFn>(REXFABRIC_MOD_INIT_NAME);
  auto shutdown = library->Function<RexFabricModShutdownFn>(REXFABRIC_MOD_SHUTDOWN_NAME);
  if (!init) {
    RF_INFO("[{}] loaded (no {}, so it sets itself up)", mod->name, REXFABRIC_MOD_INIT_NAME);
  } else if (!init(&Api(), mod.get())) {
    RF_WARN("[{}] declined to load", mod->name);
    Hooks::Get().RemoveAll(mod.get());
    return;
  } else {
    RF_INFO("[{}] loaded", mod->name);
  }
  plugins_.push_back({std::move(mod), std::move(library), shutdown});
}

void NativeLoader::Unload() {
  for (Plugin& plugin : plugins_ | std::views::reverse) {
    Hooks::Get().RemoveAll(plugin.mod.get());
    if (plugin.shutdown) plugin.shutdown();
    // Left mapped on purpose: a guest thread may still be returning through one of its hooks, and this only
    // runs as the process exits.
    (void)plugin.library.release();
    (void)plugin.mod.release();
  }
  plugins_.clear();
}

}  // namespace rexfabric
