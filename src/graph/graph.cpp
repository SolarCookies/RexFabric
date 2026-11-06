#include "graph.h"

#include <algorithm>
#include <array>
#include <utility>

namespace rexfabric::graph {
namespace {

constexpr std::array<std::pair<PinKind, const char*>, 9> kKindNames = {{
    {PinKind::kExec, "exec"},
    {PinKind::kBool, "bool"},
    {PinKind::kInt, "int"},
    {PinKind::kFloat, "float"},
    {PinKind::kNumber, "number"},
    {PinKind::kString, "string"},
    {PinKind::kObject, "object"},
    {PinKind::kRegisters, "registers"},
    {PinKind::kAny, "any"},
}};

}  // namespace

const char* PinKindName(PinKind kind) {
  for (const auto& [value, name] : kKindNames) {
    if (value == kind) return name;
  }
  return "any";
}

bool ParsePinKind(std::string_view name, PinKind& kind) {
  for (const auto& [value, text] : kKindNames) {
    if (name == text) {
      kind = value;
      return true;
    }
  }
  return false;
}

std::string Node::Setting(std::string_view name) const {
  const auto found = settings.find(name);
  return found != settings.end() ? found->second : std::string();
}

Node& Graph::AddNode(std::string type, float x, float y) {
  Node node;
  node.id = next_node_;
  node.type = std::move(type);
  node.x = x;
  node.y = y;
  return InsertNode(std::move(node));
}

Node& Graph::InsertNode(Node node) {
  next_node_ = std::max(next_node_, node.id + 1);
  nodes_.push_back(std::move(node));
  return nodes_.back();
}

bool Graph::RemoveNode(int id) {
  const auto found = std::find_if(nodes_.begin(), nodes_.end(), [id](const Node& node) { return node.id == id; });
  if (found == nodes_.end()) return false;
  nodes_.erase(found);
  std::erase_if(links_, [id](const Link& link) { return link.from.node == id || link.to.node == id; });
  return true;
}

void Graph::AddLink(const PinRef& from, const PinRef& to, bool exec) {
  if (exec) RemoveLinksAt(from, true);
  else RemoveLinksAt(to, false);
  InsertLink(from, to);
}

void Graph::InsertLink(const PinRef& from, const PinRef& to) { links_.push_back({next_link_++, from, to}); }

void Graph::RemoveLink(int id) {
  std::erase_if(links_, [id](const Link& link) { return link.id == id; });
}

void Graph::RemoveLinksAt(const PinRef& pin, bool output) {
  std::erase_if(links_, [&](const Link& link) { return (output ? link.from : link.to) == pin; });
}

Node* Graph::FindNode(int id) {
  const auto found = std::find_if(nodes_.begin(), nodes_.end(), [id](const Node& node) { return node.id == id; });
  return found != nodes_.end() ? &*found : nullptr;
}

const Node* Graph::FindNode(int id) const { return const_cast<Graph*>(this)->FindNode(id); }

Variable* Graph::FindVariable(std::string_view name) {
  const auto found = std::find_if(variables_.begin(), variables_.end(), [name](const Variable& variable) { return variable.name == name; });
  return found != variables_.end() ? &*found : nullptr;
}

const Variable* Graph::FindVariable(std::string_view name) const { return const_cast<Graph*>(this)->FindVariable(name); }

bool Graph::HasLinkAt(const PinRef& pin, bool output) const {
  return std::any_of(links_.begin(), links_.end(), [&](const Link& link) { return (output ? link.from : link.to) == pin; });
}

const Link* Graph::LinkInto(const PinRef& input) const {
  const auto found = std::find_if(links_.begin(), links_.end(), [&](const Link& link) { return link.to == input; });
  return found != links_.end() ? &*found : nullptr;
}

std::vector<const Link*> Graph::LinksOutOf(const PinRef& output) const {
  std::vector<const Link*> found;
  for (const Link& link : links_) {
    if (link.from == output) found.push_back(&link);
  }
  return found;
}

std::string Graph::UniqueVariableName(std::string_view base) const {
  std::string name(base);
  for (int suffix = 1; FindVariable(name); ++suffix) name = std::string(base) + std::to_string(suffix);
  return name;
}

}  // namespace rexfabric::graph
