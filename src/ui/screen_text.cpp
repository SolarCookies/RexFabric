#include "screen_text.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <deque>
#include <mutex>

#include <imgui.h>

#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/windowed_app_context.h>

namespace rexfabric::ui {
namespace {

using Clock = std::chrono::steady_clock;

// Older lines than this many go, so a print in every frame doesn't fill the screen.
constexpr size_t kMaxLines = 40;
// The text's size, in multiples of the UI's font size, so it reads over the game from across the room.
constexpr float kTextScale = 1.6f;
// From the screen's top right corner, and between lines, in multiples of the text's size.
constexpr float kMargin = 1.0f;
constexpr float kLineGap = 0.2f;
// Lines fade out over their last moments.
constexpr double kFadeSeconds = 0.4;

struct Line {
  std::string text;
  Clock::time_point until;
  uint32_t rgb = 0;
};

struct Screen {
  std::mutex mutex;
  std::deque<Line> lines;  // newest first
  rex::ui::ImGuiDrawer* drawer = nullptr;
  // The overlay is open, or on its way from the UI thread.
  bool overlay = false;
};

Screen& Get() {
  static Screen screen;
  return screen;
}

// Draws the lines over everything, and closes itself once there are none.
class Overlay final : public rex::ui::ImGuiDialog {
 public:
  explicit Overlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    Screen& screen = Get();
    std::lock_guard lock(screen.mutex);
    const Clock::time_point now = Clock::now();
    std::erase_if(screen.lines, [now](const Line& line) { return line.until <= now; });
    if (screen.lines.empty()) {
      screen.overlay = false;
      Close();
      return;
    }

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* face = ImGui::GetFont();
    const float font = ImGui::GetFontSize() * kTextScale;
    const float right = io.DisplaySize.x - font * kMargin;
    float y = font * kMargin;
    for (const Line& line : screen.lines) {
      const double left = std::chrono::duration<double>(line.until - now).count();
      const float alpha = static_cast<float>(std::clamp(left / kFadeSeconds, 0.0, 1.0));
      const ImVec2 size = face->CalcTextSizeA(font, FLT_MAX, 0.0f, line.text.c_str());
      const ImVec2 at(right - size.x, y);
      const ImU32 color = IM_COL32((line.rgb >> 16) & 0xFF, (line.rgb >> 8) & 0xFF, line.rgb & 0xFF, static_cast<int>(255.0f * alpha));
      // A shadow, so the text reads over a bright game.
      draw->AddText(face, font, ImVec2(at.x + 2.0f, at.y + 2.0f), IM_COL32(0, 0, 0, static_cast<int>(200.0f * alpha)), line.text.c_str());
      draw->AddText(face, font, at, color, line.text.c_str());
      y += size.y + font * kLineGap;
      if (y > io.DisplaySize.y) break;
    }
  }
};

rex::ui::WindowedAppContext* AppContext() {
  auto* kernel = rex::system::kernel_state();
  auto* runtime = kernel ? kernel->emulator() : nullptr;
  return runtime ? runtime->app_context() : nullptr;
}

}  // namespace

void PrintToScreen(std::string text, double seconds, uint32_t rgb) {
  Screen& screen = Get();
  std::lock_guard lock(screen.mutex);
  if (!screen.drawer || seconds <= 0.0) return;
  screen.lines.push_front({std::move(text), Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds)), rgb & 0xFFFFFF});
  if (screen.lines.size() > kMaxLines) screen.lines.pop_back();
  if (screen.overlay) return;

  // Dialogs are made on the UI thread; prints come from wherever the scripts run.
  rex::ui::WindowedAppContext* context = AppContext();
  rex::ui::ImGuiDrawer* drawer = screen.drawer;
  screen.overlay = context && context->CallInUIThread([drawer] { new Overlay(drawer); });
}

void SetScreenTextDrawer(rex::ui::ImGuiDrawer* drawer) {
  std::lock_guard lock(Get().mutex);
  Get().drawer = drawer;
}

}  // namespace rexfabric::ui
