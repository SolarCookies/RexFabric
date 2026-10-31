#include <rexfabric/rexfabric.h>

#include <rex/filesystem.h>

#include "hooks.h"
#include "log.h"
#include "mods.h"
#include "native_loader.h"

namespace rexfabric {
namespace {

struct State {
  bool started = false;
  NativeLoader native;
};

State& Get() {
  static State state;
  return state;
}

}  // namespace

rex::LogCategoryId LogCategory() {
  static const rex::LogCategoryId category = rex::RegisterLogCategory("rexfabric");
  return category;
}

void Start(const Config& config) {
  State& state = Get();
  if (state.started) return;
  state.started = true;
  Hooks::Get().Init();
  const std::filesystem::path dir = config.mods_dir.empty() ? rex::filesystem::GetExecutableFolder() / "mods" : config.mods_dir;
  const std::vector<ModFolder> mods = FindMods(dir);
  RF_INFO("{} mods in {}", mods.size(), dir.string());
  if (config.load_native_mods) state.native.Load(mods);
}

void Stop() {
  State& state = Get();
  if (!state.started) return;
  state.native.Unload();
  Hooks::Get().RemoveAll(Host());
  state.started = false;
}

}  // namespace rexfabric
