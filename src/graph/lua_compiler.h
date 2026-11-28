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
//
// An event whose delegate pin is linked (to Set Timer by Event, a dispatcher's Bind) has its chain written once, as a
// function named n and its id and _Run, which the event's own registration calls and which the timer or dispatcher is
// handed. Dispatchers become tables of such functions, named after the dispatcher.
//
// Functions (graph.h's Routine) become Lua functions, declared together before the events so they can call each other.
// Macros are written out in full wherever they're used: the chain runs into the macro's entry and out of its exit back
// into the graph using it, and the names of the macro's own locals start with m and the id of the node using it.
//
// An avatar class's own graph (Graph::self_class; docs/content_and_classes.md, Class graphs) runs for each avatar: every
// function the script writes takes the avatar as Self first, and its variables, dispatchers and the state nodes keep
// are the avatar's own, fields of the table rex.content.own keeps for it (My), made by Fresh the first time.
//
//   local Class = rex.content.script_asset()
//   local function Fresh()
//     return {
//       Count = 0,
//     }
//   end
//
//   rex.content.on_class(Class, "avatar_tick", function(Self, n2_Seconds)
//     local My = rex.content.own(Self, Class, Fresh)
//     My.Count = My.Count + 1
//   end)

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
  // The game asset the graph belongs to ("MyMod/Leaf"), for an asset's own graph: its asset events (AssetEventNode)
  // run only when the game fires them for this asset. Empty for a mod's graph.
  std::string asset;
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
// Typed text as a Lua integer literal: "0x10", "-3". False when it isn't a whole number.
bool IntegerText(std::string_view typed, std::string& literal);
// A name a variable can have: a Lua identifier that hides nothing the script needs.
bool IsVariableName(std::string_view name, const std::vector<std::string>& globals, std::string* reason = nullptr);

// What a node's emit and expression write through: the node being written, its inputs as Lua, and the script.
class Emitter {
 public:
  Emitter(const Graph& graph, const CompileOptions& options);

  const CompileOptions& options() const { return options_; }

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
  // The graph is an avatar class's own: Self is the avatar its chains run for.
  bool ClassGraph() const { return !graph_.self_class().empty(); }
  // A variable or dispatcher of the graph as Lua reaches it: a local of the script, or in a class graph the avatar's own.
  std::string Member(std::string_view name);
  // The current event's outputs, as the parameters its function has, in order.
  std::vector<std::string> EventParams();
  // Something in the chain asked for this event local.
  bool HasEventLocal(std::string_view name) const;
  // The event whose function is being written.
  const Node& event() const;
  // The exec input the chain reached this node through. Do Once has two.
  const std::string& entered() const;

  // Lua the script needs once, at its top, before anything using it: a helper function. Kept by `name`.
  void Helper(std::string_view name, std::string_view code);

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
    int instance = -1;  // the macro written out this node is in, -1 for none
  };

  // A macro written out where a node uses it, inside the instance the using node is in.
  struct Instance {
    const Node* call = nullptr;
    int parent = -1;
    int routine = 0;
    std::string prefix;  // of the names of its locals
  };

  // A function being written: where its locals go, which they are, and what has run in it so far.
  struct Function {
    size_t locals_at = 0;
    int indent = 0;
    int event = 0;
    std::vector<std::string> locals;
    std::set<std::string> ran;  // node keys
  };

  void EmitEvent(const Node& node);
  // An event's chain as a function of its own, for its delegate pin.
  void EmitDelegate(const Node& event);
  // The event has a delegate pin and something takes it, so its chain is a function of its own.
  bool Delegated(const Node& node) const;
  void EmitFunction(const Routine& routine);
  void EmitAction(const Node& node, const std::string& entered, int instance);
  // Into a macro where a node uses it, through the exec input `entered`; and out of one through its exit.
  void EnterMacro(const Node& call, const Routine& routine, const std::string& entered, int instance);
  void LeaveMacro(const std::string& entered, int instance);
  // The instance of a macro a call writes out inside `parent`, made the first time; -1 when it would use itself.
  int InstanceOf(const Node& call, int parent);
  std::string ValueOf(const PinRef& output);
  std::string Literal(const Pin& pin, const nlohmann::json& value);
  void Declare(const std::string& name);
  // A node, in the instance of the node writing it unless given.
  void Push(const Node& node, std::string entered = {});
  void Push(const Node& node, std::string entered, int instance);
  void Pop() { stack_.pop_back(); }
  const std::string& Prefix(int instance) const;
  // The name of a node's local in an instance, and its key in Function::ran and the chain's path.
  std::string LocalName(int instance, int node, std::string_view pin) const;
  std::string Key(int instance, int node) const;
  void CheckLinks();
  void CheckVariables();
  // A variable or param of an object type that went away (a class removed, a mod no longer there).
  void CheckObjectType(const std::string& what, const PinType& type);
  void CheckRoutines();
  void CheckDispatchers();
  // A class graph's events: the avatar's own, one of each, and custom events.
  void CheckClassEvents(const std::vector<const Node*>& events);
  // `params` with Self first, in a class graph, for the functions the script writes and the calls to them.
  std::string WithSelf(const std::string& params) const;
  // A class graph's own part of the script's top: its class, and what each avatar's own table starts as.
  std::string ClassHeader();

  const Graph& graph_;
  const CompileOptions& options_;
  // The names a variable can't have: the options' globals, and in a class graph its own (Self, My, Class, Fresh).
  std::vector<std::string> globals_;
  // A class graph: something reads the avatar's own table, so each function fetches it, and the nodes' kept state that
  // lives in it (State), by name with its starting value.
  bool uses_own_ = false;
  std::vector<std::pair<std::string, std::string>> own_state_;
  std::vector<std::string> lines_;
  std::vector<std::string> state_;
  std::set<std::string> state_names_;
  std::vector<Problem> problems_;
  std::vector<Current> stack_;
  std::vector<Function> functions_;
  std::vector<Instance> instances_;
  std::vector<std::string> path_;  // the actions on the chain being written, by key, to find exec loops
  std::set<int> reached_;          // every action any chain reached
  int indent_ = 0;
  int depth_ = 0;            // pure expressions inside expressions
  int delegate_ = 0;         // the event whose delegate function is being written
};

}  // namespace rexfabric::graph
