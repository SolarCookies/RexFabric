// RexGlass, when the game has it. Built in when the game attaches RexGlass (REXFABRIC_WITH_GLASS), and used when
// the game installed it, so the menus and the node editor are glass in a game that wants glass and plain ImGui
// otherwise.

#pragma once

#include <imgui.h>

#if REXFABRIC_WITH_GLASS
#include <rexglass/panel.h>
#include <rexglass/rexglass.h>
#endif

namespace rexfabric::ui::glass {

inline bool Enabled() {
#if REXFABRIC_WITH_GLASS
  return rexglass::Installed();
#else
  return false;
#endif
}

// ImGui::Begin, as a glass window when there's glass. Pair it with ImGui::End.
inline bool BeginWindow(const char* name, bool* open, ImGuiWindowFlags flags) {
#if REXFABRIC_WITH_GLASS
  if (Enabled()) return rexglass::BeginWindow(name, open, flags);
#endif
  return ImGui::Begin(name, open, flags);
}

}  // namespace rexfabric::ui::glass
