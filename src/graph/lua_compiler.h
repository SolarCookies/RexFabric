// Turns a graph into a Lua script, the same as a mod would write by hand.
//
// Each event becomes the call that registers a function (rex.hook, rex.on, rex.midasm), and the chain of actions
// its exec output leads to becomes that function's body, in link order. Branches and loops nest their chains in
// if and for blocks. Pure nodes become expressions where their outputs are read, so a pure node read twice is
// worked out twice, as in a blueprint. An action's outputs become locals of its event's function, declared at the
// function's top. Variables and the state some nodes keep (Do Once) are locals at the top of the script.
//
//   -- Made with the RexFabric node editor from graphs/Main.json.
//   local Count = 0
//
//   rex.hook(0x8266FBB0, function(n1_Registers)
//     Count = (Count + 1)
//     if (Count > 100) then
//       rex.info("swaps", Count)
//     end
//   end)
//
// Names of a node's own locals start with n and its id. Events go out top to bottom as laid out on the canvas, so
// the On Start highest up runs first.

#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "graph.h"
#include "node_library.h"

namespace rexfabric::graph {

struct Problem {
  int node = 0;  // 0: the graph as a whole
  std::string message;
  bool warning = false;
};

struct CompileOptions {
  // Where the graph is, for the script's first line: "graphs/Main.json".
  std::string source;
  // Says whether a guest function can be hooked. Unset: hook addresses aren't checked.
  std::function<bool(uint32_t address)> hookable;
  // Globals the script reads besides Lua's own and rex: the game's namespaces. Variables can't hide them.
  std::vector<std::string> globals;
};

struct Compiled {
  std::string lua;
  std::vector<Problem> problems;
  // No errors; warnings are fine.
  bool ok() const;
};

Compiled Compile(const Graph& graph, const CompileOptions& options = {});

// Turns a name into a Lua string literal.
std::string LuaQuote(std::string_view text);
// A name a variable can have: a Lua identifier that hides nothing the script needs.
bool IsVariableName(std::string_view name, const std::vector<std::string>& globals, std::string* reason = nullptr);

// What a node's emit and expression write through: the node being written, its inputs as Lua, and the script.
class Emitter {
 public:
  Emitter(const Graph& graph, const CompileOptions& options);

  // The node being written.
  const Node& node() const;
  const Graph& graph() const { return graph_; }
  // A setting, or its initial value when the node doesn't have it.
  std::string Setting(std::string_view name) const;
  // A setting holding a guest address, as a hex literal. An error when it isn't one.
  std::string Address(std::string_view setting);
  // Checks that the address a hook event names can be hooked.
  void CheckHookable(std::string_view setting);

  // An input as a Lua expression: whatever its link brings, or the value typed into it.
  std::string Input(std::string_view pin);
  // An input, or an exec output, has a link.
  bool Linked(std::string_view pin) const;
  // An action's output: the local of the event's function that holds it.
  std::string Output(std::string_view pin);
  // An event's output: a parameter of its function.
  std::string Param(std::string_view pin);
  // A local of this node's kept between runs, declared at the top of the script with `initial`.
  std::string State(std::string_view name, std::string_view initial);
  // A local of the event's function, shared by its nodes: Skip Original's flag.
  std::string EventLocal(std::string_view name);
  // Something in the chain asked for this event local.
  bool HasEventLocal(std::string_view name) const;
  // The event whose function is being written.
  const Node& event() const;
  // The exec input the chain reached this node through. Do Once has two.
  const std::string& entered() const;

  void Line(std::string_view text);
  void Indent() { ++indent_; }
  void Dedent() { --indent_; }
  // A function body: one level in, with locals of its own, which Close declares at its top.
  void Open();
  void Close();
  // Writes the actions an exec output leads to, at the current level. An action whose emit doesn't follow its
  // Then has it followed after, unless it ends the chain.
  void Chain(std::string_view output);
  void EndChain() { stack_.back().chained.insert(std::string(kExecOut)); }

  void Error(std::string message);
  void Warn(std::string message);

  // Writes the events. Compile's work, once per emitter.
  Compiled Finish(std::string_view source);

 private:
  struct Current {
    const Node* node = nullptr;
    const NodeDesc* desc = nullptr;
    NodePins pins;
    std::string entered;
    std::set<std::string> chained;
  };

  // A function being written: where its locals go, which they are, and what has run in it so far.
  struct Function {
    size_t locals_at = 0;
    int indent = 0;
    int event = 0;
    std::vector<std::string> locals;
    std::set<int> ran;
  };

  void EmitEvent(const Node& node);
  void EmitAction(const Node& node, const std::string& entered);
  std::string ValueOf(const PinRef& output);
  std::string Literal(const Pin& pin, const nlohmann::json& value);
  void Declare(const std::string& name);
  void Push(const Node& node, std::string entered = {});
  void Pop() { stack_.pop_back(); }
  void CheckLinks();
  void CheckVariables();

  const Graph& graph_;
  const CompileOptions& options_;
  std::vector<std::string> lines_;
  std::vector<std::string> state_;
  std::set<std::string> state_names_;
  std::vector<Problem> problems_;
  std::vector<Current> stack_;
  std::vector<Function> functions_;
  std::vector<int> path_;    // the actions on the chain being written, to find exec loops
  std::set<int> reached_;    // every action any chain reached
  int indent_ = 0;
  int depth_ = 0;            // pure expressions inside expressions
};

}  // namespace rexfabric::graph
