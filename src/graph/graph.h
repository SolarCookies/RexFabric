// A node graph: what the node editor edits and the Lua compiler reads.
//
// Pure data. Which pins a node has, and which links make sense, is the node library's business
// (node_library.h); this only keeps the nodes, the links between them and the graph's variables.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace rexfabric::graph {

// What flows along a link. Exec links carry the order things run in; the rest carry values.
enum class PinKind : uint8_t {
  kExec,
  kBool,
  kInt,
  kFloat,
  kNumber,     // an int or a float, whichever the node's number inputs are: Add of two ints is an int
  kString,
  kObject,     // an object of one of the game's types
  kRegisters,  // a hook's or a midasm site's registers
  kAny,        // whatever Lua value comes in
};

const char* PinKindName(PinKind kind);
bool ParsePinKind(std::string_view name, PinKind& kind);

struct Node {
  int id = 0;
  // A node type from the library: "flow.branch", "game.call:tip.version".
  std::string type;
  float x = 0.0f;
  float y = 0.0f;
  // Choices made on the node rather than through a pin: an address, a key, a register.
  std::map<std::string, std::string, std::less<>> settings;
  // Values typed into unlinked inputs, by pin name. A missing one is the pin's default.
  std::map<std::string, nlohmann::json, std::less<>> values;

  std::string Setting(std::string_view name) const;
};

struct PinRef {
  int node = 0;
  std::string pin;
  bool operator==(const PinRef&) const = default;
};

// From an output pin to an input pin. Pins are named, so links survive a node type gaining pins.
struct Link {
  int id = 0;
  PinRef from;
  PinRef to;
};

// A value the whole graph shares, kept between events. It compiles to a local at the top of the script.
struct Variable {
  std::string name;
  PinKind type = PinKind::kFloat;
  nlohmann::json initial;
  std::string comment;
};

class Graph {
 public:
  Node& AddNode(std::string type, float x, float y);
  // Keeps the node's own id, as loaded from a file.
  Node& InsertNode(Node node);
  bool RemoveNode(int id);

  // Exec outputs and data inputs hold one link, so a link already at that end is replaced. Exec inputs and data
  // outputs take any number.
  void AddLink(const PinRef& from, const PinRef& to, bool exec);
  // Adds a link as stored in a file, replacing nothing.
  void InsertLink(const PinRef& from, const PinRef& to);
  void RemoveLink(int id);
  // Drops every link at this pin.
  void RemoveLinksAt(const PinRef& pin, bool output);

  std::vector<Node>& nodes() { return nodes_; }
  const std::vector<Node>& nodes() const { return nodes_; }
  const std::vector<Link>& links() const { return links_; }
  std::vector<Variable>& variables() { return variables_; }
  const std::vector<Variable>& variables() const { return variables_; }

  Node* FindNode(int id);
  const Node* FindNode(int id) const;
  Variable* FindVariable(std::string_view name);
  const Variable* FindVariable(std::string_view name) const;

  bool HasLinkAt(const PinRef& pin, bool output) const;
  // The one link into a data input, or null.
  const Link* LinkInto(const PinRef& input) const;
  // Every link out of an output.
  std::vector<const Link*> LinksOutOf(const PinRef& output) const;

  // "Speed", "Speed1", "Speed2": a variable name nothing uses yet.
  std::string UniqueVariableName(std::string_view base) const;

 private:
  std::vector<Node> nodes_;
  std::vector<Link> links_;
  std::vector<Variable> variables_;
  int next_node_ = 1;
  int next_link_ = 1;
};

}  // namespace rexfabric::graph
