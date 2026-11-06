// The node editor: makes Lua mods out of node graphs, in game. Built when the project attaches RexFabric
// WITH_NODE_EDITOR.
//
// It picks a mod and one of its graphs, edits the graph on a canvas, and saves it with the Lua it compiles to
// (graph_document.h), which Save and Reload then runs. Laid out like a blueprint editor: the mod's graphs and the
// graph's variables on the left, the canvas and the Lua it makes in the middle, the selected node or variable and
// the graph's problems on the right.
//
// On the canvas the wheel zooms, a right drag pans, a right click adds a node, and dragging a link into empty space
// adds a node connected to it. Delete removes the selection; Ctrl+C, Ctrl+X, Ctrl+V and Ctrl+D copy, cut, paste
// and duplicate. Ctrl+S saves, Ctrl+Z and Ctrl+Y undo and redo. Unlinked inputs are typed into on the node itself.

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "../graph/graph_document.h"
#include "menu_model.h"
#include "node_graph_style.h"

struct ImVec2;

namespace ax::NodeEditor {
struct EditorContext;
}

namespace rexfabric::ui {

class NodeEditorWindow : public rex::ui::ImGuiDialog {
 public:
  // `closed` runs as the window goes.
  NodeEditorWindow(rex::ui::ImGuiDrawer* drawer, MenuModel& model, std::function<void()> closed);
  ~NodeEditorWindow() override;

  // Closes the window, once unsaved changes are saved or let go.
  void Hide();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class Focus { kNone, kNode, kVariable };
  // The names the window asks for in a popup.
  enum class Ask { kNone, kNewMod, kNewGraph, kRenameGraph, kDeleteGraph };

  // A pin a link was dragged from into empty space; the node added next connects to it.
  struct PendingPin {
    graph::PinRef ref;
    bool output = false;
  };

  // Top
  void DrawToolbar();
  void DrawModPicker();
  void Shortcuts();

  // Left
  void DrawGraphs();
  void DrawVariables();

  // Middle
  void DrawCanvas();
  void DrawNode(graph::Node& node);
  void DrawInlineValue(graph::Node& node, const graph::Pin& pin);
  void HandleCreate();
  void HandleDelete();
  void HandleClipboard();
  void PasteNodes(const std::string& text, float x, float y);
  void DrawAddMenu();
  void AddNode(const std::string& type, const std::string& setting, const std::string& value);
  void SyncFromCanvas();
  void DrawLua();

  // Right
  void DrawDetails();
  void DrawNodeDetails();
  void DrawVariableDetails();
  void DrawProblems();

  // Mods and graphs
  const ModInfo* Mod() const;
  void OpenMod(const std::string& folder);
  void OpenGraph(const std::string& name);
  void Save(bool reload);
  void CreateMod(const std::string& name);
  void CreateGraph(const std::string& name);
  void RenameGraph(const std::string& name);
  void DeleteGraph();
  void RefreshGraphs();

  // Runs `action` now, or once the open graph's unsaved changes are saved or let go.
  void AfterSaving(std::function<void()> action);
  void DrawPopups();
  void AskFor(Ask ask, std::string initial);

  // After an edit to the graph. See graph::Document::Edited.
  void Edited(bool settled = true);
  void Undo();
  void Redo();
  void SetStatus(std::string text, bool error = false);
  void ResetCanvas();

  MenuModel& model_;
  std::function<void()> closed_;

  std::string mod_folder_;
  std::vector<std::string> graphs_;
  std::unique_ptr<graph::Document> document_;
  ax::NodeEditor::EditorContext* canvas_ = nullptr;
  NodeGraphStyle style_;

  Focus focus_ = Focus::kNone;
  int selected_node_ = 0;
  std::string selected_variable_;
  std::set<int> problem_nodes_;
  int navigate_to_ = 0;
  // What the pin under the mouse is, shown once the canvas is suspended.
  std::string pin_tooltip_;

  bool place_nodes_ = true;
  bool fit_view_ = true;
  bool moving_ = false;

  bool open_add_menu_ = false;
  float add_x_ = 0.0f;
  float add_y_ = 0.0f;
  std::string add_filter_;
  std::optional<PendingPin> pending_pin_;

  Ask ask_ = Ask::kNone;
  bool open_ask_ = false;
  std::string ask_text_;
  std::function<void()> after_saving_;
  bool open_save_prompt_ = false;

  std::string status_;
  bool status_error_ = false;
  double status_time_ = 0.0;
  std::string lua_view_;
};

}  // namespace rexfabric::ui
