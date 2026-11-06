// Every kind of node, and the rules for linking them.
//
// A node type is a NodeDesc: its pins, the choices made on it (settings), and how it becomes Lua. The built-in
// nodes (nodes_builtin.cpp) cover the rex API and plain logic; the game's nodes (nodes_game.cpp) are made from
// whatever the game declared with rexfabric::Namespace, so a game's functions, types and events show up without
// anyone writing nodes for them.
//
// To add a node, add a NodeDesc to one of those files. Its emit or expression writes the Lua (lua_compiler.h).

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "graph.h"

namespace rexfabric::graph {

class Emitter;

struct PinType {
  PinKind kind = PinKind::kExec;
  // kObject: the game type's full name, "tip.Pinata". Empty is any object.
  std::string object;
  bool operator==(const PinType&) const = default;
};

// A pin as one node has it.
struct Pin {
  std::string name;
  PinType type;
  // An unlinked data input's value until one is typed: a bool for Bool, a number for Float, text for the rest.
  nlohmann::json initial;
  // Has to be linked. Objects and registers can't be typed in.
  bool required = false;
};

struct NodePins {
  std::vector<Pin> inputs;
  std::vector<Pin> outputs;
};

enum class SettingKind : uint8_t {
  kAddress,   // a guest address, "0x8266FBB0"
  kText,      // free text: a cvar's or a switch's name
  kChoice,    // one of `choices`: a key, a register, a type
  kVariable,  // one of the graph's variables
};

struct SettingDesc {
  std::string name;
  SettingKind kind = SettingKind::kText;
  std::string initial;
  std::vector<std::string> choices;
  std::string tooltip;
};

enum class NodeRole : uint8_t {
  kEvent,   // starts a chain; no exec input
  kAction,  // runs when a chain reaches its exec input
  kPure,    // no exec pins; worked out wherever its outputs are read
};

struct NodeDesc {
  std::string id;  // stable: stored in graph files
  std::string title;
  // "Flow", or "Game/tip/Functions": slashes nest the add menu.
  std::string category;
  std::string description;
  NodeRole role = NodeRole::kAction;
  NodePins pins;
  std::vector<SettingDesc> settings;
  // Pins that depend on the node's settings or on the graph: a register's type, a variable's, a number of
  // arguments. Runs after `pins` are copied in.
  std::function<void(const Node& node, const Graph& graph, NodePins& pins)> shape;
  // The title with the node's settings in it, "Before 0x8266FBB0". Unset: `title`.
  std::function<std::string(const Node& node)> title_of;
  // Events and actions write Lua statements. Pure nodes give an expression for each output instead.
  std::function<void(Emitter& emit)> emit;
  std::function<std::string(Emitter& emit, std::string_view output)> expression;
};

// The exec pins most nodes have. Their labels aren't drawn.
inline constexpr std::string_view kExecIn = "Exec";
inline constexpr std::string_view kExecOut = "Then";

class Library {
 public:
  // The built-in nodes and the game's, made on first use, so after the game declared its API.
  static const Library& Get();

  void Add(NodeDesc desc);
  const NodeDesc* Find(std::string_view id) const;
  const std::vector<std::unique_ptr<NodeDesc>>& nodes() const { return nodes_; }

 private:
  std::vector<std::unique_ptr<NodeDesc>> nodes_;
  std::unordered_map<std::string, const NodeDesc*> by_id_;
};

void AddBuiltinNodes(Library& library);
void AddGameNodes(Library& library);

// Pins, for the node descriptions.
Pin ExecPin(std::string_view name = kExecIn);
Pin DataPin(std::string_view name, PinKind kind, nlohmann::json initial = nullptr);
Pin ObjectPin(std::string_view name, std::string_view type);
// What an unlinked input of this kind starts as.
nlohmann::json DefaultValue(PinKind kind);

const NodeDesc* FindDesc(const Node& node);
// The node's pins: its description's, shaped by its settings. None for a type the library doesn't know.
NodePins PinsOf(const Node& node, const Graph& graph);
const Pin* FindPin(const std::vector<Pin>& pins, std::string_view name);
// A setting's value, or its description's initial value when the node doesn't have it.
std::string SettingOf(const Node& node, std::string_view name);
std::string TitleOf(const Node& node);

// "Int", "Float", "tip.Pinata": a type as messages and tooltips name it.
std::string TypeName(const PinType& type);
// Typed number text with a fraction or an exponent, so a Number pin it's typed into is a float.
bool IsFloatText(std::string_view text);

// The type an output carries, with Number resolved to Int or Float from what goes into the node.
PinType OutputType(const Graph& graph, const PinRef& output);
// A value of `from` can go into an input of `to`. Ints go into floats; anything goes into and comes from Any.
bool Fits(const PinType& from, const PinType& to);

// From an output to an input of another node, with fitting types and no loop through data links. Sets `reason`
// when the answer is no.
bool CanConnect(const Graph& graph, const PinRef& from, const PinRef& to, std::string* reason = nullptr);
bool Connect(Graph& graph, const PinRef& from, const PinRef& to);
// Drops links to pins their nodes no longer have, after a setting took a pin away.
void RemoveDanglingLinks(Graph& graph);

}  // namespace rexfabric::graph
