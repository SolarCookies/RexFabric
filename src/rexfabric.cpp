#include <rexfabric/rexfabric.h>

#include <chrono>
#include <memory>

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
#if REXFABRIC_WITH_GUIDE
#include "ui/guide_menu.h"
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

void Start() {
  State& state = Get();
  if (state.started) return;
  state.started = true;
  if (state.config.mods_dir.empty()) state.config.mods_dir = rex::filesystem::GetExecutableFolder() / "mods";
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
  state.menu = std::make_unique<ui::MenuModel>(state.config.mods_dir, state.config.thunderstore, images);
#if REXFABRIC_WITH_GUIDE
  ui::guide::AddMenus(*state.menu);
#endif
  if (!state.config.menu_key.empty()) rex::ui::RegisterBind("bind_mods", state.config.menu_key, "Toggle the mods menu", [] { ToggleMenu(); });
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

void ToggleMenu() { ShowMenu(!MenuShown()); }

bool MenuShown() {
#if REXFABRIC_WITH_GUIDE
  return ui::guide::Shown();
#else
  return Get().window != nullptr;
#endif
}

}  // namespace rexfabric
