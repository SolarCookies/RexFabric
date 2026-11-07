#include <rexfabric/rexfabric.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

#include <rex/filesystem.h>
#include <rex/ui/keybinds.h>
#include <rexfabric/game_api.h>

#include "hooks.h"
#include "log.h"
#include "lua/lua_scripts.h"
#include "mods.h"
#include "native_loader.h"
#include "ui/imgui_menu.h"
#include "ui/menu_model.h"
#include "ui/screen_text.h"
#if REXFABRIC_WITH_GUIDE
#include "ui/guide_menu.h"
#endif
#if REXFABRIC_WITH_NODE_EDITOR
#include "ui/node_editor.h"
#endif

namespace rexfabric {
namespace {

struct State {
  Config config;
  bool started = false;
  NativeLoader native;
  RexFabricHookId frame_hook = 0;
  std::chrono::steady_clock::time_point last_frame;
  std::unique_ptr<ui::MenuModel> menu;
  rex::ui::ImGuiDrawer* drawer = nullptr;
  ui::ModsWindow* window = nullptr;
#if REXFABRIC_WITH_NODE_EDITOR
  ui::NodeEditorWindow* editor = nullptr;
#endif
};

State& Get() {
  static State state;
  return state;
}

const Event& FrameEvent() {
  static const Event frame("frame");
  return frame;
}

void OnFrame(RexFabricCall* call, void*) {
  Tick();
  CallNext(*call);
}

}  // namespace

rex::LogCategoryId LogCategory() {
  static const rex::LogCategoryId category = rex::RegisterLogCategory("rexfabric");
  return category;
}

void Setup(Config config) {
  if (config.mods_dir.empty()) config.mods_dir = rex::filesystem::GetExecutableFolder() / "mods";
  Get().config = std::move(config);
}

const Config& GetConfig() { return Get().config; }

namespace {

std::atomic<uint64_t> g_mods_revision{0};
// Changes to mods.json from the game's calls, so two threads don't write it at once.
std::mutex g_mods_mutex;

}  // namespace

void NoteModsChanged() { g_mods_revision.fetch_add(1, std::memory_order_relaxed); }

std::filesystem::path ModsDir() {
  const std::filesystem::path& dir = Get().config.mods_dir;
  return dir.empty() ? rex::filesystem::GetExecutableFolder() / "mods" : dir;
}

std::vector<InstalledMod> InstalledMods() {
  ModList list(ModsDir());
  list.Scan();
  std::vector<InstalledMod> mods;
  for (const ModInfo& mod : list.mods()) mods.push_back({mod.folder, mod.dir, mod.name, mod.version, mod.author, mod.enabled});
  return mods;
}

std::vector<std::filesystem::path> EnabledModDirs() {
  ModList list(ModsDir());
  list.Scan();
  std::vector<std::filesystem::path> dirs;
  for (size_t i = 0; i < list.EnabledCount(); ++i) dirs.push_back(list.mods()[i].dir);
  return dirs;
}

uint64_t ModsRevision() { return g_mods_revision.load(std::memory_order_relaxed); }

bool SetModEnabled(std::string_view folder, bool enabled) {
  std::lock_guard lock(g_mods_mutex);
  ModList list(ModsDir());
  list.Scan();
  const ModInfo* mod = list.Find(folder);
  if (!mod) return false;
  if (mod->enabled != enabled) {
    list.SetEnabled(static_cast<size_t>(mod - list.mods().data()), enabled);
    NoteModsChanged();
  } else if (list.New(folder)) {
    // A folder the game just made (a new workspace, say) is on already as a new folder; write it into the load
    // order so it stays there, and tell the content caches.
    list.Save();
    NoteModsChanged();
  }
  return true;
}

bool SeedLoadOrder(const std::vector<std::string>& enabled) {
  std::lock_guard lock(g_mods_mutex);
  std::error_code error;
  if (std::filesystem::exists(ModsDir() / kOrderFile, error)) return false;
  ModList list(ModsDir());
  list.Scan();
  list.SetOrder(enabled);
  NoteModsChanged();
  RF_INFO("load order started from the game's earlier list: {} of {} mods enabled", list.EnabledCount(), list.mods().size());
  return true;
}

bool RenameMod(std::string_view from, std::string_view to, std::string* error) {
  std::lock_guard lock(g_mods_mutex);
  ModList list(ModsDir());
  list.Scan();
  const ModInfo* mod = list.Find(from);
  if (!mod) {
    if (error) *error = "There is no mod named " + std::string(from);
    return false;
  }
  if (!list.Rename(static_cast<size_t>(mod - list.mods().data()), std::string(to), error)) return false;
  NoteModsChanged();
  return true;
}

void Start() {
  State& state = Get();
  if (state.started) return;
  state.started = true;
  state.config.mods_dir = ModsDir();
  Hooks::Get().Init();

  ModList list(state.config.mods_dir);
  list.Scan();
  list.Save();
  const std::vector<ModInfo> enabled(list.mods().begin(), list.mods().begin() + static_cast<ptrdiff_t>(list.EnabledCount()));
  RF_INFO("{} of {} mods enabled in {}", enabled.size(), list.mods().size(), list.root().string());
  if (state.config.native_mods) state.native.Load(enabled);
  if (state.config.lua_mods) lua::Start(enabled);

  if (state.config.frame_function) {
    state.frame_hook = Hooks::Get().Add(Host(), state.config.frame_function, OnFrame, nullptr);
    if (!state.frame_hook) RF_ERROR("the frame function {:08X} isn't hookable, so no frame events", state.config.frame_function);
  }
}

void Stop() {
  State& state = Get();
  if (!state.started) return;
  Hooks::Get().Remove(state.frame_hook);
  lua::Stop();
  state.native.Unload();
  Detach(Host());
  state.started = false;
}

void Tick() {
  State& state = Get();
  const auto now = std::chrono::steady_clock::now();
  const double seconds = state.last_frame.time_since_epoch().count() ? std::chrono::duration<double>(now - state.last_frame).count() : 0.0;
  state.last_frame = now;
  FrameEvent().Fire(seconds);
}

void CreateMenus(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* images) {
  State& state = Get();
  if (state.menu) return;
  state.drawer = drawer;
  ui::SetScreenTextDrawer(drawer);
  state.menu = std::make_unique<ui::MenuModel>(state.config.mods_dir, state.config.thunderstore, images);
#if REXFABRIC_WITH_GUIDE
  ui::guide::AddMenus(*state.menu);
#endif
  if (!state.config.menu_key.empty()) rex::ui::RegisterBind("bind_mods", state.config.menu_key, "Toggle the mods menu", [] { ToggleMenu(); });
#if REXFABRIC_WITH_NODE_EDITOR
  if (!state.config.node_editor_key.empty()) rex::ui::RegisterBind("bind_node_editor", state.config.node_editor_key, "Toggle the node editor", [] { ShowNodeEditor(!NodeEditorShown()); });
#endif
}

void ShowMenu(bool show) {
  State& state = Get();
  if (!state.menu) return;
#if REXFABRIC_WITH_GUIDE
  ui::guide::Show(show);
#else
  if (show && !state.window) state.window = new ui::ModsWindow(state.drawer, *state.menu, [] { Get().window = nullptr; });
  else if (!show && state.window) state.window->Hide();
#endif
}

namespace {

bool ModsMenuShown() {
#if REXFABRIC_WITH_GUIDE
  return ui::guide::Shown();
#else
  return Get().window != nullptr;
#endif
}

}  // namespace

void ToggleMenu() { ShowMenu(!ModsMenuShown()); }

bool MenuShown() { return ModsMenuShown() || NodeEditorShown(); }

void ShowNodeEditor(bool show) {
#if REXFABRIC_WITH_NODE_EDITOR
  State& state = Get();
  if (!state.menu) return;
  if (show && !state.editor) state.editor = new ui::NodeEditorWindow(state.drawer, *state.menu, [] { Get().editor = nullptr; });
  else if (!show && state.editor) state.editor->Hide();
#else
  (void)show;
#endif
}

bool NodeEditorShown() {
#if REXFABRIC_WITH_NODE_EDITOR
  return Get().editor != nullptr;
#else
  return false;
#endif
}

}  // namespace rexfabric
