#include "screen_text.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>

#include <imgui.h>
#include <imgui_internal.h>

#include <rex/ui/imgui_dialog.h>

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
  float size = 0.0f;  // pixels; 0 is kTextScale times the UI's font
  std::string key;    // empty: none
};

// The largest a line can be, in pixels, so a typo doesn't fill the screen.
constexpr float kMaxTextSize = 200.0f;

struct Screen {
  std::mutex mutex;
  std::deque<Line> lines;  // newest first
  rex::ui::ImGuiDrawer* drawer = nullptr;
  // Made with the menus, on the UI thread, and kept: prints come from wherever the scripts run.
  bool overlay = false;
};

Screen& Get() {
  static Screen screen;
  return screen;
}

// Draws the lines over everything; nothing while there are none.
class Overlay final : public rex::ui::ImGuiDialog {
 public:
  explicit Overlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    Screen& screen = Get();
    std::lock_guard lock(screen.mutex);
    const Clock::time_point now = Clock::now();
    std::erase_if(screen.lines, [now](const Line& line) { return line.until <= now; });
    if (screen.lines.empty()) return;

    // In a window of its own over the whole screen, which takes no input and is kept in front: a game drawing the
    // UI itself may not draw the foreground list of a dialog without windows.
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    const bool shown = ImGui::Begin("##rexfabric_screen_text", nullptr, kFlags);
    ImGui::PopStyleVar(2);
    if (shown) {
      ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
      DrawLines(screen, io, now);
    }
    ImGui::End();
  }

 private:
  static void DrawLines(const Screen& screen, const ImGuiIO& io, Clock::time_point now) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* face = ImGui::GetFont();
    const float standard = ImGui::GetFontSize() * kTextScale;
    // The margins are the default size's, whatever each line's.
    const float right = io.DisplaySize.x - standard * kMargin;
    float y = standard * kMargin;
    for (const Line& line : screen.lines) {
      const float font = line.size > 0.0f ? line.size : standard;
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

}  // namespace

void PrintToScreen(std::string text, double seconds, uint32_t rgb, float size, std::string key) {
  Screen& screen = Get();
  std::lock_guard lock(screen.mutex);
  if (!screen.overlay || seconds <= 0.0) return;
  Line line{std::move(text), Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds)), rgb & 0xFFFFFF,
            std::isfinite(size) ? std::clamp(size, 0.0f, kMaxTextSize) : 0.0f, std::move(key)};
  // A keyed line takes the place of the one with its key, so a value printed every frame is one line that changes.
  if (!line.key.empty()) {
    const auto same = std::find_if(screen.lines.begin(), screen.lines.end(), [&line](const Line& shown) { return shown.key == line.key; });
    if (same != screen.lines.end()) {
      *same = std::move(line);
      return;
    }
  }
  screen.lines.push_front(std::move(line));
  if (screen.lines.size() > kMaxLines) screen.lines.pop_back();
}

void SetScreenTextDrawer(rex::ui::ImGuiDrawer* drawer) {
  Screen& screen = Get();
  {
    std::lock_guard lock(screen.mutex);
    screen.drawer = drawer;
    if (!drawer || screen.overlay) return;
    screen.overlay = true;
  }
  // CreateMenus runs on the UI thread, where dialogs are made. Unlocked: a dialog may draw as it's made.
  new Overlay(drawer);
}

}  // namespace rexfabric::ui
