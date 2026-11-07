#include "node_library.h"

#include <algorithm>
#include <cctype>

namespace rexfabric::graph {
namespace {

// Number pins resolve through the nodes feeding them; a graph deeper than this is treated as floats.
constexpr int kMaxResolveDepth = 64;

PinType ResolveOutput(const Graph& graph, const PinRef& output, int depth) {
  const Node* node = graph.FindNode(output.node);
  if (!node) return {PinKind::kAny};
  const NodePins pins = PinsOf(*node, graph);
  const Pin* pin = FindPin(pins.outputs, output.pin);
  if (!pin) return {PinKind::kAny};
  if (pin->type.kind != PinKind::kNumber) return pin->type;
  if (depth > kMaxResolveDepth) return {PinKind::kFloat};

  // An int only when every number going in is one.
  for (const Pin& input : pins.inputs) {
    if (input.type.kind != PinKind::kNumber) continue;
    if (const Link* link = graph.LinkInto({node->id, input.name})) {
      if (ResolveOutput(graph, link->from, depth + 1).kind != PinKind::kInt) return {PinKind::kFloat};
      continue;
    }
    const auto typed = node->values.find(input.name);
    const nlohmann::json& value = typed != node->values.end() ? typed->second : input.initial;
    if (!value.is_string() || IsFloatText(value.get<std::string>())) return {PinKind::kFloat};
  }
  return {PinKind::kInt};
}

// Whether `node`'s value depends, through data links, on `on`.
bool DependsOn(const Graph& graph, int node, int on, int depth = 0) {
  if (node == on) return true;
  if (depth > kMaxResolveDepth) return true;
  for (const Link& link : graph.links()) {
    if (link.to.node == node && DependsOn(graph, link.from.node, on, depth + 1)) return true;
  }
  return false;
}

}  // namespace

const Library& Library::Get() {
  static const Library library = [] {
    Library made;
    AddBuiltinNodes(made);
    AddGameNodes(made);
    return made;
  }();
  return library;
}

void Library::Add(NodeDesc desc) {
  desc.title = TitleCase(desc.title);
  auto owned = std::make_unique<NodeDesc>(std::move(desc));
  by_id_[owned->id] = owned.get();
  nodes_.push_back(std::move(owned));
}

const NodeDesc* Library::Find(std::string_view id) const {
  const auto found = by_id_.find(std::string(id));
  return found != by_id_.end() ? found->second : nullptr;
}

Pin ExecPin(std::string_view name) { return {std::string(name), {PinKind::kExec}}; }

Pin DataPin(std::string_view name, PinKind kind, nlohmann::json initial) {
  Pin pin{std::string(name), {kind}, initial.is_null() ? DefaultValue(kind) : std::move(initial)};
  pin.required = kind == PinKind::kObject || kind == PinKind::kRegisters;
  return pin;
}

Pin ObjectPin(std::string_view name, std::string_view type) {
  Pin pin{std::string(name), {PinKind::kObject, std::string(type)}};
  pin.required = true;
  return pin;
}

nlohmann::json DefaultValue(PinKind kind) {
  switch (kind) {
    case PinKind::kBool: return false;
    case PinKind::kInt:
    case PinKind::kNumber: return "0";
    case PinKind::kFloat: return 0.0;
    case PinKind::kString:
    case PinKind::kAny: return "";
    default: return nullptr;
  }
}

const NodeDesc* FindDesc(const Node& node) { return Library::Get().Find(node.type); }

NodePins PinsOf(const Node& node, const Graph& graph) {
  const NodeDesc* desc = FindDesc(node);
  if (!desc) return {};
  NodePins pins = desc->pins;
  if (desc->shape) desc->shape(node, graph, pins);
  return pins;
}

const Pin* FindPin(const std::vector<Pin>& pins, std::string_view name) {
  const auto found = std::find_if(pins.begin(), pins.end(), [name](const Pin& pin) { return pin.name == name; });
  return found != pins.end() ? &*found : nullptr;
}

std::string SettingOf(const Node& node, std::string_view name) {
  if (const auto found = node.settings.find(name); found != node.settings.end()) return found->second;
  if (const NodeDesc* desc = FindDesc(node)) {
    for (const SettingDesc& setting : desc->settings) {
      if (setting.name == name) return setting.initial;
    }
  }
  return {};
}

std::string TitleOf(const Node& node) {
  const NodeDesc* desc = FindDesc(node);
  if (!desc) return node.type;
  return desc->title_of ? desc->title_of(node) : desc->title;
}

std::string TitleCase(std::string_view name) {
  const auto lower = [](char c) { return std::islower(static_cast<unsigned char>(c)) != 0; };
  const auto upper = [](char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; };
  const auto digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
  std::string title;
  bool word_start = true;
  for (size_t i = 0; i < name.size(); ++i) {
    char c = name[i];
    if (c == '_' || c == ' ') {
      // One space for a run of them, none at either end.
      if (!title.empty() && title.back() != ' ' && i + 1 < name.size()) title += ' ';
      word_start = true;
      continue;
    }
    // A new word where camel case starts one: "tM" in "SetMaterial", "PS" before "Server" in "HTTPServer".
    if (upper(c) && i > 0 && !word_start) {
      const char before = name[i - 1];
      const bool after_lower = lower(before) || digit(before);
      const bool acronym_end = upper(before) && i + 1 < name.size() && lower(name[i + 1]);
      if (after_lower || acronym_end) title += ' ';
    }
    if (word_start && lower(c)) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    title += c;
    word_start = c == '.' || c == ':' || c == '/';
  }
  while (!title.empty() && title.back() == ' ') title.pop_back();
  return title;
}

std::string TypeName(const PinType& type) {
  switch (type.kind) {
    case PinKind::kExec: return "Exec";
    case PinKind::kBool: return "Bool";
    case PinKind::kInt: return "Int";
    case PinKind::kFloat: return "Float";
    case PinKind::kNumber: return "Number";
    case PinKind::kString: return "String";
    case PinKind::kObject: return type.object.empty() ? "Object" : type.object;
    case PinKind::kRegisters: return "Registers";
    case PinKind::kAny: return "Any";
  }
  return "Any";
}

bool IsFloatText(std::string_view text) {
  const bool hex = text.find("0x") != std::string_view::npos || text.find("0X") != std::string_view::npos;
  if (hex) return text.find('.') != std::string_view::npos || text.find('p') != std::string_view::npos || text.find('P') != std::string_view::npos;
  return text.find_first_of(".eE") != std::string_view::npos || text.find("inf") != std::string_view::npos || text.find("nan") != std::string_view::npos;
}

PinType OutputType(const Graph& graph, const PinRef& output) { return ResolveOutput(graph, output, 0); }

bool Fits(const PinType& from, const PinType& to) {
  if (from.kind == PinKind::kExec || to.kind == PinKind::kExec) return from.kind == to.kind;
  if (from.kind == PinKind::kAny || to.kind == PinKind::kAny) return true;
  switch (to.kind) {
    case PinKind::kFloat:
    case PinKind::kNumber: return from.kind == PinKind::kInt || from.kind == PinKind::kFloat || from.kind == PinKind::kNumber;
    case PinKind::kObject: return from.kind == PinKind::kObject && (to.object.empty() || from.object.empty() || to.object == from.object);
    default: return from.kind == to.kind;
  }
}

bool CanConnect(const Graph& graph, const PinRef& from, const PinRef& to, std::string* reason) {
  const auto no = [reason](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  if (from.node == to.node) return no("A node can't link to itself");
  const Node* source = graph.FindNode(from.node);
  const Node* target = graph.FindNode(to.node);
  if (!source || !target) return no({});
  const NodePins source_pins = PinsOf(*source, graph);
  const NodePins target_pins = PinsOf(*target, graph);
  const Pin* output = FindPin(source_pins.outputs, from.pin);
  const Pin* input = FindPin(target_pins.inputs, to.pin);
  if (!output || !input) return no({});

  const bool exec = output->type.kind == PinKind::kExec;
  if (exec != (input->type.kind == PinKind::kExec)) return no("Exec pins only link to exec pins");
  const PinType carried = OutputType(graph, from);
  if (!Fits(carried, input->type)) return no(TypeName(carried) + " doesn't fit " + TypeName(input->type));
  if (!exec && DependsOn(graph, from.node, to.node)) return no("That would make a loop");
  return true;
}

bool Connect(Graph& graph, const PinRef& from, const PinRef& to) {
  if (!CanConnect(graph, from, to)) return false;
  const NodePins pins = PinsOf(*graph.FindNode(from.node), graph);
  graph.AddLink(from, to, FindPin(pins.outputs, from.pin)->type.kind == PinKind::kExec);
  return true;
}

void RemoveDanglingLinks(Graph& graph) {
  std::vector<int> dangling;
  for (const Link& link : graph.links()) {
    const Node* source = graph.FindNode(link.from.node);
    const Node* target = graph.FindNode(link.to.node);
    // Links of a node the library doesn't know are kept, in case its type comes back.
    if (!source || !target || !FindDesc(*source) || !FindDesc(*target)) continue;
    const NodePins source_pins = PinsOf(*source, graph);
    const NodePins target_pins = PinsOf(*target, graph);
    if (!FindPin(source_pins.outputs, link.from.pin) || !FindPin(target_pins.inputs, link.to.pin)) dangling.push_back(link.id);
  }
  for (int id : dangling) graph.RemoveLink(id);
}

}  // namespace rexfabric::graph
