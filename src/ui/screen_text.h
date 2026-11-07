// Text mods print on the screen while they're being made, as Unreal's Print String does: each line in the screen's
// top right corner for its own few seconds, the newest at the top. Lua prints with rex.screen_text, which the node
// editor's Print String node writes.
//
// Printing is thread safe and cheap. The lines are drawn by an overlay on the app's ImGui, made with the menus, which
// opens a window only while there's something to show.

#pragma once

#include <cstdint>
#include <string>

namespace rex::ui {
class ImGuiDrawer;
}

namespace rexfabric::ui {

// Shows `text` for `seconds` in `rgb` (0xRRGGBB), `size` pixels tall (0: the default). A line with a `key` replaces
// the shown line with the same key, where it is. Before SetScreenTextDrawer, or without a UI, it's dropped.
void PrintToScreen(std::string text, double seconds, uint32_t rgb, float size = 0.0f, std::string key = {});
// Where the lines are drawn. Set once, from CreateMenus.
void SetScreenTextDrawer(rex::ui::ImGuiDrawer* drawer);

}  // namespace rexfabric::ui
