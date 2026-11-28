#include "own_graph.h"

#include <algorithm>
#include <utility>

#include "../graph/node_library.h"
#include "glass.h"

namespace rexfabric::ui {
namespace {

// The panes' widths in multiples of the font size, and their tint over the canvas's light.
constexpr float kSidebarWidth = 15.0f;
constexpr float kDetailsWidth = 26.0f;
// The edge between two panes that's dragged to widen one, and the narrowest a pane gets, in multiples of the font size.
constexpr float kSplitterWidth = 0.5f;
constexpr float kMinPane = 8.0f;
constexpr ImVec4 kPaneTint{0.05f, 0.06f, 0.11f, 0.42f};

}  // namespace

OwnGraph::OwnGraph(Host host) : host_(std::move(host)), view_({.status = [this](std::string text, bool error) { host_.status(std::move(text), error); }, .edited = [this](bool settled) {
  if (!settled) return;
  followed_ = document_->Text();
  host_.edited(followed_);
}}) {}

std::string OwnGraph::Open(const std::filesystem::path& mod_dir, const std::string& mod_folder, std::string name, graph::Location location, const std::vector<std::string>& events, const std::vector<content::AssetEventDecl>* field_events) {
  document_ = std::make_unique<graph::Document>(mod_dir, std::move(name), std::move(location));
  std::string error;
  if (!document_->Load(&error)) host_.status("Its graph can't be read: " + error, true);
  // A new graph, or one whose class or type has more events now, or whose asset's fields changed outside the editor:
  // its event nodes as they'd be had it always been so.
  const bool placed = graph::PlaceEventNodes(document_->graph(), events);
  const bool followed = field_events != nullptr && graph::PlaceFieldEvents(document_->graph(), *field_events);
  if (placed || followed) document_->Reset(document_->Text());
  followed_ = document_->Text();
  view_.SetDocument(document_.get(), mod_folder);
  view_.SetFixed([events](const graph::Node& node) { return node.owner == 0 && (graph::IsFieldEvent(node) || std::find(events.begin(), events.end(), node.type) != events.end()); });
  return followed_;
}

void OwnGraph::Follow(const std::string& text) {
  if (text.empty() || text == followed_) return;
  std::string error;
  if (!document_->Reset(text, &error)) host_.status("Its graph can't be read back: " + error, true);
  followed_ = text;
  view_.AfterHistory();
}

std::optional<std::string> OwnGraph::FollowFieldEvents(const std::vector<content::AssetEventDecl>& field_events) {
  if (!graph::PlaceFieldEvents(document_->graph(), field_events)) return std::nullopt;
  document_->Edited();
  view_.AfterHistory();
  followed_ = document_->Text();
  return followed_;
}

void OwnGraph::DrawBackdrop() {
  // Under the title bar to the window's rounded bottom.
  const ImVec2 window_min = ImGui::GetWindowPos();
  const ImVec2 window_max(window_min.x + ImGui::GetWindowSize().x, window_min.y + ImGui::GetWindowSize().y);
  view_.DrawBackdrop(ImVec2(window_min.x, window_min.y + ImGui::GetFrameHeight()), window_max, ImGui::GetStyle().WindowRounding);
}

void OwnGraph::Draw(const std::function<void()>& viewport, const std::function<void()>& details, std::string_view script_label) {
  const float font = ImGui::GetFontSize();
  if (sidebar_width_ == 0.0f) sidebar_width_ = font * kSidebarWidth;
  if (details_width_ == 0.0f) details_width_ = font * kDetailsWidth;
  DrawTabs();
  // The graph's lists only beside the graph, as a blueprint's My Blueprint panel is.
  if (tab_ != Tab::kViewport) {
    glass::BeginChild("##left", ImVec2(sidebar_width_, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, 0, kPaneTint);
    view_.DrawSidebar();
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    Splitter("##left_edge", sidebar_width_, 1.0f);
    ImGui::SameLine(0.0f, 0.0f);
  }
  const float middle = std::max(ImGui::GetContentRegionAvail().x - details_width_ - font * kSplitterWidth, font * kMinPane);
  ImGui::BeginChild("##middle", ImVec2(middle, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  switch (tab_) {
    case Tab::kViewport: viewport(); break;
    case Tab::kGraph:
      view_.DrawBreadcrumb();
      view_.DrawCanvas();
      break;
    case Tab::kLua: view_.DrawLua(script_label); break;
  }
  ImGui::EndChild();
  ImGui::SameLine(0.0f, 0.0f);
  Splitter("##right_edge", details_width_, -1.0f);
  ImGui::SameLine(0.0f, 0.0f);
  glass::BeginChild("##details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, 0, kPaneTint);
  // As a blueprint's details: the editor's own on its viewport, the selected node's on its graph.
  if (tab_ == Tab::kViewport) details();
  else view_.DrawDetails();
  ImGui::EndChild();
}

void OwnGraph::DrawTabs() {
  if (!ImGui::BeginTabBar("##views")) return;
  const auto tab = [this](const char* label, Tab which) {
    if (!ImGui::BeginTabItem(label)) return;
    tab_ = which;
    ImGui::EndTabItem();
  };
  tab("Viewport", Tab::kViewport);
  tab("Event Graph", Tab::kGraph);
  tab("Lua", Tab::kLua);
  ImGui::EndTabBar();
}

void OwnGraph::Splitter(const char* id, float& width, float toward) {
  const float font = ImGui::GetFontSize();
  ImGui::InvisibleButton(id, ImVec2(font * kSplitterWidth, std::max(ImGui::GetContentRegionAvail().y, 1.0f)));
  if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  if (ImGui::IsItemActive()) width = std::clamp(width + ImGui::GetIO().MouseDelta.x * toward, font * kMinPane, ImGui::GetWindowWidth() * 0.5f);
}

}  // namespace rexfabric::ui
