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
#include <string_view>
#include <vector>

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

// A mod in the mods folder, for the game to show or report.
struct InstalledMod {
  std::string folder;
  std::filesystem::path dir;
  std::string name;
  std::string version;
  std::string author;
  bool enabled = false;
};
// The mods folder as it is now: the enabled mods in load order, then the rest. Reads the folder each call, so
// it's for occasional use, such as a crash report. Any thread.
std::vector<InstalledMod> InstalledMods();

// Content: for a game that loads files from mods (models, textures, data). Each reads the mods folder as it is
// now, so a change applies at once, where code mods wait for the next start. Any thread.
std::filesystem::path ModsDir();
// The enabled mods' folders, in load order.
std::vector<std::filesystem::path> EnabledModDirs();
// Goes up whenever a mod is turned on or off, moved or renamed in this process (the menu, the calls below, an
// install), so a cache of mod content knows to reload.
uint64_t ModsRevision();
// Turns a mod on or off, as the menu does. False when there's no such mod.
bool SetModEnabled(std::string_view folder, bool enabled);
// Renames a mod's folder, keeping its place in the load order.
bool RenameMod(std::string_view from, std::string_view to, std::string* error = nullptr);

// For a game moving from a mod system of its own: when the mods folder has no load order yet, these folders
// start enabled in this order and every other one starts off. Call before Start. False when there was a load
// order already, which is then left alone.
bool SeedLoadOrder(const std::vector<std::string>& enabled);

// The interface native mods get. The game may use it too, as Host().
const RexFabricApi& Api();
RexFabricMod* Host();

}  // namespace rexfabric
