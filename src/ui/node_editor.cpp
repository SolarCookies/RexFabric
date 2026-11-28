#include "node_editor.h"

#include <algorithm>
#include <cfloat>
#include <fstream>

#include <imgui.h>

#include <rex/hid/input_system.h>
#include <rexfabric/rexfabric.h>

#include "glass.h"
#include "node_graph_widgets.h"

namespace rexfabric::ui {
namespace {

// In multiples of the font size, so the layout follows the UI's scale.
constexpr float kSidebarWidth = 15.0f;
constexpr float kDetailsWidth = 20.0f;
// How long a status message stays.
constexpr double kStatusSeconds = 5.0;
// A status line that isn't an error; errors and warnings are in the view's colors.
constexpr ImVec4 kGoodColor{0.5f, 0.9f, 0.5f, 1.0f};
// The glass of the panels beside the canvas.
constexpr ImVec4 kPaneTint{0.05f, 0.06f, 0.11f, 0.42f};

constexpr const char* kAskPopup = "###ask";
constexpr const char* kSavePopup = "Unsaved changes###save";

rex::hid::IInputSystem* GuestInput() { return REX_INTERFACE(rex::hid::IInputSystem); }

// The mod and graph last open, so the window opens again where it was.
struct LastOpen {
  std::string mod;
  std::string graph;
};

LastOpen& Last() {
  static LastOpen last;
  return last;
}

}  // namespace

NodeEditorWindow::NodeEditorWindow(MenuModel& model, std::function<void()> closed) : model_(model), closed_(std::move(closed)), view_({[this](std::string text, bool error) { SetStatus(std::move(text), error); }, {}}) {
  if (auto* input = GuestInput()) input->AcquireUIBlock();
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
  if (auto* input = GuestInput()) input->ReleaseUIBlock();
  if (closed_) closed_();
}

void NodeEditorWindow::Hide() {
  AfterSaving([this] { closing_ = true; });
}

bool NodeEditorWindow::Draw(ImGuiIO& io) {
  if (closing_) return false;
  OnDraw(io);
  return !closing_;
}

void NodeEditorWindow::OnDraw(ImGuiIO& io) {
  widgets::SyncModifiers(io);
  model_.Poll();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(1320.0f, 780.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(820.0f, 480.0f), ImVec2(FLT_MAX, FLT_MAX));
  // Glass all over when there's glass: the theme, the window, and the canvas's light behind everything in it.
  const glass::Theme theme;
  bool open = true;
  const std::string title = document_ ? "Node Editor: " + document_->name() + (document_->dirty() ? " *" : "") + "###rexfabric_node_editor" : std::string("Node Editor###rexfabric_node_editor");
  if (glass::BeginWindow(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    // Under the title bar, to the window's rounded bottom.
    const ImVec2 window_min = ImGui::GetWindowPos();
    const ImVec2 window_max(window_min.x + ImGui::GetWindowSize().x, window_min.y + ImGui::GetWindowSize().y);
    view_.DrawBackdrop(ImVec2(window_min.x, window_min.y + ImGui::GetFrameHeight()), window_max, ImGui::GetStyle().WindowRounding);

    Shortcuts();
    DrawToolbar();
    ImGui::Spacing();

    const float font = ImGui::GetFontSize();
    glass::BeginChild("##left", ImVec2(font * kSidebarWidth, 0.0f), ImGuiChildFlags_ResizeX | ImGuiChildFlags_AlwaysUseWindowPadding, 0, kPaneTint);
    DrawGraphs();
    view_.DrawSidebar();
    ImGui::EndChild();

    ImGui::SameLine();
    const float middle = std::max(ImGui::GetContentRegionAvail().x - font * kDetailsWidth - ImGui::GetStyle().ItemSpacing.x, font * 10.0f);
    ImGui::BeginChild("##middle", ImVec2(middle, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!document_) {
      ImGui::TextDisabled(mod_folder_.empty() ? "Pick a mod, or make a new one, to start." : "Make a graph for this mod to start.");
    } else if (ImGui::BeginTabBar("##views")) {
      if (ImGui::BeginTabItem("Graph")) {
        view_.DrawBreadcrumb();
        view_.DrawCanvas();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Lua")) {
        view_.DrawLua("scripts/" + document_->name() + ".graph.lua");
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    glass::BeginChild("##details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, 0, kPaneTint);
    if (document_) view_.DrawDetails();
    else ImGui::TextWrapped("Graphs are saved in the mod's graphs folder, each with the Lua script it makes in scripts. The script is an ordinary Lua mod.");
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
  if (glass::Button("Save", glass::kAccent)) Save(false);
  ImGui::SetItemTooltip("Writes the graph and its script (Ctrl+S)");
  ImGui::SameLine();
  if (glass::Button("Save and Reload", glass::kCreate)) Save(true);
  ImGui::SetItemTooltip("Saves, then runs every enabled Lua mod again, this one included");
  ImGui::SameLine();
  ImGui::BeginDisabled(!document_ || !document_->CanUndo());
  if (glass::Button("Undo")) view_.Undo();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!document_ || !document_->CanRedo());
  if (glass::Button("Redo")) view_.Redo();
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (glass::Button("Content")) ShowContentBrowser();
  ImGui::SetItemTooltip("Opens the content browser: every mod's classes and folders");

  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  if (!status_.empty() && ImGui::GetTime() - status_time_ < kStatusSeconds) {
    ImGui::TextColored(status_error_ ? GraphView::kErrorColor : kGoodColor, "%s", status_.c_str());
  } else if (document_) {
    const auto& problems = document_->compiled().problems;
    const size_t errors = static_cast<size_t>(std::count_if(problems.begin(), problems.end(), [](const graph::Problem& problem) { return !problem.warning; }));
    if (errors) ImGui::TextColored(GraphView::kErrorColor, "%zu problem%s; it won't run until they're fixed", errors, errors == 1 ? "" : "s");
    else if (!problems.empty()) ImGui::TextColored(GraphView::kWarningColor, "%zu warning%s", problems.size(), problems.size() == 1 ? "" : "s");
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
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) io.KeyShift ? view_.Redo() : view_.Undo();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) view_.Redo();
}

// Left --------------------------------------------------------------------------------------------------

void NodeEditorWindow::DrawGraphs() {
  const std::string next = graphs_.empty() ? "Main" : "Graph" + std::to_string(graphs_.size() + 1);
  if (GraphView::SectionHeader("Graphs", "A new graph: a script of its own in this mod", Mod() != nullptr)) AskFor(Ask::kNewGraph, next);

  for (const std::string& name : graphs_) {
    const bool open = document_ && document_->name() == name;
    if (ImGui::Selectable((name + "##graph").c_str(), open)) {
      if (open) view_.ShowGraph(0);
      else AfterSaving([this, name] { OpenGraph(name); });
    }
    ImGui::SetItemTooltip("Right click to rename or delete");
    if (open && ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem("Rename...")) AskFor(Ask::kRenameGraph, name);
      if (ImGui::MenuItem("Delete...")) AskFor(Ask::kDeleteGraph, name);
      ImGui::EndPopup();
    }
    // The open graph's own event graph, under it, as its functions and macros are below.
    if (open) {
      ImGui::Indent();
      if (ImGui::Selectable("Event Graph", view_.owner() == 0)) view_.ShowGraph(0);
      ImGui::Unindent();
    }
  }
  if (Mod() && graphs_.empty()) ImGui::TextDisabled("None yet");
}

// Mods and graphs ----------------------------------------------------------------------------------------

const ModInfo* NodeEditorWindow::Mod() const { return mod_folder_.empty() ? nullptr : model_.mods().Find(mod_folder_); }

void NodeEditorWindow::OpenMod(const std::string& folder) {
  mod_folder_ = folder;
  Last() = {folder, {}};
  SetDocument(nullptr);
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
  SetDocument(std::move(document));
  Last() = {mod_folder_, name};
}

void NodeEditorWindow::SetDocument(std::unique_ptr<graph::Document> document) {
  document_ = std::move(document);
  view_.SetDocument(document_.get(), mod_folder_);
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
  SetDocument(std::make_unique<graph::Document>(mod->dir, name));
  Last() = {mod_folder_, name};
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
  SetDocument(nullptr);
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
      if (glass::Button("Delete", glass::kDanger)) {
        DeleteGraph();
        done = true;
      }
    } else {
      if (ask_ == Ask::kNewMod) ImGui::TextWrapped("A folder in the mods folder, with a graph to start.");
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      const bool entered = node_widgets::InputString("Name", ask_text_, ImGuiInputTextFlags_EnterReturnsTrue);
      if (glass::Button("OK", glass::kAccent) || entered) {
        const std::string name = ask_text_;
        if (ask_ == Ask::kNewMod) AfterSaving([this, name] { CreateMod(name); });
        if (ask_ == Ask::kNewGraph) AfterSaving([this, name] { CreateGraph(name); });
        if (ask_ == Ask::kRenameGraph) RenameGraph(name);
        done = true;
      }
    }
    ImGui::SameLine();
    if (glass::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) done = true;
    if (done) {
      ask_ = Ask::kNone;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal(kSavePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Save the changes to %s first?", document_ ? document_->name().c_str() : "the graph");
    std::function<void()> next;
    if (glass::Button("Save", glass::kAccent)) {
      Save(false);
      if (document_ && !document_->dirty()) next = std::move(after_saving_);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (glass::Button("Don't Save", glass::kDanger)) {
      next = std::move(after_saving_);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (glass::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      after_saving_ = {};
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    // After EndPopup, as it may close the window.
    if (next) next();
  }
}

void NodeEditorWindow::SetStatus(std::string text, bool error) {
  status_ = std::move(text);
  status_error_ = error;
  status_time_ = ImGui::GetTime();
}

}  // namespace rexfabric::ui
