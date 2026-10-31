// The mods menu as plain ImGui windows, for games without RexLiveless: the mods with their load order, and
// the Thunderstore browser beside it when the game has a community.

#pragma once

#include <functional>
#include <string>

#include <rex/ui/imgui_dialog.h>

#include "menu_model.h"

struct ImVec2;

namespace rexfabric::ui {

class ModsWindow : public rex::ui::ImGuiDialog {
 public:
  // `closed` runs as the window goes, whether its own close button or the menu key closed it.
  ModsWindow(rex::ui::ImGuiDrawer* drawer, MenuModel& model, std::function<void()> closed);
  ~ModsWindow() override;

  void Hide() { Close(); }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void DrawMods();
  void DrawColumn(const char* id, bool enabled, float width, float height);
  void DrawRow(size_t index, bool enabled_column);

  void DrawBrowser();
  void DrawToolbar();
  void DrawGrid(float height, int& pages);
  void DrawCard(const net::Package& package, const ImVec2& size);
  void DrawPager(int pages);

  MenuModel& model_;
  std::function<void()> closed_;
  // One change per frame, made once the frame's rows are drawn.
  std::function<void()> pending_;

  bool browser_open_ = false;
  char search_[128] = {};
  double search_typed_at_ = 0.0;  // 0: nothing typed since the search last applied
  int page_ = 0;                  // from 0, a grid of packages each
  int page_input_ = 1;
};

}  // namespace rexfabric::ui
