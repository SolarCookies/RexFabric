// An asset's own node graph in its editor, laid out as a blueprint (docs/content_and_classes.md, Asset graphs and Class
// graphs): the tabs across the window (Viewport, Event Graph, Lua), which stay put whichever is open, and under them the
// panes: the graph's functions, macros, variables and dispatchers on the left, only beside the graph; the open tab's
// page in the middle; on the right the details, the editor's own on the viewport and the selected node's on the graph.
// Each side pane is as wide as its edge is dragged. The canvas is the node editor's own (GraphView).
//
// The graph is kept beside the asset's file as its companions (Maze.graph.json, compiled to Maze.graph.lua), and its
// events are fixed nodes placed down the left of a new graph, never deleted; a game asset's field events (made from its
// fields) are fixed too, but come and go with the fields they're made from. The editor showing it keeps the graph's
// text in its own document, so the fields and the graph are one history: a settled edit of the canvas is handed to it,
// and an undo of its that changes the graph is handed back with Follow.

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <rexfabric/content.h>

#include "../graph/graph_document.h"
#include "graph_view.h"

namespace rexfabric::ui {

class OwnGraph {
 public:
  // What it needs of the editor: its status line, and each settled edit of the canvas with the graph's text after it.
  struct Host {
    std::function<void(std::string text, bool error)> status;
    std::function<void(std::string text)> edited;
  };

  explicit OwnGraph(Host host);
  OwnGraph(const OwnGraph&) = delete;
  OwnGraph& operator=(const OwnGraph&) = delete;

  // Reads the graph at `location` (a new one starts empty) and adds a node of each of `events` (node types) it lacks,
  // then makes its field events `field_events` (graph::PlaceFieldEvents), as if it had always had them; null leaves
  // the ones it has. None of these can be deleted, copied or duplicated. Gives the graph's text, for the editor's
  // document. `mod_folder` is the mod whose last run's errors the details show.
  std::string Open(const std::filesystem::path& mod_dir, const std::string& mod_folder, std::string name, graph::Location location, const std::vector<std::string>& events, const std::vector<content::AssetEventDecl>* field_events = nullptr);

  // The editor's undo or redo took its document's graph to `text`: the canvas shows it. Nothing for an empty text (a
  // document from before the graph was opened).
  void Follow(const std::string& text);
  // The asset's fields changed what field events its graph has: the canvas makes them so, as an edit of its own would.
  // The graph's text after it, for the editor's document; none when nothing changed.
  std::optional<std::string> FollowFieldEvents(const std::vector<content::AssetEventDecl>& field_events);

  // The canvas's light behind the window, before anything else in it.
  void DrawBackdrop();
  // The tabs and the panes, in what's left of the window: `viewport` draws the Viewport tab's page and `details` the
  // right pane beside it; `script_label` names the script the Lua tab shows.
  void Draw(const std::function<void()>& viewport, const std::function<void()>& details, std::string_view script_label);

  // A node is being dragged: an edit not settled yet, so the editor waits before keeping its history.
  bool moving() const { return view_.moving(); }
  bool on_viewport() const { return tab_ == Tab::kViewport; }
  // Writes the graph and its script; false with `error` when it couldn't.
  bool Save(std::string* error) { return document_->Save(error); }

 private:
  enum class Tab { kViewport, kGraph, kLua };

  void DrawTabs();
  // The edge beside a side pane, dragged to make the pane `width` wider or narrower: `toward` is 1 for a pane on the
  // left (dragging right widens it) and -1 for one on the right.
  void Splitter(const char* id, float& width, float toward);

  Host host_;
  std::unique_ptr<graph::Document> document_;
  // The graph as the editor's document last had it, to see when an undo of its changed it.
  std::string followed_;
  Tab tab_ = Tab::kViewport;
  // The side panes' widths, in pixels, as dragged; 0 until the first frame sets them from the font.
  float sidebar_width_ = 0.0f;
  float details_width_ = 0.0f;
  GraphView view_;
};

}  // namespace rexfabric::ui
