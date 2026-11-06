#include "node_editor.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <fstream>
#include <limits>
#include <map>

#include <imgui.h>
#include <imgui_node_editor.h>

#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rexfabric/rexfabric.h>

#include "../graph/graph_file.h"
#include "../graph/node_library.h"
#include "../lua/lua_scripts.h"
#include "glass.h"
#include "node_graph_widgets.h"

namespace ed = ax::NodeEditor;

namespace rexfabric::ui {
namespace {

using graph::Node;
using graph::NodeDesc;
using graph::NodePins;
using graph::NodeRole;
using graph::Pin;
using graph::PinKind;
using graph::PinRef;
using graph::PinType;
using node_widgets::PinShape;

// In multiples of the font size, so the layout follows the UI's scale.
constexpr float kSidebarWidth = 15.0f;
constexpr float kDetailsWidth = 20.0f;
constexpr float kInlineNumberWidth = 5.0f;
constexpr float kInlineFloatWidth = 4.0f;
constexpr float kInlineTextWidth = 7.0f;

// Canvas ids: nodes are the graph's own ids; pins and links are moved out of their way.
constexpr uint64_t kPinBase = uint64_t(1) << 40;
constexpr uint64_t kLinkBase = uint64_t(2) << 40;
constexpr uint64_t kPinsPerNode = 128;
constexpr uint64_t kOutputOffset = 64;

constexpr float kLinkThickness = 2.0f;
// Exec links pulse with a flow of light this often, like a heartbeat; the links of the node being worked on more
// often.
constexpr float kExecPulseSeconds = 2.4f;
constexpr float kActivePulseSeconds = 1.2f;
// How long a status message stays.
constexpr double kStatusSeconds = 5.0;
// How far a duplicate lands from the original, in canvas units.
constexpr float kDuplicateOffset = 40.0f;

constexpr ImVec4 kAcceptColor{0.4f, 1.0f, 0.4f, 1.0f};
constexpr ImVec4 kRejectColor{1.0f, 0.3f, 0.3f, 1.0f};
constexpr ImVec4 kErrorColor{1.0f, 0.45f, 0.4f, 1.0f};
constexpr ImVec4 kWarningColor{1.0f, 0.75f, 0.35f, 1.0f};
constexpr ImVec4 kGoodColor{0.5f, 0.9f, 0.5f, 1.0f};
constexpr ImVec4 kTitleColor{1.0f, 1.0f, 1.0f, 0.95f};

constexpr const char* kAddMenu = "AddNode";
constexpr const char* kAskPopup = "###ask";
constexpr const char* kSavePopup = "Unsaved changes###save";

constexpr std::array<PinKind, 5> kVariableKinds = {PinKind::kBool, PinKind::kInt, PinKind::kFloat, PinKind::kString, PinKind::kAny};

ed::NodeId NodeId(int node) { return ed::NodeId(static_cast<size_t>(node)); }
ed::PinId PinId(int node, size_t index, bool output) { return ed::PinId(static_cast<size_t>(kPinBase + static_cast<uint64_t>(node) * kPinsPerNode + (output ? kOutputOffset : 0) + index)); }
ed::LinkId LinkId(int link) { return ed::LinkId(static_cast<size_t>(kLinkBase + static_cast<uint64_t>(link))); }

struct DecodedPin {
  int node = 0;
  size_t index = 0;
  bool output = false;
};

std::optional<DecodedPin> DecodePin(ed::PinId id) {
  const uint64_t value = id.Get();
  if (value < kPinBase || value >= kLinkBase) return std::nullopt;
  const uint64_t local = value - kPinBase;
  const uint64_t slot = local % kPinsPerNode;
  const bool output = slot >= kOutputOffset;
  return DecodedPin{static_cast<int>(local / kPinsPerNode), static_cast<size_t>(output ? slot - kOutputOffset : slot), output};
}

rex::input::InputSystem* GuestInput() {
  auto* kernel = rex::system::kernel_state();
  auto* runtime = kernel ? kernel->emulator() : nullptr;
  return runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
}

ImU32 PinColor(PinKind kind) {
  switch (kind) {
    case PinKind::kExec: return IM_COL32(240, 240, 240, 255);
    case PinKind::kBool: return IM_COL32(220, 70, 70, 255);
    case PinKind::kInt: return IM_COL32(80, 220, 190, 255);
    case PinKind::kFloat: return IM_COL32(140, 230, 100, 255);
    case PinKind::kNumber: return IM_COL32(200, 230, 120, 255);
    case PinKind::kString: return IM_COL32(250, 110, 200, 255);
    case PinKind::kObject: return IM_COL32(80, 160, 255, 255);
    case PinKind::kRegisters: return IM_COL32(255, 170, 60, 255);
    case PinKind::kAny: return IM_COL32(190, 190, 190, 255);
  }
  return IM_COL32(190, 190, 190, 255);
}

// Pure nodes take the color of what they give, so a node and its links match; the rest go by what they're about.
ImVec4 HeaderColor(const NodeDesc* desc, const NodePins& pins) {
  if (!desc) return kRejectColor;
  if (desc->role == NodeRole::kEvent) return ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
  if (desc->role == NodeRole::kPure && !pins.outputs.empty()) return ImGui::ColorConvertU32ToFloat4(PinColor(pins.outputs.front().type.kind));
  const std::string_view category = desc->category;
  if (category.starts_with("Flow")) return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
  if (category.starts_with("Hooks")) return ImVec4(1.0f, 0.65f, 0.3f, 1.0f);
  if (category.starts_with("Memory")) return ImVec4(0.75f, 0.55f, 1.0f, 1.0f);
  if (category.starts_with("Variables")) return ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
  if (category.starts_with("Game")) return ImVec4(0.35f, 0.85f, 0.75f, 1.0f);
  return ImVec4(0.55f, 0.7f, 1.0f, 1.0f);
}

// Exec pins named like the usual ones need no label.
std::string_view PinLabel(const Pin& pin) {
  if (pin.type.kind == PinKind::kExec && (pin.name == graph::kExecIn || pin.name == graph::kExecOut)) return {};
  return pin.name;
}

PinShape ShapeOf(const Pin& pin) { return pin.type.kind == PinKind::kExec ? PinShape::kArrow : PinShape::kCircle; }

// Whether an output of `from` could go into `to` once its node is linked: a Number becomes whatever its inputs are.
bool MayFit(const PinType& from, const PinType& to) {
  if (from.kind == PinKind::kNumber) return to.kind == PinKind::kInt || to.kind == PinKind::kFloat || to.kind == PinKind::kNumber || to.kind == PinKind::kAny;
  return graph::Fits(from, to);
}

// A node of this type as the add menu would make it, to see its pins before it's added.
Node Prototype(const NodeDesc& desc, const std::string& setting, const std::string& value) {
  Node node;
  node.type = desc.id;
  for (const graph::SettingDesc& entry : desc.settings) node.settings[entry.name] = entry.initial;
  if (!setting.empty()) node.settings[setting] = value;
  return node;
}

const char* KindLabel(PinKind kind) {
  switch (kind) {
    case PinKind::kBool: return "Bool";
    case PinKind::kInt: return "Int";
    case PinKind::kFloat: return "Float";
    case PinKind::kString: return "String";
    default: return "Any";
  }
}

// The mod and graph last open, so the window opens again where it was.
struct LastOpen {
  std::string mod;
  std::string graph;
};

LastOpen& Last() {
  static LastOpen last;
  return last;
}

// The SDK's ImGui drawer reports Ctrl, Shift and Alt as keys but not as ImGui's modifiers, which ImGui rebuilds
// io.KeyCtrl and the rest from each frame, so its shortcuts never see them. While the editor is open the modifiers
// follow their keys, a frame behind, so Ctrl+S, the canvas's Ctrl+C and text fields' shortcuts work.
void SyncModifiers(ImGuiIO& io) {
  constexpr std::array<std::array<ImGuiKey, 3>, 3> kModifiers = {{
      {ImGuiMod_Ctrl, ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl},
      {ImGuiMod_Shift, ImGuiKey_LeftShift, ImGuiKey_RightShift},
      {ImGuiMod_Alt, ImGuiKey_LeftAlt, ImGuiKey_RightAlt},
  }};
  for (const auto& [modifier, left, right] : kModifiers) {
    const bool held = ImGui::IsKeyDown(left) || ImGui::IsKeyDown(right);
    if (held != ImGui::IsKeyDown(modifier)) io.AddKeyEvent(modifier, held);
  }
}

bool TextValue(const nlohmann::json& value, std::string& text) {
  if (value.is_string()) text = value.get<std::string>();
  else if (value.is_number()) text = value.dump();
  else text.clear();
  return true;
}

}  // namespace

NodeEditorWindow::NodeEditorWindow(rex::ui::ImGuiDrawer* drawer, MenuModel& model, std::function<void()> closed) : ImGuiDialog(drawer), model_(model), closed_(std::move(closed)) {
  if (auto* input = GuestInput()) input->AddUIInputBlocker();
  // Where the window was last time, or else the first mod with graphs, as that's likely the one being worked on.
  const LastOpen last = Last();
  if (model_.mods().Find(last.mod)) {
    OpenMod(last.mod);
    if (std::find(graphs_.begin(), graphs_.end(), last.graph) != graphs_.end()) OpenGraph(last.graph);
    return;
  }
  const std::vector<ModInfo>& mods = model_.mods().mods();
  const auto with_graphs = std::find_if(mods.begin(), mods.end(), [](const ModInfo& mod) { return !graph::ListGraphs(mod.dir).empty(); });
  if (with_graphs != mods.end()) OpenMod(with_graphs->folder);
  else if (!mods.empty()) OpenMod(mods.front().folder);
}

NodeEditorWindow::~NodeEditorWindow() {
  if (canvas_) ed::DestroyEditor(canvas_);
  if (auto* input = GuestInput()) input->RemoveUIInputBlocker();
  if (closed_) closed_();
}

void NodeEditorWindow::Hide() {
  AfterSaving([this] { Close(); });
}

void NodeEditorWindow::OnDraw(ImGuiIO& io) {
  SyncModifiers(io);
  model_.Poll();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(1320.0f, 780.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(820.0f, 480.0f), ImVec2(FLT_MAX, FLT_MAX));
  bool open = true;
  const std::string title = document_ ? "Node Editor: " + document_->name() + (document_->dirty() ? " *" : "") + "###rexfabric_node_editor" : std::string("Node Editor###rexfabric_node_editor");
  if (glass::BeginWindow(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    Shortcuts();
    DrawToolbar();
    ImGui::Separator();

    const float font = ImGui::GetFontSize();
    ImGui::BeginChild("##left", ImVec2(font * kSidebarWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    DrawGraphs();
    DrawVariables();
    ImGui::EndChild();

    ImGui::SameLine();
    const float middle = std::max(ImGui::GetContentRegionAvail().x - font * kDetailsWidth - ImGui::GetStyle().ItemSpacing.x, font * 10.0f);
    ImGui::BeginChild("##middle", ImVec2(middle, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!document_) {
      ImGui::TextDisabled(mod_folder_.empty() ? "Pick a mod, or make a new one, to start." : "Make a graph for this mod to start.");
    } else if (ImGui::BeginTabBar("##views")) {
      if (ImGui::BeginTabItem("Graph")) {
        DrawCanvas();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Lua")) {
        DrawLua();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    DrawDetails();
    ImGui::EndChild();

    DrawPopups();
  }
  ImGui::End();
  if (!open) Hide();
}

// Top ---------------------------------------------------------------------------------------------------

void NodeEditorWindow::DrawToolbar() {
  DrawModPicker();

  ImGui::SameLine();
  ImGui::BeginDisabled(!document_);
  if (ImGui::Button("Save")) Save(false);
  ImGui::SetItemTooltip("Writes the graph and its script (Ctrl+S)");
  ImGui::SameLine();
  if (ImGui::Button("Save and Reload")) Save(true);
  ImGui::SetItemTooltip("Saves, then runs every enabled Lua mod again, this one included");
  ImGui::SameLine();
  ImGui::BeginDisabled(!document_ || !document_->CanUndo());
  if (ImGui::Button("Undo")) Undo();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!document_ || !document_->CanRedo());
  if (ImGui::Button("Redo")) Redo();
  ImGui::EndDisabled();
  ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  if (!status_.empty() && ImGui::GetTime() - status_time_ < kStatusSeconds) {
    ImGui::TextColored(status_error_ ? kErrorColor : kGoodColor, "%s", status_.c_str());
  } else if (document_) {
    const auto& problems = document_->compiled().problems;
    const size_t errors = static_cast<size_t>(std::count_if(problems.begin(), problems.end(), [](const graph::Problem& problem) { return !problem.warning; }));
    if (errors) ImGui::TextColored(kErrorColor, "%zu problem%s; it won't run until they're fixed", errors, errors == 1 ? "" : "s");
    else if (!problems.empty()) ImGui::TextColored(kWarningColor, "%zu warning%s", problems.size(), problems.size() == 1 ? "" : "s");
    else ImGui::TextColored(kGoodColor, "No problems");
  }
}

void NodeEditorWindow::DrawModPicker() {
  const ModInfo* mod = Mod();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Mod");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
  if (!ImGui::BeginCombo("##mod", mod ? mod->name.c_str() : "Pick a mod")) return;
  for (const ModInfo& entry : model_.mods().mods()) {
    const std::string label = entry.name + (entry.enabled ? "" : "  (off)") + "##" + entry.folder;
    if (ImGui::Selectable(label.c_str(), entry.folder == mod_folder_) && entry.folder != mod_folder_) {
      const std::string folder = entry.folder;
      AfterSaving([this, folder] { OpenMod(folder); });
    }
  }
  ImGui::Separator();
  if (ImGui::Selectable("New Mod...")) AskFor(Ask::kNewMod, "");
  ImGui::EndCombo();
}

void NodeEditorWindow::Shortcuts() {
  if (!document_ || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
  const ImGuiIO& io = ImGui::GetIO();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Save(false);
  // A text field has undo of its own.
  if (io.WantTextInput) return;
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) io.KeyShift ? Redo() : Undo();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
}

// Left --------------------------------------------------------------------------------------------------

void NodeEditorWindow::DrawGraphs() {
  ImGui::SeparatorText("Graphs");
  ImGui::BeginDisabled(!Mod());
  if (ImGui::SmallButton("New")) AskFor(Ask::kNewGraph, graphs_.empty() ? "Main" : "Graph" + std::to_string(graphs_.size() + 1));
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!document_);
  if (ImGui::SmallButton("Rename")) AskFor(Ask::kRenameGraph, document_->name());
  ImGui::SameLine();
  if (ImGui::SmallButton("Delete")) AskFor(Ask::kDeleteGraph, document_->name());
  ImGui::EndDisabled();

  for (const std::string& name : graphs_) {
    const bool open = document_ && document_->name() == name;
    if (ImGui::Selectable((name + "##graph").c_str(), open) && !open) AfterSaving([this, name] { OpenGraph(name); });
  }
  if (Mod() && graphs_.empty()) ImGui::TextDisabled("None yet");
}

void NodeEditorWindow::DrawVariables() {
  ImGui::SeparatorText("Variables");
  if (!document_) return;
  graph::Graph& graph = document_->graph();

  if (ImGui::SmallButton("+ Variable")) {
    graph::Variable variable;
    variable.name = graph.UniqueVariableName("NewVar");
    variable.type = PinKind::kFloat;
    variable.initial = graph::DefaultValue(PinKind::kFloat);
    graph.variables().push_back(variable);
    selected_variable_ = variable.name;
    focus_ = Focus::kVariable;
    Edited();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!graph.FindVariable(selected_variable_));
  if (ImGui::SmallButton("Remove")) {
    // Its Get and Set nodes go with it.
    const std::string name = selected_variable_;
    std::vector<int> users;
    for (const Node& node : graph.nodes()) {
      if ((node.type == "var.get" || node.type == "var.set") && node.Setting("Variable") == name) users.push_back(node.id);
    }
    for (int id : users) graph.RemoveNode(id);
    std::erase_if(graph.variables(), [&name](const graph::Variable& variable) { return variable.name == name; });
    selected_variable_.clear();
    focus_ = Focus::kNone;
    Edited();
  }
  ImGui::EndDisabled();

  for (const graph::Variable& variable : graph.variables()) {
    ImGui::PushID(variable.name.c_str());
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float swatch = ImGui::GetTextLineHeight() * 0.6f;
    ImGui::Dummy(ImVec2(swatch, ImGui::GetTextLineHeight()));
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(start.x, start.y + swatch * 0.33f), ImVec2(start.x + swatch, start.y + swatch * 1.33f), PinColor(variable.type), 2.0f);
    ImGui::SameLine();
    if (ImGui::Selectable(variable.name.c_str(), focus_ == Focus::kVariable && selected_variable_ == variable.name)) {
      selected_variable_ = variable.name;
      focus_ = Focus::kVariable;
    }
    ImGui::SetItemTooltip("%s%s%s", KindLabel(variable.type), variable.comment.empty() ? "" : "\n", variable.comment.c_str());
    ImGui::PopID();
  }
  if (graph.variables().empty()) ImGui::TextDisabled("None yet. Variables keep values between events.");
}

// Middle: the canvas -------------------------------------------------------------------------------------

void NodeEditorWindow::DrawCanvas() {
  graph::Graph& graph = document_->graph();
  problem_nodes_.clear();
  for (const graph::Problem& problem : document_->compiled().problems) {
    if (!problem.warning) problem_nodes_.insert(problem.node);
  }

  const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
  pin_tooltip_.clear();
  style_.DrawBackdrop();
  ed::SetCurrentEditor(canvas_);
  ed::Begin("##graph");

  for (Node& node : graph.nodes()) {
    if (place_nodes_) ed::SetNodePosition(NodeId(node.id), ImVec2(node.x, node.y));
    DrawNode(node);
  }
  place_nodes_ = false;

  for (const graph::Link& link : graph.links()) {
    const Node* source = graph.FindNode(link.from.node);
    const Node* target = graph.FindNode(link.to.node);
    if (!source || !target) continue;
    const NodePins source_pins = graph::PinsOf(*source, graph);
    const NodePins target_pins = graph::PinsOf(*target, graph);
    const auto from = std::find_if(source_pins.outputs.begin(), source_pins.outputs.end(), [&](const Pin& pin) { return pin.name == link.from.pin; });
    const auto to = std::find_if(target_pins.inputs.begin(), target_pins.inputs.end(), [&](const Pin& pin) { return pin.name == link.to.pin; });
    if (from == source_pins.outputs.end() || to == target_pins.inputs.end()) continue;

    const PinType carried = graph::OutputType(graph, link.from);
    const bool fits = graph::Fits(carried, to->type);
    const ImVec4 color = fits ? ImGui::ColorConvertU32ToFloat4(PinColor(carried.kind)) : kRejectColor;
    ed::Link(LinkId(link.id), PinId(link.from.node, static_cast<size_t>(from - source_pins.outputs.begin()), true), PinId(link.to.node, static_cast<size_t>(to - target_pins.inputs.begin()), false), color, kLinkThickness);

    // Light flows along exec links now and then, and along every link of the node being worked on.
    const bool exec = from->type.kind == PinKind::kExec;
    const bool active = style_.IsNodeActive(link.from.node) || style_.IsNodeActive(link.to.node);
    if ((exec && style_.Pulse(kExecPulseSeconds)) || (active && style_.Pulse(kActivePulseSeconds))) ed::Flow(LinkId(link.id));
  }

  HandleCreate();
  HandleDelete();
  HandleClipboard();

  if (navigate_to_) {
    ed::ClearSelection();
    ed::SelectNode(NodeId(navigate_to_));
    ed::NavigateToSelection(false, 0.3f);
    navigate_to_ = 0;
  }

  // Popups and tooltips draw in screen space, outside the canvas transform.
  ed::Suspend();
  if (!pin_tooltip_.empty() && !ImGui::IsPopupOpen(kAddMenu)) ImGui::SetTooltip("%s", pin_tooltip_.c_str());
  if (ed::ShowBackgroundContextMenu()) {
    pending_pin_.reset();
    open_add_menu_ = true;
  }
  if (open_add_menu_) {
    open_add_menu_ = false;
    const ImVec2 position = ed::ScreenToCanvas(ImGui::GetMousePos());
    add_x_ = position.x;
    add_y_ = position.y;
    add_filter_.clear();
    ImGui::OpenPopup(kAddMenu);
  }
  DrawAddMenu();
  ed::Resume();

  SyncFromCanvas();
  if (fit_view_) {
    ed::NavigateToContent(0.0f);
    fit_view_ = false;
  }
  ed::End();
  ed::SetCurrentEditor(nullptr);

  if (graph.nodes().empty()) ImGui::GetWindowDrawList()->AddText(ImVec2(canvas_min.x + 16.0f, canvas_min.y + 12.0f), IM_COL32(255, 255, 255, 110), "Right click to add a node. Start with an event: On Frame, Key Pressed, Before Function.");
}

void NodeEditorWindow::DrawNode(Node& node) {
  const graph::Graph& graph = document_->graph();
  const NodeDesc* desc = graph::FindDesc(node);
  const NodePins pins = graph::PinsOf(node, graph);

  style_.BeginNode(node.id);
  ed::BeginNode(NodeId(node.id));
  ImGui::PushID(node.id);

  const float content_x = ImGui::GetCursorPosX();
  ImGui::TextColored(kTitleColor, "%s", graph::TitleOf(node).c_str());
  const float title_width = ImGui::GetItemRectSize().x;
  const float header_bottom = ImGui::GetItemRectMax().y;
  ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));

  float inputs_width = 0.0f;
  if (!pins.inputs.empty()) ImGui::BeginGroup();
  for (size_t index = 0; index < pins.inputs.size(); ++index) {
    const Pin& pin = pins.inputs[index];
    const bool linked = graph.HasLinkAt({node.id, pin.name}, false);
    ed::BeginPin(PinId(node.id, index, false), ed::PinKind::Input);
    node_widgets::PinIcon(linked, PinColor(pin.type.kind), ShapeOf(pin));
    ed::EndPin();
    if (ImGui::IsItemHovered()) pin_tooltip_ = pin.name + ": " + graph::TypeName(pin.type);

    const std::string_view label = PinLabel(pin);
    if (!label.empty()) {
      ImGui::SameLine();
      ImGui::TextUnformatted(label.data(), label.data() + label.size());
    }
    // Unlinked values are typed in place, beside the pin so a click doesn't start a link.
    if (!linked) DrawInlineValue(node, pin);
  }
  if (!pins.inputs.empty()) {
    ImGui::EndGroup();
    inputs_width = ImGui::GetItemRectSize().x;
  }

  if (!pins.outputs.empty()) {
    float column_width = 0.0f;
    for (const Pin& pin : pins.outputs) column_width = std::max(column_width, node_widgets::OutputRowWidth(PinLabel(pin)));
    const float column_x = node_widgets::PlaceOutputColumn(content_x, title_width, inputs_width, column_width);
    ImGui::BeginGroup();
    for (size_t index = 0; index < pins.outputs.size(); ++index) {
      const Pin& pin = pins.outputs[index];
      const std::string_view label = PinLabel(pin);
      // A Number output shows what it resolves to.
      const PinType carried = pin.type.kind == PinKind::kNumber ? graph::OutputType(graph, {node.id, pin.name}) : pin.type;
      node_widgets::AlignOutputRow(column_x, column_width, label);
      ed::BeginPin(PinId(node.id, index, true), ed::PinKind::Output);
      if (!label.empty()) {
        ImGui::TextUnformatted(label.data(), label.data() + label.size());
        ImGui::SameLine();
      }
      node_widgets::PinIcon(graph.HasLinkAt({node.id, pin.name}, true), PinColor(carried.kind), ShapeOf(pin));
      ed::EndPin();
      if (ImGui::IsItemHovered()) pin_tooltip_ = pin.name + ": " + graph::TypeName(carried);
    }
    ImGui::EndGroup();
  }

  ImGui::PopID();
  ed::EndNode();
  style_.EndNode(node.id, HeaderColor(desc, pins), header_bottom, problem_nodes_.contains(node.id));
}

void NodeEditorWindow::DrawInlineValue(Node& node, const Pin& pin) {
  const PinKind kind = pin.type.kind;
  if (kind == PinKind::kExec || kind == PinKind::kObject || kind == PinKind::kRegisters) return;

  const auto stored = node.values.find(pin.name);
  const nlohmann::json value = stored != node.values.end() ? stored->second : pin.initial;
  const float font = ImGui::GetFontSize();
  const std::string id = "##" + pin.name;

  ImGui::SameLine();
  if (kind == PinKind::kBool) {
    bool checked = value.is_boolean() && value.get<bool>();
    if (ImGui::Checkbox(id.c_str(), &checked)) {
      node.values[pin.name] = checked;
      Edited();
    }
    return;
  }

  if (kind == PinKind::kFloat) {
    float number = value.is_number() ? value.get<float>() : 0.0f;
    ImGui::SetNextItemWidth(font * kInlineFloatWidth);
    if (ImGui::DragFloat(id.c_str(), &number, 0.01f, 0.0f, 0.0f, "%.3g")) {
      node.values[pin.name] = number;
      Edited(false);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) Edited();
    return;
  }

  // Ints and numbers are typed as text, so hex addresses stay hex.
  std::string text;
  TextValue(value, text);
  ImGui::SetNextItemWidth(font * (kind == PinKind::kInt || kind == PinKind::kNumber ? kInlineNumberWidth : kInlineTextWidth));
  const char* hint = kind == PinKind::kInt && pin.name == "Address" ? "0x82000000" : nullptr;
  if (node_widgets::InputString(id.c_str(), text, 0, hint)) {
    node.values[pin.name] = text;
    Edited(false);
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) Edited();
}

void NodeEditorWindow::HandleCreate() {
  // EndCreate only follows a BeginCreate that returned true.
  if (!ed::BeginCreate(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), kLinkThickness)) return;
  graph::Graph& graph = document_->graph();

  const auto resolve = [&graph](const DecodedPin& decoded) -> std::optional<PinRef> {
    const Node* node = graph.FindNode(decoded.node);
    if (!node) return std::nullopt;
    const NodePins pins = graph::PinsOf(*node, graph);
    const std::vector<Pin>& side = decoded.output ? pins.outputs : pins.inputs;
    if (decoded.index >= side.size()) return std::nullopt;
    return PinRef{decoded.node, side[decoded.index].name};
  };

  // New links: output to input, dragged either way.
  ed::PinId start_id;
  ed::PinId end_id;
  if (ed::QueryNewLink(&start_id, &end_id)) {
    std::optional<DecodedPin> start = DecodePin(start_id);
    std::optional<DecodedPin> end = DecodePin(end_id);
    if (start && end) {
      if (!start->output) std::swap(start, end);
      const std::optional<PinRef> from = start->output ? resolve(*start) : std::nullopt;
      const std::optional<PinRef> to = !end->output ? resolve(*end) : std::nullopt;
      std::string reason;
      if (!from || !to || !graph::CanConnect(graph, *from, *to, &reason)) {
        ed::RejectNewItem(kRejectColor, kLinkThickness);
        if (!reason.empty()) {
          ed::Suspend();
          ImGui::SetTooltip("%s", reason.c_str());
          ed::Resume();
        }
      } else if (ed::AcceptNewItem(kAcceptColor, kLinkThickness + 1.0f) && graph::Connect(graph, *from, *to)) {
        Edited();
      }
    }
  }

  // A link dropped on empty space: add a node there, connected to the pin.
  ed::PinId pin_id;
  if (ed::QueryNewNode(&pin_id) && ed::AcceptNewItem()) {
    if (const std::optional<DecodedPin> decoded = DecodePin(pin_id)) {
      if (const std::optional<PinRef> ref = resolve(*decoded)) {
        pending_pin_ = PendingPin{*ref, decoded->output};
        open_add_menu_ = true;
      }
    }
  }
  ed::EndCreate();
}

void NodeEditorWindow::HandleDelete() {
  // EndDelete only follows a BeginDelete that returned true.
  if (!ed::BeginDelete()) return;
  graph::Graph& graph = document_->graph();
  bool changed = false;

  ed::LinkId link_id;
  while (ed::QueryDeletedLink(&link_id)) {
    if (ed::AcceptDeletedItem()) {
      graph.RemoveLink(static_cast<int>(link_id.Get() - kLinkBase));
      changed = true;
    }
  }
  ed::NodeId node_id;
  while (ed::QueryDeletedNode(&node_id)) {
    if (ed::AcceptDeletedItem()) {
      const int id = static_cast<int>(node_id.Get());
      graph.RemoveNode(id);
      if (selected_node_ == id) selected_node_ = 0;
      changed = true;
    }
  }
  ed::EndDelete();
  if (changed) Edited();
}

void NodeEditorWindow::HandleClipboard() {
  using node_widgets::ClipboardAction;
  const node_widgets::ClipboardRequest request = node_widgets::TakeClipboardRequest();
  graph::Graph& graph = document_->graph();

  switch (request.action) {
    case ClipboardAction::kCopy:
    case ClipboardAction::kCut:
      if (request.nodes.empty()) break;
      ImGui::SetClipboardText(graph::WriteNodes(graph, request.nodes).c_str());
      if (request.action == ClipboardAction::kCut) {
        for (int id : request.nodes) graph.RemoveNode(id);
        if (std::find(request.nodes.begin(), request.nodes.end(), selected_node_) != request.nodes.end()) selected_node_ = 0;
        Edited();
      }
      break;
    case ClipboardAction::kPaste:
      // Between ed::Begin and ed::End, outside Suspend, the mouse is already in canvas units.
      if (const char* text = ImGui::GetClipboardText()) PasteNodes(text, ImGui::GetMousePos().x, ImGui::GetMousePos().y);
      break;
    case ClipboardAction::kDuplicate: {
      // A copy beside the original, leaving the clipboard alone.
      float x = std::numeric_limits<float>::max();
      float y = std::numeric_limits<float>::max();
      for (int id : request.nodes) {
        if (const Node* node = graph.FindNode(id)) {
          x = std::min(x, node->x);
          y = std::min(y, node->y);
        }
      }
      if (!request.nodes.empty()) PasteNodes(graph::WriteNodes(graph, request.nodes), x + kDuplicateOffset, y + kDuplicateOffset);
      break;
    }
    case ClipboardAction::kNone: break;
  }
}

void NodeEditorWindow::PasteNodes(const std::string& text, float x, float y) {
  graph::Graph copied;
  if (!graph::ReadNodes(text, copied)) return;
  graph::Graph& graph = document_->graph();

  float left = std::numeric_limits<float>::max();
  float top = std::numeric_limits<float>::max();
  for (const Node& node : copied.nodes()) {
    left = std::min(left, node.x);
    top = std::min(top, node.y);
  }

  // The copies get new ids, and their links are made again between them.
  std::map<int, int> new_ids;
  std::vector<int> pasted;
  for (const Node& node : copied.nodes()) {
    Node& added = graph.AddNode(node.type, x + node.x - left, y + node.y - top);
    added.settings = node.settings;
    added.values = node.values;
    new_ids[node.id] = added.id;
    pasted.push_back(added.id);
    ed::SetNodePosition(NodeId(added.id), ImVec2(added.x, added.y));
  }
  for (const graph::Link& link : copied.links()) {
    const auto from = new_ids.find(link.from.node);
    const auto to = new_ids.find(link.to.node);
    if (from != new_ids.end() && to != new_ids.end()) graph::Connect(graph, {from->second, link.from.pin}, {to->second, link.to.pin});
  }
  node_widgets::SelectNodes(pasted);
  selected_node_ = pasted.size() == 1 ? pasted.front() : 0;
  focus_ = Focus::kNode;
  Edited();
}

void NodeEditorWindow::DrawAddMenu() {
  if (!ImGui::BeginPopup(kAddMenu)) return;
  const graph::Graph& graph = document_->graph();

  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
  node_widgets::InputString("##search", add_filter_, 0, "Search nodes");

  // Every entry: a node type, with the setting that picks a variable for Get and Set.
  struct Entry {
    std::string label;
    std::string category;
    std::string type;
    std::string setting;
    std::string value;
    const std::string* description;
  };
  std::vector<Entry> entries;
  const auto offer = [&](const NodeDesc& desc, std::string label, std::string setting, std::string value) {
    // Dragged from a pin: only nodes with a pin that fits it.
    if (pending_pin_) {
      const NodePins pins = graph::PinsOf(Prototype(desc, setting, value), graph);
      const Node* from = graph.FindNode(pending_pin_->ref.node);
      if (!from) return;
      const NodePins from_pins = graph::PinsOf(*from, graph);
      const Pin* pending = graph::FindPin(pending_pin_->output ? from_pins.outputs : from_pins.inputs, pending_pin_->ref.pin);
      if (!pending) return;
      const PinType pending_type = pending_pin_->output ? graph::OutputType(graph, pending_pin_->ref) : pending->type;
      const std::vector<Pin>& side = pending_pin_->output ? pins.inputs : pins.outputs;
      const bool fits = std::any_of(side.begin(), side.end(), [&](const Pin& pin) { return pending_pin_->output ? MayFit(pending_type, pin.type) : MayFit(pin.type, pending_type); });
      if (!fits) return;
    }
    entries.push_back({std::move(label), desc.category, desc.id, std::move(setting), std::move(value), &desc.description});
  };
  for (const std::unique_ptr<NodeDesc>& desc : graph::Library::Get().nodes()) {
    if (desc->id == "var.get" || desc->id == "var.set") {
      for (const graph::Variable& variable : graph.variables()) offer(*desc, desc->title + " " + variable.name, "Variable", variable.name);
      continue;
    }
    offer(*desc, desc->title, {}, {});
  }

  const Entry* chosen = nullptr;
  const auto item = [&](const Entry& entry, const std::string& label) {
    const bool picked = ImGui::MenuItem(label.c_str());
    if (ImGui::IsItemHovered() && !entry.description->empty()) ImGui::SetTooltip("%s", entry.description->c_str());
    return picked;
  };

  if (!add_filter_.empty()) {
    // A flat list of matches; Enter takes the first.
    bool first = true;
    for (const Entry& entry : entries) {
      if (!node_widgets::ContainsIgnoringCase(entry.label, add_filter_) && !node_widgets::ContainsIgnoringCase(entry.category, add_filter_)) continue;
      if (item(entry, entry.label + "##" + entry.type + entry.value) || (first && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        chosen = &entry;
        break;
      }
      first = false;
    }
    if (first) ImGui::TextDisabled("Nothing matches");
  } else {
    // Categories nest at their slashes: Game/tip/Functions.
    const std::function<void(const std::string&)> level = [&](const std::string& prefix) {
      std::vector<std::string> children;
      for (const Entry& entry : entries) {
        if (entry.category.size() <= prefix.size() || entry.category.compare(0, prefix.size(), prefix) != 0) continue;
        const std::string rest = entry.category.substr(prefix.size());
        const std::string child = rest.substr(0, rest.find('/'));
        if (std::find(children.begin(), children.end(), child) == children.end()) children.push_back(child);
      }
      for (const std::string& child : children) {
        if (!ImGui::BeginMenu(child.c_str())) continue;
        level(prefix + child + "/");
        ImGui::EndMenu();
      }
      const std::string own = prefix.empty() ? std::string() : prefix.substr(0, prefix.size() - 1);
      for (const Entry& entry : entries) {
        if (!prefix.empty() && entry.category == own && item(entry, entry.label + "##" + entry.type + entry.value)) chosen = &entry;
      }
    };
    level({});
    if (entries.empty()) ImGui::TextDisabled("No node fits that pin");
  }

  if (chosen) {
    AddNode(chosen->type, chosen->setting, chosen->value);
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void NodeEditorWindow::AddNode(const std::string& type, const std::string& setting, const std::string& value) {
  graph::Graph& graph = document_->graph();
  const NodeDesc* desc = graph::Library::Get().Find(type);
  if (!desc) return;
  Node& node = graph.AddNode(type, add_x_, add_y_);
  node.settings = Prototype(*desc, setting, value).settings;
  const int id = node.id;
  ed::SetNodePosition(NodeId(id), ImVec2(add_x_, add_y_));

  // Opened from a dragged link: connect through the first pin that fits.
  if (pending_pin_) {
    const NodePins pins = graph::PinsOf(*graph.FindNode(id), graph);
    for (const Pin& pin : pending_pin_->output ? pins.inputs : pins.outputs) {
      const PinRef ref{id, pin.name};
      if (pending_pin_->output ? graph::Connect(graph, pending_pin_->ref, ref) : graph::Connect(graph, ref, pending_pin_->ref)) break;
    }
    pending_pin_.reset();
  }

  // The new node is selected, so its details show at once.
  ed::ClearSelection();
  ed::SelectNode(NodeId(id));
  selected_node_ = id;
  focus_ = Focus::kNode;
  Edited();
}

void NodeEditorWindow::SyncFromCanvas() {
  // Positions are kept in the graph. A drag is one undo step, taken when the mouse lets go.
  for (Node& node : document_->graph().nodes()) {
    const ImVec2 position = ed::GetNodePosition(NodeId(node.id));
    if (position.x != node.x || position.y != node.y) {
      node.x = position.x;
      node.y = position.y;
      moving_ = true;
    }
  }
  if (moving_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    moving_ = false;
    Edited();
  }

  // Selecting a node shows it in the details.
  ed::NodeId selected[1];
  const int node = ed::GetSelectedNodes(selected, 1) > 0 ? static_cast<int>(selected[0].Get()) : 0;
  if (node != selected_node_) {
    selected_node_ = node;
    if (node) focus_ = Focus::kNode;
    else if (focus_ == Focus::kNode) focus_ = Focus::kNone;
  }
}

void NodeEditorWindow::DrawLua() {
  if (lua_view_ != document_->compiled().lua) lua_view_ = document_->compiled().lua;
  if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(lua_view_.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("What the graph saves as scripts/%s.graph.lua", document_->name().c_str());
  node_widgets::InputStringMultiline("##lua", lua_view_, ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);
}

// Right -------------------------------------------------------------------------------------------------

void NodeEditorWindow::DrawDetails() {
  if (!document_) {
    ImGui::TextWrapped("Graphs are saved in the mod's graphs folder, each with the Lua script it makes in scripts. The script is an ordinary Lua mod.");
    return;
  }
  switch (focus_) {
    case Focus::kNode: DrawNodeDetails(); break;
    case Focus::kVariable: DrawVariableDetails(); break;
    case Focus::kNone: ImGui::TextDisabled("Select a node or a variable to edit it here."); break;
  }
  DrawProblems();
}

void NodeEditorWindow::DrawNodeDetails() {
  Node* node = document_->graph().FindNode(selected_node_);
  const NodeDesc* desc = node ? graph::FindDesc(*node) : nullptr;
  if (!node) return;
  ImGui::SeparatorText(graph::TitleOf(*node).c_str());
  if (!desc) {
    ImGui::TextColored(kErrorColor, "There's no %s node; the game may not declare it any more.", node->type.c_str());
    return;
  }
  ImGui::TextWrapped("%s", desc->description.c_str());
  ImGui::TextDisabled("n%d in the Lua", node->id);

  for (const graph::SettingDesc& setting : desc->settings) {
    ImGui::PushID(setting.name.c_str());
    std::string value = graph::SettingOf(*node, setting.name);
    switch (setting.kind) {
      case graph::SettingKind::kAddress:
      case graph::SettingKind::kText:
        if (node_widgets::InputString(setting.name.c_str(), value, 0, setting.kind == graph::SettingKind::kAddress ? "0x82000000" : nullptr)) {
          node->settings[setting.name] = value;
          Edited(false);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) Edited();
        break;
      case graph::SettingKind::kChoice:
      case graph::SettingKind::kVariable: {
        std::vector<std::string> choices = setting.choices;
        if (setting.kind == graph::SettingKind::kVariable) {
          choices.clear();
          for (const graph::Variable& variable : document_->graph().variables()) choices.push_back(variable.name);
        }
        if (ImGui::BeginCombo(setting.name.c_str(), value.empty() ? "None" : value.c_str())) {
          for (const std::string& choice : choices) {
            if (ImGui::Selectable(choice.c_str(), choice == value) && choice != value) {
              node->settings[setting.name] = choice;
              Edited();
            }
          }
          ImGui::EndCombo();
        }
        break;
      }
    }
    if (!setting.tooltip.empty()) ImGui::SetItemTooltip("%s", setting.tooltip.c_str());
    ImGui::PopID();
  }
}

void NodeEditorWindow::DrawVariableDetails() {
  graph::Graph& graph = document_->graph();
  graph::Variable* variable = graph.FindVariable(selected_variable_);
  if (!variable) return;
  ImGui::SeparatorText("Variable");

  std::string name = variable->name;
  if (node_widgets::InputString("Name", name, ImGuiInputTextFlags_EnterReturnsTrue) && name != variable->name) {
    std::string reason;
    if (graph.FindVariable(name)) {
      SetStatus("There's a variable named " + name + " already", true);
    } else if (!graph::IsVariableName(name, graph::GameGlobals(), &reason)) {
      SetStatus(reason, true);
    } else {
      // Its Get and Set nodes follow.
      for (Node& node : graph.nodes()) {
        if ((node.type == "var.get" || node.type == "var.set") && node.Setting("Variable") == variable->name) node.settings["Variable"] = name;
      }
      variable->name = name;
      selected_variable_ = name;
      Edited();
    }
  }
  ImGui::SetItemTooltip("Press Enter to rename. Its Get and Set nodes follow");

  if (ImGui::BeginCombo("Type", KindLabel(variable->type))) {
    for (PinKind kind : kVariableKinds) {
      if (ImGui::Selectable(KindLabel(kind), kind == variable->type) && kind != variable->type) {
        variable->type = kind;
        variable->initial = graph::DefaultValue(kind);
        Edited();
      }
    }
    ImGui::EndCombo();
  }

  // The value it starts with, as the script loads.
  switch (variable->type) {
    case PinKind::kBool: {
      bool checked = variable->initial.is_boolean() && variable->initial.get<bool>();
      if (ImGui::Checkbox("Starts as", &checked)) {
        variable->initial = checked;
        Edited();
      }
      break;
    }
    case PinKind::kFloat: {
      double number = variable->initial.is_number() ? variable->initial.get<double>() : 0.0;
      if (ImGui::InputDouble("Starts as", &number, 0.0, 0.0, "%.6g")) {
        variable->initial = number;
        Edited(false);
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) Edited();
      break;
    }
    case PinKind::kAny: ImGui::TextDisabled("Starts as nil"); break;
    default: {
      std::string text;
      TextValue(variable->initial, text);
      if (node_widgets::InputString("Starts as", text)) {
        variable->initial = text;
        Edited(false);
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) Edited();
    }
  }

  if (node_widgets::InputStringMultiline("Comment", variable->comment, ImVec2(0.0f, ImGui::GetTextLineHeight() * 3.0f))) Edited(false);
  if (ImGui::IsItemDeactivatedAfterEdit()) Edited();
}

void NodeEditorWindow::DrawProblems() {
  const std::vector<graph::Problem>& problems = document_->compiled().problems;
  if (!problems.empty()) {
    ImGui::SeparatorText("Problems");
    for (size_t i = 0; i < problems.size(); ++i) {
      const graph::Problem& problem = problems[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::PushStyleColor(ImGuiCol_Text, problem.warning ? kWarningColor : kErrorColor);
      const Node* node = document_->graph().FindNode(problem.node);
      const std::string text = node ? graph::TitleOf(*node) + ": " + problem.message : problem.message;
      ImGui::PushTextWrapPos(0.0f);
      if (ImGui::Selectable(text.c_str()) && node) navigate_to_ = node->id;
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
      if (node) ImGui::SetItemTooltip("Click to find it on the canvas");
      ImGui::PopID();
    }
  }

  // What happened when the scripts last ran.
  for (const lua::ScriptMod& loaded : lua::Loaded()) {
    if (loaded.name != mod_folder_ || loaded.error.empty()) continue;
    ImGui::SeparatorText("Last run");
    ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
    ImGui::TextWrapped("%s", loaded.error.c_str());
    ImGui::PopStyleColor();
  }
}

// Mods and graphs ----------------------------------------------------------------------------------------

const ModInfo* NodeEditorWindow::Mod() const { return mod_folder_.empty() ? nullptr : model_.mods().Find(mod_folder_); }

void NodeEditorWindow::OpenMod(const std::string& folder) {
  mod_folder_ = folder;
  Last() = {folder, {}};
  document_.reset();
  RefreshGraphs();
  if (!graphs_.empty()) OpenGraph(graphs_.front());
}

void NodeEditorWindow::OpenGraph(const std::string& name) {
  const ModInfo* mod = Mod();
  if (!mod) return;
  auto document = std::make_unique<graph::Document>(mod->dir, name);
  std::string error;
  if (!document->Load(&error)) {
    SetStatus("Couldn't open " + name + ": " + error, true);
    return;
  }
  document_ = std::move(document);
  Last() = {mod_folder_, name};
  focus_ = Focus::kNone;
  selected_node_ = 0;
  selected_variable_.clear();
  pending_pin_.reset();
  ResetCanvas();
}

void NodeEditorWindow::ResetCanvas() {
  // A canvas per graph, so each opens fresh, fitted to its nodes.
  if (canvas_) ed::DestroyEditor(canvas_);
  // Node positions are kept in the graph, so the canvas keeps nothing in imgui.ini.
  ed::Config config;
  config.SettingsName = nullptr;
  canvas_ = ed::CreateEditor(&config);
  ed::SetCurrentEditor(canvas_);
  NodeGraphStyle::Apply();
  ed::SetCurrentEditor(nullptr);
  style_ = NodeGraphStyle();
  place_nodes_ = true;
  fit_view_ = true;
  moving_ = false;
}

void NodeEditorWindow::Save(bool reload) {
  if (!document_) return;
  std::string error;
  if (!document_->Save(&error)) {
    SetStatus(error, true);
    return;
  }
  RefreshGraphs();
  const ModInfo* mod = Mod();
  if (!reload) {
    SetStatus("Saved " + document_->name());
    return;
  }
  model_.ReloadScripts();
  if (mod && !mod->enabled) SetStatus("Saved. " + mod->name + " is off, so its scripts don't run; turn it on in the mods menu", true);
  else if (!document_->compiled().ok()) SetStatus("Saved and reloaded, but this graph has problems, so it doesn't run", true);
  else SetStatus("Saved and reloaded");
}

void NodeEditorWindow::CreateMod(const std::string& name) {
  if (!graph::IsFileName(name)) {
    SetStatus("A mod's name can have letters, digits, spaces, _ and -", true);
    return;
  }
  const std::filesystem::path dir = model_.mods().root() / name;
  std::error_code code;
  if (std::filesystem::exists(dir, code)) {
    SetStatus("There's a mod folder named " + name + " already", true);
    return;
  }
  std::filesystem::create_directories(dir / "scripts", code);
  if (code) {
    SetStatus("Couldn't make " + dir.string() + ": " + code.message(), true);
    return;
  }
  std::ofstream(dir / "mod.toml") << "name = \"" << name << "\"\nversion = \"1.0.0\"\ndescription = \"Made with the node editor\"\n";
  // Enabled at the end of the load order, and the menus rescan.
  SetModEnabled(name, true);
  model_.Poll();
  OpenMod(name);
  CreateGraph("Main");
}

void NodeEditorWindow::CreateGraph(const std::string& name) {
  const ModInfo* mod = Mod();
  if (!mod) return;
  if (!graph::IsFileName(name)) {
    SetStatus("A graph's name can have letters, digits, spaces, _ and -", true);
    return;
  }
  if (std::find(graphs_.begin(), graphs_.end(), name) != graphs_.end()) {
    SetStatus("There's a graph named " + name + " already", true);
    return;
  }
  document_ = std::make_unique<graph::Document>(mod->dir, name);
  Last() = {mod_folder_, name};
  focus_ = Focus::kNone;
  selected_node_ = 0;
  selected_variable_.clear();
  ResetCanvas();
  Save(false);
}

void NodeEditorWindow::RenameGraph(const std::string& name) {
  if (!document_ || name == document_->name()) return;
  if (!graph::IsFileName(name)) {
    SetStatus("A graph's name can have letters, digits, spaces, _ and -", true);
    return;
  }
  // Saved first, so what's renamed is what's on screen.
  std::string error;
  if (!document_->Save(&error) || !graph::RenameGraph(document_->mod_dir(), document_->name(), name, &error)) {
    SetStatus(error, true);
    return;
  }
  RefreshGraphs();
  OpenGraph(name);
  SetStatus("Renamed to " + name);
}

void NodeEditorWindow::DeleteGraph() {
  if (!document_) return;
  std::string error;
  const std::string name = document_->name();
  if (!graph::DeleteGraph(document_->mod_dir(), name, &error)) {
    SetStatus(error, true);
    return;
  }
  document_.reset();
  RefreshGraphs();
  if (!graphs_.empty()) OpenGraph(graphs_.front());
  SetStatus("Deleted " + name);
}

void NodeEditorWindow::RefreshGraphs() {
  const ModInfo* mod = Mod();
  graphs_ = mod ? graph::ListGraphs(mod->dir) : std::vector<std::string>{};
}

void NodeEditorWindow::AfterSaving(std::function<void()> action) {
  if (!document_ || !document_->dirty()) {
    action();
    return;
  }
  after_saving_ = std::move(action);
  open_save_prompt_ = true;
}

void NodeEditorWindow::AskFor(Ask ask, std::string initial) {
  ask_ = ask;
  ask_text_ = std::move(initial);
  open_ask_ = true;
}

void NodeEditorWindow::DrawPopups() {
  // Opened here, at the window's level, so they open the same from the toolbar, the sidebar or the canvas.
  if (open_ask_) {
    open_ask_ = false;
    ImGui::OpenPopup(kAskPopup);
  }
  if (open_save_prompt_) {
    open_save_prompt_ = false;
    ImGui::OpenPopup(kSavePopup);
  }

  const char* titles[] = {"", "New Mod", "New Graph", "Rename Graph", "Delete Graph"};
  const std::string ask_title = std::string(titles[static_cast<int>(ask_)]) + kAskPopup;
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 24.0f, 0.0f));
  if (ImGui::BeginPopupModal(ask_title.c_str(), nullptr, ImGuiWindowFlags_NoResize)) {
    bool done = false;
    if (ask_ == Ask::kDeleteGraph) {
      ImGui::TextWrapped("Delete %s and its script? This can't be undone.", ask_text_.c_str());
      if (ImGui::Button("Delete")) {
        DeleteGraph();
        done = true;
      }
    } else {
      if (ask_ == Ask::kNewMod) ImGui::TextWrapped("A folder in the mods folder, with a graph to start.");
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      const bool entered = node_widgets::InputString("Name", ask_text_, ImGuiInputTextFlags_EnterReturnsTrue);
      if (ImGui::Button("OK") || entered) {
        const std::string name = ask_text_;
        if (ask_ == Ask::kNewMod) AfterSaving([this, name] { CreateMod(name); });
        if (ask_ == Ask::kNewGraph) AfterSaving([this, name] { CreateGraph(name); });
        if (ask_ == Ask::kRenameGraph) RenameGraph(name);
        done = true;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) done = true;
    if (done) {
      ask_ = Ask::kNone;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal(kSavePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Save the changes to %s first?", document_ ? document_->name().c_str() : "the graph");
    std::function<void()> next;
    if (ImGui::Button("Save")) {
      Save(false);
      if (document_ && !document_->dirty()) next = std::move(after_saving_);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save")) {
      next = std::move(after_saving_);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      after_saving_ = {};
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    // After EndPopup, as it may close the window.
    if (next) next();
  }
}

void NodeEditorWindow::Edited(bool settled) {
  if (!document_) return;
  document_->Edited(settled);
}

void NodeEditorWindow::Undo() {
  if (!document_ || !document_->Undo()) return;
  place_nodes_ = true;
  if (!document_->graph().FindNode(selected_node_)) selected_node_ = 0;
  if (!document_->graph().FindVariable(selected_variable_)) selected_variable_.clear();
}

void NodeEditorWindow::Redo() {
  if (!document_ || !document_->Redo()) return;
  place_nodes_ = true;
  if (!document_->graph().FindNode(selected_node_)) selected_node_ = 0;
  if (!document_->graph().FindVariable(selected_variable_)) selected_variable_.clear();
}

void NodeEditorWindow::SetStatus(std::string text, bool error) {
  status_ = std::move(text);
  status_error_ = error;
  status_time_ = ImGui::GetTime();
}

}  // namespace rexfabric::ui
