// One graph open in an editor: where it lives, its undo history, and the Lua it compiles to.
//
//   mods/<Mod>/graphs/<Name>.json         a mod's graph, open in the node editor
//   mods/<Mod>/scripts/<Name>.graph.lua   its script, run like any other of the mod's scripts
//
//   mods/<Mod>/content/.../<Asset>.graph.json   a game asset's or an avatar class's own graph, open in its editor
//   mods/<Mod>/content/.../<Asset>.graph.lua    its script, run after the mod's scripts (mods.h, LuaScripts)
//
// The script is plain Lua, so a mod made in the editor ships and runs as an ordinary Lua mod; the graph is only
// needed to keep editing it.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "graph.h"
#include "lua_compiler.h"

namespace rexfabric::graph {

// The game's namespaces, which the scripts reach as globals and variables can't hide.
std::vector<std::string> GameGlobals();

// The graphs a mod has, by name.
std::vector<std::string> ListGraphs(const std::filesystem::path& mod_dir);
std::filesystem::path GraphFile(const std::filesystem::path& mod_dir, std::string_view name);
std::filesystem::path ScriptFile(const std::filesystem::path& mod_dir, std::string_view name);
// A name a graph, or a mod made in the editor, can have: letters, digits, spaces, _ and -.
bool IsFileName(std::string_view name);
// Removes the graph and its script.
bool DeleteGraph(const std::filesystem::path& mod_dir, std::string_view name, std::string* error);
// Renames the graph and its script.
bool RenameGraph(const std::filesystem::path& mod_dir, std::string_view from, std::string_view to, std::string* error);

// Where a document's graph and script are, and what its script says it was made from.
struct Location {
  std::filesystem::path graph;
  std::filesystem::path script;
  std::string source;  // "graphs/Main.json", "content/Leaves/Maze.graph.json"
  std::string asset;   // a game asset's own graph: the asset ("MyMod/Leaves/Maze"), CompileOptions::asset
  std::string avatar_class;  // an avatar class's own graph: the class ("MyMod/Glow"), its graph's self_class
};

// A game asset's own graph, beside the asset's file (Maze.json): Maze.graph.json and Maze.graph.lua.
Location AssetGraphLocation(const std::filesystem::path& mod_dir, const std::filesystem::path& asset_file, std::string asset);
// An avatar class's own graph, beside the class's file (Glow.json) as an asset's is: Glow.graph.json and Glow.graph.lua.
Location ClassGraphLocation(const std::filesystem::path& mod_dir, const std::filesystem::path& class_file, std::string avatar_class);

class Document {
 public:
  // A mod's graph, by its name in the mod's graphs folder.
  Document(std::filesystem::path mod_dir, std::string name);
  // A graph anywhere: a game asset's own.
  Document(std::filesystem::path mod_dir, std::string name, Location location);

  // Reads the graph file. A graph without one starts empty.
  bool Load(std::string* error);
  // Writes the graph and its script. A graph with errors gets a script that says so in the log, rather than
  // running half a mod.
  bool Save(std::string* error);

  Graph& graph() { return graph_; }
  const Graph& graph() const { return graph_; }
  const std::string& name() const { return name_; }
  const std::filesystem::path& mod_dir() const { return mod_dir_; }
  const Compiled& compiled() const { return compiled_; }
  // Differs from the file.
  bool dirty() const { return history_[at_] != saved_; }

  // After an edit: drops links to pins that went away and recompiles. A settled edit is an undo step; text
  // still being typed isn't, so it's one step once typing stops.
  void Edited(bool settled = true);
  // The graph as its file keeps it.
  std::string Text() const;
  // The graph as `text` has it, after a host's own undo took it there: its history starts over from it.
  bool Reset(const std::string& text, std::string* error = nullptr);
  bool CanUndo() const { return at_ > 0; }
  bool CanRedo() const { return at_ + 1 < history_.size(); }
  bool Undo();
  bool Redo();

 private:
  void Recompile();
  void Restore(const std::string& snapshot);
  // The graph was read anew: it's told again whose own graph it is.
  void Adopt() { graph_.SetSelfClass(location_.avatar_class); }

  std::filesystem::path mod_dir_;
  std::string name_;
  Location location_;
  Graph graph_;
  Compiled compiled_;
  // The graph as written after each settled edit, oldest first; at_ is where the graph is now.
  std::vector<std::string> history_;
  size_t at_ = 0;
  std::string saved_;
};

}  // namespace rexfabric::graph
