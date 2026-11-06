#include "node_graph_style.h"

#include <algorithm>
#include <cmath>

#include <imgui_node_editor.h>

#include "glass.h"

namespace ed = ax::NodeEditor;

namespace rexfabric::ui {
namespace {

// Seconds a node takes to fade in, and the delay between nodes appearing together.
constexpr float kAppearSeconds = 0.35f;
constexpr float kAppearStagger = 0.025f;
// How quickly hover and selection ease in and out, per second.
constexpr float kEaseRate = 12.0f;
constexpr float kRounding = 10.0f;
#if REXFABRIC_WITH_GLASS
// Glass nodes are rounder, as Toon's are.
constexpr float kGlassRounding = 14.0f;
#endif
// The backdrop's colors drift through one cycle in this many seconds.
constexpr float kBackdropCycle = 40.0f;

float Ease(float current, float target, float delta) { return current + (target - current) * (1.0f - std::exp(-kEaseRate * delta)); }

ImU32 Color(const ImVec4& color, float scale, float alpha) { return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x * scale, color.y * scale, color.z * scale, alpha)); }

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }

}  // namespace

void NodeGraphStyle::Apply() {
  ed::Style& style = ed::GetStyle();
  style.NodeRounding = kRounding;
  style.NodePadding = ImVec4(12.0f, 8.0f, 12.0f, 10.0f);
  style.NodeBorderWidth = 0.0f;
  style.HoveredNodeBorderWidth = 0.0f;
  style.SelectedNodeBorderWidth = 0.0f;
  style.LinkStrength = 140.0f;
  style.FlowMarkerDistance = 34.0f;
  style.FlowSpeed = 110.0f;
  style.FlowDuration = 1.4f;
  style.PinRounding = 6.0f;

  // The style draws the nodes, their rims and glow; the backdrop replaces the canvas background.
  style.Colors[ed::StyleColor_Bg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  style.Colors[ed::StyleColor_Grid] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
  style.Colors[ed::StyleColor_NodeBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  style.Colors[ed::StyleColor_NodeBorder] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  style.Colors[ed::StyleColor_HovNodeBorder] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  style.Colors[ed::StyleColor_SelNodeBorder] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  style.Colors[ed::StyleColor_NodeSelRect] = ImVec4(0.45f, 0.65f, 1.0f, 0.12f);
  style.Colors[ed::StyleColor_NodeSelRectBorder] = ImVec4(0.55f, 0.75f, 1.0f, 0.6f);
  style.Colors[ed::StyleColor_HovLinkBorder] = ImVec4(1.0f, 1.0f, 1.0f, 0.55f);
  style.Colors[ed::StyleColor_SelLinkBorder] = ImVec4(0.6f, 0.8f, 1.0f, 0.9f);
  style.Colors[ed::StyleColor_Flow] = ImVec4(1.0f, 1.0f, 1.0f, 0.3f);
  style.Colors[ed::StyleColor_FlowMarker] = ImVec4(1.0f, 1.0f, 1.0f, 0.6f);
}

void NodeGraphStyle::DrawBackdrop() {
  delta_ = ImGui::GetIO().DeltaTime;
  time_ += delta_;
  appearing_ = 0;

  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 size = ImGui::GetContentRegionAvail();
  const ImVec2 max(min.x + size.x, min.y + size.y);
#if REXFABRIC_WITH_GLASS
  // Under glass nodes, Toon's drifting colored light, for the glass to show.
  if (glass::Enabled()) {
    rexglass::DrawAurora(ImGui::GetWindowDrawList(), min, max, time_);
    return;
  }
#endif

  // A deep blue to violet gradient whose corners drift slowly, so the canvas never looks quite still.
  const float phase = time_ * 6.2831853f / kBackdropCycle;
  const ImVec4 deep(0.05f, 0.06f, 0.10f, 1.0f);
  const ImVec4 blue(0.07f, 0.11f, 0.20f, 1.0f);
  const ImVec4 violet(0.13f, 0.08f, 0.19f, 1.0f);
  const auto corner = [&](float offset) { return ImGui::ColorConvertFloat4ToU32(Mix(deep, Mix(blue, violet, 0.5f + 0.5f * std::sin(phase + offset)), 0.75f)); };
  ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min, max, corner(0.0f), corner(1.6f), corner(3.1f), corner(4.7f));
}

void NodeGraphStyle::BeginNode(int node) {
  // A node seen for the first time fades in; several at once (a graph opening) one after the other.
  auto [state, inserted] = nodes_.try_emplace(node);
  if (inserted) state->second.appear = -kAppearStagger * static_cast<float>(appearing_++);
  NodeState& current = state->second;
  current.appear = std::min(current.appear + delta_ / kAppearSeconds, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * std::clamp(current.appear, 0.0f, 1.0f));
}

void NodeGraphStyle::EndNode(int node, const ImVec4& color, float header_bottom, bool problem) {
  ImGui::PopStyleVar();

  NodeState& state = nodes_[node];
  const ed::NodeId id(static_cast<size_t>(node));
  state.hover = Ease(state.hover, ed::GetHoveredNode() == id ? 1.0f : 0.0f, delta_);
  state.selected = Ease(state.selected, ed::IsNodeSelected(id) ? 1.0f : 0.0f, delta_);

  ImDrawList* draw = ed::GetNodeBackgroundDrawList(id);
  const ImVec2 size = ed::GetNodeSize(id);
  if (!draw || size.x <= 0.0f) return;

  // The background draw list is in canvas units, under the node's contents.
  const ImVec2 min = ed::GetNodePosition(id);
  const ImVec2 max(min.x + size.x, min.y + size.y);
  const float appear = std::clamp(state.appear, 0.0f, 1.0f);

#if REXFABRIC_WITH_GLASS
  // Glass nodes, as in Toon. The panel is in canvas units, so the canvas's zoom scales it like the rest of the node.
  if (glass::Enabled()) {
    rexglass::Panel panel;
    panel.min = min;
    panel.max = max;
    panel.rounding = kGlassRounding;
    panel.tint = ImVec4(color.x * 0.35f, color.y * 0.35f, color.z * 0.35f, 0.34f);
    panel.header_tint = ImVec4(color.x * 0.75f, color.y * 0.75f, color.z * 0.75f, 0.55f);
    panel.header_height = header_bottom > min.y ? header_bottom - min.y + 4.0f : 0.0f;
    panel.highlight = state.hover;
    panel.frost = 0.4f;
    panel.opacity = appear;
    // Over the canvas, which is what should show through, not the game.
    panel.see_game = false;
    // Hovering brightens the rim; selecting lights it in the node's own color; a problem lights it red.
    const float glow = std::max(state.hover * 0.25f, state.selected * 0.8f);
    const bool lit = state.selected > state.hover * 0.3f;
    panel.glow = lit ? ImVec4(0.55f + color.x * 0.45f, 0.55f + color.y * 0.45f, 0.55f + color.z * 0.45f, glow) : ImVec4(1.0f, 1.0f, 1.0f, glow);
    if (problem) panel.glow = ImVec4(1.0f, 0.35f, 0.3f, std::max(glow, 0.8f));
    rexglass::AddPanel(draw, panel);
    return;
  }
#endif

  draw->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.07f + color.x * 0.06f, 0.08f + color.y * 0.06f, 0.10f + color.z * 0.06f, 0.94f * appear)), kRounding);
  if (header_bottom > min.y) {
    const ImVec2 header_max(max.x, header_bottom + 5.0f);
    draw->AddRectFilled(min, header_max, Color(color, 0.55f, 0.85f * appear), kRounding, ImDrawFlags_RoundCornersTop);
    draw->AddLine(ImVec2(min.x + 1.0f, header_max.y), ImVec2(max.x - 1.0f, header_max.y), Color(color, 0.9f, 0.5f * appear));
  }

  // The rim: faint at rest, brighter when hovered, lit in the node's own color when selected, red for a problem.
  const float glow = std::max(0.18f + state.hover * 0.3f, state.selected * 0.95f);
  ImVec4 rim = state.selected > state.hover * 0.3f ? ImVec4(0.55f + color.x * 0.45f, 0.55f + color.y * 0.45f, 0.55f + color.z * 0.45f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  if (problem) rim = ImVec4(1.0f, 0.35f, 0.3f, 1.0f);
  const float alpha = (problem ? std::max(glow, 0.8f) : glow) * appear;
  draw->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(ImVec4(rim.x, rim.y, rim.z, alpha)), kRounding, 0, 1.0f + 1.5f * std::max(state.selected, problem ? 0.5f : 0.0f));
}

bool NodeGraphStyle::Pulse(float seconds) const { return std::floor(time_ / seconds) != std::floor((time_ - delta_) / seconds); }

bool NodeGraphStyle::IsNodeActive(int node) const {
  const auto found = nodes_.find(node);
  return found != nodes_.end() && (found->second.hover > 0.5f || found->second.selected > 0.5f);
}

}  // namespace rexfabric::ui
