// RexFabric for the host project.
//
//   RetipApp(...) : ReXApp(...) {
//     rexfabric::Setup({.thunderstore = "my-community", .menu_key = "F5", .frame_function = 0x8266FBB0});
//   }
//   void OnPostSetup() override { DeclareModApi(); rexfabric::Start(); }
//   void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override { rexfabric::CreateMenus(drawer, immediate_drawer()); }
//   bool OnWindowCloseRequested() override { rexfabric::Stop(); return true; }
//   void OnShutdown() override { rexfabric::Stop(); }
//
// Closing the window hard exits without reaching OnShutdown, so stop on the close request too; Stop runs once.
// The game's own modding API (rexfabric/game_api.h) is declared before Start, which is when mods load.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <rexfabric/api.h>

namespace rex::ui {
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace rex::ui

namespace rexfabric {

struct Config {
  // The game's Thunderstore community, as in thunderstore.io/c/<community>. Empty: no Thunderstore, so the
  // menu has no browser.
  std::string thunderstore;
  // One folder per mod. Empty: "mods" beside the executable.
  std::filesystem::path mods_dir;
  // The key that opens the mods menu, by name ("F5"); it becomes the rebindable bind_mods. Empty: no key,
  // and the game opens the menu with ShowMenu.
  std::string menu_key;
  // A guest function the game runs once a frame, such as its present. RexFabric hooks it and fires the
  // frame event from there, on the guest thread. 0: the game calls Tick itself.
  uint32_t frame_function = 0;
  bool native_mods = true;
  bool lua_mods = true;
};

// Call once, before anything else here.
void Setup(Config config);
const Config& GetConfig();

// Loads the enabled mods. Call after guest memory exists and before the guest runs: ReXApp::OnPostSetup.
void Start();
// Shuts the mods down and removes every hook. Later calls do nothing.
void Stop();
// Fires the frame event, with the seconds since the last one. Only needed without a frame_function.
void Tick();

// Makes the mods menu: a Guide blade and window when built WITH_GUIDE, plain ImGui windows otherwise. Call
// from ReXApp::OnCreateDialogs, after the Guide is installed. `images` is the app's immediate_drawer(), for
// the mods' icons.
void CreateMenus(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* images);
void ShowMenu(bool show = true);
void ToggleMenu();
bool MenuShown();

// The interface native mods get. The game may use it too, as Host().
const RexFabricApi& Api();
RexFabricMod* Host();

}  // namespace rexfabric
