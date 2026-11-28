// One graph::Document edited on a canvas, for a window to lay out as it likes: the sidebar's sections listing the
// graph's functions, macros, variables and event dispatchers, the canvas with the breadcrumb over it, the Lua the graph
// compiles to, and the details of what's selected with the graph's problems. The node editor (node_editor.h) shows one
// beside its mods and graphs; any other editor can show one for a graph of its own.
//
// The canvas shows one graph at a time: the event graph, or the graph of a function or macro, opened from the
// sidebar or from a node using it. On the canvas the wheel zooms, a right drag pans, a right click adds a node or,
// on a node, shows what can be done with it, and dragging a link into empty space adds a node connected to it.
// Double clicking a link adds a reroute there, a point to drag the link around things by; deleting a reroute joins
// its link up again. A variable dragged in from the sidebar asks whether to get or set it (Ctrl drops a Get, Alt a Set); a function or
// macro dragged in is called; an event dispatcher dragged in asks whether to call it, bind to it or unbind from it.
//
// Events that can be run by a timer or a dispatcher have a square delegate pin in their title's top right corner, as
// in Unreal: linked to Set Timer by Event or a dispatcher's Bind Event, the event's chain runs when they say. Delete removes the selection; Ctrl+C, Ctrl+X, Ctrl+V and Ctrl+D copy, cut, paste and
// duplicate. Unlinked inputs are typed into on the node itself. Undo and redo are the window's to offer, through Undo and Redo.

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "../graph/graph_document.h"
#include "node_graph_style.h"

namespace ax::NodeEditor {
struct EditorContext;
}

namespace rexfabric::ui {

class GraphView {
 public:
  // What the view needs of the window showing it.
  struct Host {
    // Shows a status line in the window.
    std::function<void(std::string text, bool error)> status;
    // After each edit to the graph, once the document has it (graph::Document::Edited, with the same `settled`).
    std::function<void(bool settled)> edited;
  };

  // The colors problems are shown in, for the window's status line to match.
  static constexpr ImVec4 kErrorColor{1.0f, 0.45f, 0.4f, 1.0f};
  static constexpr ImVec4 kWarningColor{1.0f, 0.75f, 0.35f, 1.0f};

  explicit GraphView(Host host);
  ~GraphView();
  GraphView(const GraphView&) = delete;
  GraphView& operator=(const GraphView&) = delete;

  // The document shown, or null for none; the view doesn't own it. `mod_folder` is the mod whose last run's errors
  // the problems show. The canvas starts over, on the event graph with nothing selected.
  void SetDocument(graph::Document* document, std::string mod_folder);
  // The window's fixed nodes: never deleted, copied, cut, duplicated or pasted, as a function's or macro's inputs and
  // outputs aren't.
  void SetFixed(std::function<bool(const graph::Node& node)> fixed);

  // The canvas's light behind the whole window, before anything else in it, once a frame (NodeGraphStyle).
  void DrawBackdrop(const ImVec2& min, const ImVec2& max, float rounding);
  // The sidebar's sections: the graph's functions, macros, variables and event dispatchers.
  void DrawSidebar();
  // Which graph the canvas shows, with a way back to the event graph.
  void DrawBreadcrumb();
  void DrawCanvas();
  // The Lua the graph compiles to; `script_label` names the script it's saved as.
  void DrawLua(std::string_view script_label);
  // The selected node, function, macro, variable or event dispatcher, and the graph's problems.
  void DrawDetails();

  // Shows the event graph (0) or a function's or macro's graph on the canvas.
  void ShowGraph(int owner);
  // The graph on the canvas: 0 for the event graph, or a function's or macro's id.
  int owner() const { return owner_; }

  // The document's undo and redo, then AfterHistory.
  void Undo();
  void Redo();
  // After the document's graph was replaced, by undo or redo or a reload: the canvas places its nodes again, and
  // what was shown or selected may be gone.
  void AfterHistory();
  // A node is being dragged: an edit not settled yet.
  bool moving() const { return moving_; }

  // A section of a sidebar: its title, with a round + at its right that adds to it. True when the + is pressed. The
  // window's own sections use it too, so they match the view's.
  static bool SectionHeader(const char* title, const char* add_tooltip, bool can_add);

 private:
  enum class Focus { kNone, kNode, kVariable, kRoutine, kDispatcher };
  using Settings = decltype(graph::Node::settings);

