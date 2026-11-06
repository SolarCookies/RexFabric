#include "lua_compiler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace rexfabric::graph {
namespace {

// Pure nodes nest as deep as their links go; past this the graph is taken to loop.
constexpr int kMaxExpressionDepth = 200;

constexpr std::array<std::string_view, 22> kKeywords = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
};

// Globals the generated script uses, or a mod reaches for often enough that hiding them would surprise.
constexpr std::array<std::string_view, 16> kReservedGlobals = {
    "rex", "math", "string", "table", "os", "io", "utf8", "coroutine", "print", "tostring", "tonumber", "type", "select", "pairs", "ipairs", "require",
};

std::string Indentation(int levels) { return std::string(static_cast<size_t>(std::max(levels, 0)) * 2, ' '); }

// A pin's name as part of a Lua name: "Arg 1" is Arg_1.
std::string NamePart(std::string_view text) {
  std::string part;
  for (char c : text) part += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
  return part;
}

std::string Trim(std::string_view text) {
  const size_t first = text.find_first_not_of(" \t");
  if (first == std::string_view::npos) return {};
  return std::string(text.substr(first, text.find_last_not_of(" \t") - first + 1));
}

// Whole number text, decimal or hex, as Lua reads it. Hex stays hex, so addresses read as addresses.
bool IntegerLiteral(std::string_view typed, std::string& literal) {
  const std::string text = Trim(typed);
  if (text.empty()) return false;
  const bool negative = text[0] == '-';
  const std::string_view digits = std::string_view(text).substr(negative ? 1 : 0);
  const bool hex = digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X');
  uint64_t value = 0;
  const std::string_view body = hex ? digits.substr(2) : digits;
  const auto [end, error] = std::from_chars(body.data(), body.data() + body.size(), value, hex ? 16 : 10);
  if (error != std::errc() || end != body.data() + body.size()) return false;
  char buffer[32];
  if (hex) std::snprintf(buffer, sizeof(buffer), "%s0x%llX", negative ? "-" : "", static_cast<unsigned long long>(value));
  else std::snprintf(buffer, sizeof(buffer), "%s%llu", negative ? "-" : "", static_cast<unsigned long long>(value));
  literal = buffer;
  return true;
}

std::string FloatLiteral(double value) {
  if (std::isnan(value)) return "(0/0)";
  if (std::isinf(value)) return value > 0 ? "math.huge" : "(-math.huge)";
  char buffer[64];
  const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
  std::string text(buffer, error == std::errc() ? end : buffer);
  // A float literal, so Lua keeps it a float.
  if (text.find_first_of(".e") == std::string::npos) text += ".0";
  return value < 0 ? "(" + text + ")" : text;
}

bool NumberLiteral(std::string_view typed, std::string& literal) {
  if (IntegerLiteral(typed, literal)) return true;
  const std::string text = Trim(typed);
  if (text.empty()) return false;
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end != text.c_str() + text.size()) return false;
  literal = FloatLiteral(value);
  return true;
}

}  // namespace

bool Compiled::ok() const {
  return std::none_of(problems.begin(), problems.end(), [](const Problem& problem) { return !problem.warning; });
}

std::string LuaQuote(std::string_view text) {
  std::string quoted = "\"";
  for (unsigned char c : text) {
    switch (c) {
      case '"': quoted += "\\\""; break;
      case '\\': quoted += "\\\\"; break;
      case '\n': quoted += "\\n"; break;
      case '\r': quoted += "\\r"; break;
      case '\t': quoted += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7F) {
          char escape[8];
          std::snprintf(escape, sizeof(escape), "\\%03u", static_cast<unsigned>(c));
          quoted += escape;
        } else {
          quoted += static_cast<char>(c);
        }
    }
  }
  return quoted + "\"";
}

