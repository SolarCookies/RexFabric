// RexFabric for the host project: start it once the runtime is set up, stop it on the way out. Closing the
// window hard exits without reaching OnShutdown, so stop it on the close request too; Stop runs once.
//
//   void OnPostSetup() override { rexfabric::Start(); }
//   bool OnWindowCloseRequested() override { rexfabric::Stop(); return true; }
//   void OnShutdown() override { rexfabric::Stop(); }

#pragma once

#include <filesystem>

#include <rexfabric/api.h>

namespace rexfabric {

struct Config {
  // Where mods live, one folder each. Empty means "mods" beside the executable.
  std::filesystem::path mods_dir;
  bool load_native_mods = true;
};

// Loads the mods and lets them attach their hooks. Call after guest memory exists and before the guest
// starts running; ReXApp::OnPostSetup is the right place.
void Start(const Config& config = {});
// Shuts the mods down and removes every hook. Later calls do nothing.
void Stop();

// The interface native mods get. The host may use it too, as Host().
const RexFabricApi& Api();
RexFabricMod* Host();

}  // namespace rexfabric