  // A pin a link was dragged from into empty space; the node added next connects to it.
  struct PendingPin {
    graph::PinRef ref;
    bool output = false;
  };

  // Left
  void DrawRoutines(graph::RoutineKind kind);
  void DrawVariables();
  void DrawDispatchers();

  // Middle
  // Fits the graph shown to the view, zoomed out when it's big but never in past its size.
  void FitView();
  void DrawNode(graph::Node& node);
  void DrawCompactNode(graph::Node& node, const graph::NodePins& pins);
  void DrawReroute(graph::Node& node);
  // A double clicked link gets a reroute where it was clicked.
  void HandleDoubleClick();
  void DrawInlineValue(graph::Node& node, const graph::Pin& pin);
  // The list an input with choices (Pin::choices) is picked from, with a search box.
  void DrawChoiceMenu();
  // An input's choices: a live list as it was when last opened (worked out the first time it's needed).
  const graph::ChoiceList& ChoicesOf(const graph::ChoiceSource& source, bool refresh = false);
  void HandleCreate();
  void HandleDelete();
  void HandleClipboard();
  void PasteNodes(const std::string& text, float x, float y);
  void DrawAddMenu();
  void AddNode(const std::string& type, const Settings& settings);
  // A drop from the sidebar onto the canvas, at `at` in canvas units.
  void AcceptDrop(const ImVec2& at);
  void DrawDropMenu();
  void DrawNodeMenu();
  // Makes a node another type, keeping its settings: a Bool's Get into its If.
  void ConvertNode(int node, const std::string& type);
  void SyncFromCanvas();
  // Why a node can't be deleted, copied or duplicated: a function's or macro's inputs and outputs, or one of the
  // window's fixed nodes. Null when it can.
  const char* KeptReason(const graph::Node& node) const;

  // Right
  void DrawNodeDetails();
  void DrawVariableDetails();
  void DrawRoutineDetails();
  void DrawParams(graph::Routine& routine, bool outputs);
  void DrawDispatcherDetails();
  // A kList setting's entries: a switch's cases, a select's options. They're pins of the node, so renaming one takes
  // its links along.
  void DrawListSetting(graph::Node& node, const graph::SettingDesc& setting);
  // A kScript setting: the file's name, and its code to read, edit and save apart from the graph.
  void DrawScriptSetting(graph::Node& node, const graph::SettingDesc& setting);
  // A list of params, each with its name, type and a remove button, and a button adding one. `refuse` says why a new
  // name can't be taken, or nothing; `renamed` makes the pins using a param follow its new name.
  void DrawParamList(const char* title, std::vector<graph::Param>& params, const std::vector<graph::PinKind>& kinds, const char* add_label, const std::string& base, const std::function<std::string(const std::string& name)>& refuse, const std::function<void(const std::string& from, const std::string& to)>& renamed);
  void DrawProblems();

  // After an edit to the graph. See graph::Document::Edited.
  void Edited(bool settled = true);
  void SetStatus(std::string text, bool error = false);
  void ResetCanvas();

  Host host_;
  std::function<bool(const graph::Node& node)> fixed_;

  graph::Document* document_ = nullptr;
  std::string mod_folder_;
  ax::NodeEditor::EditorContext* canvas_ = nullptr;
  NodeGraphStyle style_;

  // The graph on the canvas: 0 for the event graph, or a function's or macro's id.
  int owner_ = 0;

  Focus focus_ = Focus::kNone;
  int selected_node_ = 0;
  std::string selected_variable_;
  int selected_routine_ = 0;
  std::string selected_dispatcher_;
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

  // The variable or dispatcher dropped on the canvas, asking what to make of it, and the node right clicked.
  std::string dropped_member_;
  bool dropped_dispatcher_ = false;
  bool open_drop_menu_ = false;
  int context_node_ = 0;
  bool open_node_menu_ = false;

  // The input whose choices are being picked from.
  int choice_node_ = 0;
  std::string choice_pin_;
  std::string choice_filter_;
  bool open_choice_menu_ = false;
  std::map<const graph::ChoiceSource*, graph::ChoiceList> live_choices_;

  // A variable's starting value is being typed into; the edit settles once nothing is active.
  bool initial_editing_ = false;

  std::string lua_view_;

  // The script file a Script File node shows (DrawScriptSetting): its code as edited and as last read or saved.
  std::filesystem::path script_file_;
  std::string script_text_;
  std::string script_saved_;
  bool script_exists_ = false;
};

}  // namespace rexfabric::ui