bool IsVariableName(std::string_view name, const std::vector<std::string>& globals, std::string* reason) {
  const auto no = [reason](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  if (name.empty()) return no("A variable needs a name");
  if (std::isdigit(static_cast<unsigned char>(name[0]))) return no("A name can't start with a digit");
  for (char c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return no("Use letters, digits and _ only");
  }
  if (std::find(kKeywords.begin(), kKeywords.end(), name) != kKeywords.end()) return no(std::string(name) + " is a Lua keyword");
  const bool global = std::find(kReservedGlobals.begin(), kReservedGlobals.end(), name) != kReservedGlobals.end() || std::find(globals.begin(), globals.end(), name) != globals.end();
  if (global) return no(std::string(name) + " would hide the global of that name");
  // n<id>_ names belong to the nodes.
  if (name.size() > 2 && name[0] == 'n' && std::isdigit(static_cast<unsigned char>(name[1]))) return no("Names like n1_ are kept for the nodes");
  return true;
}

Compiled Compile(const Graph& graph, const CompileOptions& options) {
  Emitter emitter(graph, options);
  return emitter.Finish(options.source);
}

// Emitter -------------------------------------------------------------------------------------------------

Emitter::Emitter(const Graph& graph, const CompileOptions& options) : graph_(graph), options_(options) {}

const Node& Emitter::node() const { return *stack_.back().node; }

const Node& Emitter::event() const {
  const int id = functions_.empty() ? 0 : functions_.back().event;
  const Node* found = graph_.FindNode(id);
  return found ? *found : *stack_.front().node;
}

const std::string& Emitter::entered() const { return stack_.back().entered; }

std::string Emitter::Setting(std::string_view name) const { return SettingOf(node(), name); }

std::string Emitter::Address(std::string_view setting) {
  const std::string text = Trim(Setting(setting));
  std::string literal;
  const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
  if (text.empty()) {
    Error("Needs an address");
    return "0";
  }
  if (!IntegerLiteral(hex ? text : "0x" + text, literal) || literal[0] == '-') {
    Error(text + " isn't an address; write it in hex, like 0x82558D28");
    return "0";
  }
  return literal;
}

void Emitter::CheckHookable(std::string_view setting) {
  if (!options_.hookable) return;
  const std::string text = Trim(Setting(setting));
  std::string literal;
  const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
  if (text.empty() || !IntegerLiteral(hex ? text : "0x" + text, literal) || literal[0] == '-') return;
  const uint32_t address = static_cast<uint32_t>(std::strtoull(literal.c_str(), nullptr, 16));
  if (!options_.hookable(address)) Warn(literal + " isn't the start of a recompiled function, so the hook never runs");
}

std::string Emitter::Input(std::string_view pin) {
  const Current& current = stack_.back();
  const Pin* input = FindPin(current.pins.inputs, pin);
  if (!input) {
    Error("Has no input named " + std::string(pin));
    return "nil";
  }
  if (const Link* link = graph_.LinkInto({current.node->id, std::string(pin)})) return ValueOf(link->from);
  if (input->required) {
    Error(std::string(pin) + " needs a link");
    return "nil";
  }
  const auto typed = current.node->values.find(pin);
  return Literal(*input, typed != current.node->values.end() ? typed->second : input->initial);
}

bool Emitter::Linked(std::string_view pin) const {
  const PinRef ref{node().id, std::string(pin)};
  return graph_.HasLinkAt(ref, false) || graph_.HasLinkAt(ref, true);
}

std::string Emitter::Output(std::string_view pin) {
  const std::string name = "n" + std::to_string(node().id) + "_" + NamePart(pin);
  Declare(name);
  return name;
}

std::string Emitter::Param(std::string_view pin) { return "n" + std::to_string(node().id) + "_" + NamePart(pin); }

std::string Emitter::State(std::string_view name, std::string_view initial) {
  const std::string local = "n" + std::to_string(node().id) + "_" + NamePart(name);
  if (state_names_.insert(local).second) state_.push_back("local " + local + " = " + std::string(initial));
  return local;
}

std::string Emitter::EventLocal(std::string_view name) {
  const std::string local = "n" + std::to_string(event().id) + "_" + NamePart(name);
  Declare(local);
  return local;
}

bool Emitter::HasEventLocal(std::string_view name) const {
  if (functions_.empty()) return false;
  const std::string local = "n" + std::to_string(event().id) + "_" + NamePart(name);
  const std::vector<std::string>& locals = functions_.back().locals;
  return std::find(locals.begin(), locals.end(), local) != locals.end();
}

void Emitter::Line(std::string_view text) { lines_.push_back(Indentation(indent_) + std::string(text)); }

void Emitter::Open() {
  Indent();
  functions_.push_back({lines_.size(), indent_, node().id, {}, {}});
}

void Emitter::Close() {
  const Function function = std::move(functions_.back());
  functions_.pop_back();
  if (!function.locals.empty()) {
    std::string declaration = Indentation(function.indent) + "local ";
    for (size_t i = 0; i < function.locals.size(); ++i) declaration += (i ? ", " : "") + function.locals[i];
    lines_.insert(lines_.begin() + static_cast<ptrdiff_t>(function.locals_at), declaration);
  }
  Dedent();
}

void Emitter::Chain(std::string_view output) {
  const int from = node().id;
  stack_.back().chained.insert(std::string(output));
  for (const Link* link : graph_.LinksOutOf({from, std::string(output)})) {
    if (const Node* target = graph_.FindNode(link->to.node)) EmitAction(*target, link->to.pin);
  }
}

void Emitter::Error(std::string message) { problems_.push_back({stack_.empty() ? 0 : node().id, std::move(message), false}); }

void Emitter::Warn(std::string message) { problems_.push_back({stack_.empty() ? 0 : node().id, std::move(message), true}); }

void Emitter::Declare(const std::string& name) {
  if (functions_.empty()) {
    if (state_names_.insert(name).second) state_.push_back("local " + name);
    return;
  }
  std::vector<std::string>& locals = functions_.back().locals;
  if (std::find(locals.begin(), locals.end(), name) == locals.end()) locals.push_back(name);
}

void Emitter::Push(const Node& node, std::string entered) {
  Current current;
  current.node = &node;
  current.desc = FindDesc(node);
  current.pins = PinsOf(node, graph_);
  current.entered = std::move(entered);
  stack_.push_back(std::move(current));
}

std::string Emitter::ValueOf(const PinRef& output) {
  const Node* source = graph_.FindNode(output.node);
  const NodeDesc* desc = source ? FindDesc(*source) : nullptr;
  if (!desc) {
    Error("Reads from a node the editor doesn't know");
    return "nil";
  }

  switch (desc->role) {
    case NodeRole::kPure: {
      if (!desc->expression || depth_ >= kMaxExpressionDepth) {
        Error("Can't work out " + TitleOf(*source));
        return "nil";
      }
      ++depth_;
      Push(*source);
      std::string expression = desc->expression(*this, output.pin);
      Pop();
      --depth_;
      return expression;
    }
    case NodeRole::kEvent:
      if (functions_.empty() || functions_.back().event != source->id) {
        Error("Reads " + output.pin + " of " + TitleOf(*source) + ", which only exists in that event's chain");
        return "nil";
      }
      return "n" + std::to_string(source->id) + "_" + NamePart(output.pin);
    case NodeRole::kAction:
      if (functions_.empty() || !functions_.back().ran.contains(source->id)) {
        Error("Reads " + output.pin + " of " + TitleOf(*source) + " before it runs; it has to come earlier in the same event's chain");
        return "nil";
      }
      return "n" + std::to_string(source->id) + "_" + NamePart(output.pin);
  }
  return "nil";
}

std::string Emitter::Literal(const Pin& pin, const nlohmann::json& value) {
  const std::string text = value.is_string() ? value.get<std::string>() : std::string();
  std::string literal;
  switch (pin.type.kind) {
    case PinKind::kBool: return value.is_boolean() && value.get<bool>() ? "true" : "false";
    case PinKind::kInt:
      if (IntegerLiteral(text, literal)) return literal;
      Error(pin.name + ": " + (text.empty() ? "needs a whole number" : "\"" + text + "\" isn't a whole number"));
      return "0";
    case PinKind::kNumber:
      if (NumberLiteral(text, literal)) return literal;
      Error(pin.name + ": " + (text.empty() ? "needs a number" : "\"" + text + "\" isn't a number"));
      return "0";
    case PinKind::kFloat: return FloatLiteral(value.is_number() ? value.get<double>() : 0.0);
    case PinKind::kString: return LuaQuote(text);
    case PinKind::kAny:
      // Typed into an Any pin: nothing is nil, true and false and numbers are themselves, the rest is text.
      if (Trim(text).empty() || text == "nil") return "nil";
      if (text == "true" || text == "false") return text;
      if (NumberLiteral(text, literal)) return literal;
      return LuaQuote(text);
    default: return "nil";
  }
}

void Emitter::EmitEvent(const Node& node) {
  Push(node);
  if (stack_.back().desc->emit) stack_.back().desc->emit(*this);
  Pop();
}

void Emitter::EmitAction(const Node& node, const std::string& entered) {
  const NodeDesc* desc = FindDesc(node);
  if (!desc || desc->role != NodeRole::kAction || functions_.empty()) return;
  if (std::find(path_.begin(), path_.end(), node.id) != path_.end()) {
    Error("This chain runs back into " + TitleOf(node) + "; use a For Loop to repeat things");
    return;
  }

  reached_.insert(node.id);
  functions_.back().ran.insert(node.id);
  path_.push_back(node.id);
  Push(node, entered);
  if (desc->emit) desc->emit(*this);
  const bool then_left = FindPin(stack_.back().pins.outputs, kExecOut) && !stack_.back().chained.contains(std::string(kExecOut));
  if (then_left) Chain(kExecOut);
  Pop();
  path_.pop_back();
}

void Emitter::CheckLinks() {
  for (const Link& link : graph_.links()) {
    const Node* source = graph_.FindNode(link.from.node);
    const Node* target = graph_.FindNode(link.to.node);
    if (!source || !target || !FindDesc(*source) || !FindDesc(*target)) continue;
    const NodePins source_pins = PinsOf(*source, graph_);
    const NodePins target_pins = PinsOf(*target, graph_);
    const Pin* input = FindPin(target_pins.inputs, link.to.pin);
    if (!input || !FindPin(source_pins.outputs, link.from.pin)) continue;
    const PinType carried = OutputType(graph_, link.from);
    if (!Fits(carried, input->type)) problems_.push_back({target->id, input->name + " gets " + TypeName(carried) + " from " + TitleOf(*source) + ", which doesn't fit " + TypeName(input->type), false});
  }
}

void Emitter::CheckVariables() {
  std::set<std::string> seen;
  for (const Variable& variable : graph_.variables()) {
    std::string reason;
    if (!IsVariableName(variable.name, options_.globals, &reason)) problems_.push_back({0, "Variable " + variable.name + ": " + reason, false});
    if (!seen.insert(variable.name).second) problems_.push_back({0, "Two variables are named " + variable.name, false});
  }
}

Compiled Emitter::Finish(std::string_view source) {
  CheckVariables();
  CheckLinks();

  std::vector<const Node*> events;
  for (const Node& node : graph_.nodes()) {
    const NodeDesc* desc = FindDesc(node);
    if (!desc) problems_.push_back({node.id, "There's no " + node.type + " node; the game may not declare it any more", false});
    else if (desc->role == NodeRole::kEvent) events.push_back(&node);
  }
  std::sort(events.begin(), events.end(), [](const Node* a, const Node* b) { return a->y != b->y ? a->y < b->y : a->x != b->x ? a->x < b->x : a->id < b->id; });
  for (const Node* event : events) {
    lines_.push_back({});
    lines_.push_back("-- " + TitleOf(*event));
    EmitEvent(*event);
  }

  for (const Node& node : graph_.nodes()) {
    const NodeDesc* desc = FindDesc(node);
    if (desc && desc->role == NodeRole::kAction && !reached_.contains(node.id)) problems_.push_back({node.id, "Never runs: no event's chain leads here", true});
  }

  Compiled compiled;
  std::string& lua = compiled.lua;
  lua = "-- Made with the RexFabric node editor";
  if (!source.empty()) lua += " from " + std::string(source);
  lua += ".\n-- Saving the graph again replaces this file, so change the graph rather than this file.\n";

  if (!graph_.variables().empty() || !state_.empty()) lua += "\n";
  for (const Variable& variable : graph_.variables()) {
    const Pin pin = DataPin(variable.name, variable.type);
    lua += "local " + variable.name;
    if (!variable.initial.is_null()) lua += " = " + Literal(pin, variable.initial);
    if (!variable.comment.empty()) {
      std::string comment = variable.comment;
      std::replace(comment.begin(), comment.end(), '\n', ' ');
      lua += "  -- " + comment;
    }
    lua += "\n";
  }
  for (const std::string& line : state_) lua += line + "\n";
  for (const std::string& line : lines_) lua += line + "\n";

  // A pure node read twice reports its problem twice.
  for (const Problem& problem : problems_) {
    const bool seen = std::any_of(compiled.problems.begin(), compiled.problems.end(), [&](const Problem& other) { return other.node == problem.node && other.message == problem.message; });
    if (!seen) compiled.problems.push_back(problem);
  }
  return compiled;
}

}  // namespace rexfabric::graph
