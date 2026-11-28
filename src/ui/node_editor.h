// The node editor: makes Lua mods out of node graphs, in game. Built when the project attaches RexFabric
// WITH_NODE_EDITOR.
//
// It picks a mod and one of its graphs, edits the graph on a canvas, and saves it with the Lua it compiles to
// (graph_document.h), which Save and Reload then runs. Laid out like a blueprint editor: the mod's graphs, the
// graph's functions, macros and variables on the left, the canvas and the Lua it makes in the middle, the selected
// node, function, macro or variable and the graph's problems on the right. With RexGlass the whole window is the
// canvas's drifting light, with the panels around the canvas glass over it, as the nodes are.
//
// The window keeps the mods and their graphs; the graph open is edited by a GraphView (graph_view.h), which says how
// the canvas is used. Ctrl+S saves, Ctrl+Z and Ctrl+Y undo and redo.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <rex/ui/ui_system.h>

#include "../graph/graph_document.h"
#include "graph_view.h"
#include "menu_model.h"

namespace rexfabric::ui {

class NodeEditorWindow : public rex::ui::DialogBase {
 public:
  // `closed` runs as the window goes.
  NodeEditorWindow(MenuModel& model, std::function<void()> closed);
  ~NodeEditorWindow() override;

  // Closes the window, once unsaved changes are saved or let go.
  void Hide();

  bool Draw(ImGuiIO& io) override;

 private:
  void OnDraw(ImGuiIO& io);

  // The names the window asks for in a popup.
  enum class Ask { kNone, kNewMod, kNewGraph, kRenameGraph, kDeleteGraph };

  // Top
  void DrawToolbar();
  void DrawModPicker();
  void Shortcuts();

  // Left: the mod's graphs, above the view's sections.
  void DrawGraphs();

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
  // Shows `document`, or none, in the view.
  void SetDocument(std::unique_ptr<graph::Document> document);

  // Runs `action` now, or once the open graph's unsaved changes are saved or let go.
  void AfterSaving(std::function<void()> action);
  void DrawPopups();
  void AskFor(Ask ask, std::string initial);

  void SetStatus(std::string text, bool error = false);

  MenuModel& model_;
  std::function<void()> closed_;
  // The UI drops the window on the next Draw.
  bool closing_ = false;

  std::string mod_folder_;
  std::vector<std::string> graphs_;
  std::unique_ptr<graph::Document> document_;
  // Edits document_, which it's told of whenever it changes.
  GraphView view_;

  Ask ask_ = Ask::kNone;
  bool open_ask_ = false;
  std::string ask_text_;
  std::function<void()> after_saving_;
  bool open_save_prompt_ = false;

  std::string status_;
  bool status_error_ = false;
  double status_time_ = 0.0;
};

}  // namespace rexfabric::ui
