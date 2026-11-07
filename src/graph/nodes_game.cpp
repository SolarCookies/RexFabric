// The game's own nodes, made from what it declared with rexfabric::Namespace (game_api.md): a node to call each
// function, one for each constant and enum, Make, Get and Set nodes for each type and its fields, a node for each
// method, and an event node for each event. Their ids name what they stand for, so a graph keeps working as long as
// the game still declares it.

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rexfabric/game_api.h>

#include "lua_compiler.h"
#include "node_library.h"

namespace rexfabric::graph {
namespace {

// Game events carry whatever the game fires them with; their declarations don't say what, so each gets this many
// Any outputs.
constexpr int kEventArgs = 3;

bool IsIdentifier(std::string_view text) {
  if (text.empty() || std::isdigit(static_cast<unsigned char>(text[0]))) return false;
  for (char c : text) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return true;
}

// "tip.garden.money" as Lua reaches it: tip.garden.money, or tip["odd name"] for a part that isn't a name.
std::string LuaPath(std::string_view name) {
  std::string path;
  size_t start = 0;
  while (start <= name.size()) {
    const size_t dot = name.find('.', start);
    const std::string_view part = name.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
    if (path.empty()) path = std::string(part);
    else path += IsIdentifier(part) ? "." + std::string(part) : "[" + LuaQuote(part) + "]";
    if (dot == std::string_view::npos) break;
    start = dot + 1;
  }
  return path;
}

std::string Member(std::string_view name) { return IsIdentifier(name) ? "." + std::string(name) : "[" + LuaQuote(name) + "]"; }

// "tip.Pinata" without its namespace: "Pinata".
std::string ShortName(const std::string& name, const std::string& space) { return name.size() > space.size() + 1 && name.starts_with(space + ".") ? name.substr(space.size() + 1) : name; }

// Without the underscore a part of a name starts with: "materials._define" is "materials.define".
std::string WithoutUnderscores(std::string_view local) {
  std::string shown;
  for (size_t i = 0; i < local.size(); ++i) {
    if (local[i] == '_' && (i == 0 || local[i - 1] == '.') && i + 1 < local.size() && local[i + 1] != '.') continue;
    shown += local[i];
  }
  return shown;
}

// Where a declaration goes in the add menu, and its node's title there and in full (without the namespace).
struct Placed {
  std::string category;
  std::string title;
  std::string full;
};

// The namespace's longest Category prefix the name starts with gives the category, and the title leaves it out,
// as the category already says it ("materials._define" under Materials is "define"). None: `fallback`, the full name.
Placed Place(const NamespaceDef& space, const std::string& name, std::string fallback) {
  const std::string local = ShortName(name, space.name);
  const std::pair<std::string, std::string>* best = nullptr;
  for (const auto& category : space.categories) {
    if (local.starts_with(category.first) && (!best || category.first.size() > best->first.size())) best = &category;
  }
  Placed placed{best ? best->second : std::move(fallback), WithoutUnderscores(local), {}};
  placed.full = placed.title;
  if (best && local.size() > best->first.size()) placed.title = WithoutUnderscores(std::string_view(local).substr(best->first.size()));
  return placed;
}

std::string CategoryOf(const NamespaceDef& space, const std::string& name, std::string fallback) { return Place(space, name, std::move(fallback)).category; }

PinType ValuePinType(ValueType type) {
  switch (type) {
    case ValueType::kBool: return {PinKind::kBool};
    case ValueType::kInt: return {PinKind::kInt};
    case ValueType::kFloat: return {PinKind::kFloat};
    case ValueType::kString: return {PinKind::kString};
    case ValueType::kObject: return {PinKind::kObject};
    case ValueType::kNone: break;
  }
  return {PinKind::kAny};
}

PinType FieldPinType(const FieldType& type) {
  switch (type.kind) {
    case FieldKind::kBool8:
    case FieldKind::kBool32: return {PinKind::kBool};
    case FieldKind::kF32:
    case FieldKind::kF64: return {PinKind::kFloat};
    case FieldKind::kPointer:
    case FieldKind::kStruct: return {PinKind::kObject, type.target ? type.target->name() : std::string()};
    case FieldKind::kCString:
    case FieldKind::kChars: return {PinKind::kString};
    default: return {PinKind::kInt};
  }
}

Pin TypedPin(std::string name, const PinType& type) {
  if (type.kind == PinKind::kObject) return ObjectPin(name, type.object);
  return DataPin(name, type.kind);
}

// Arg 1 to Arg n, for a function's parameters from `first`.
std::vector<Pin> ArgPins(const FunctionDef& function, size_t first) {
  std::vector<Pin> pins;
  for (size_t i = first; i < function.params.size(); ++i) pins.push_back(TypedPin("Arg " + std::to_string(i - first + 1), ValuePinType(function.params[i])));
  return pins;
}

// `callee(args)`, kept in Result when the function gives something back and Result is used.
void EmitCall(Emitter& emit, const std::string& callee, const FunctionDef& function, size_t first) {
  std::string args;
  for (size_t i = first; i < function.params.size(); ++i) args += (i > first ? ", " : "") + emit.Input("Arg " + std::to_string(i - first + 1));
  const std::string call = callee + "(" + args + ")";
  emit.Line(function.result != ValueType::kNone && emit.Linked("Result") ? emit.Output("Result") + " = " + call : call);
}

void AddFunction(Library& library, const std::string& category, const std::string& title, const FunctionDef& function) {
  NodeDesc desc{.id = "game.call:" + function.name, .title = title, .category = category, .description = "Calls the game's " + function.name + "."};
  desc.pins.inputs = {ExecPin()};
  for (Pin& pin : ArgPins(function, 0)) desc.pins.inputs.push_back(std::move(pin));
  desc.pins.outputs = {ExecPin(kExecOut)};
  if (function.result != ValueType::kNone) desc.pins.outputs.push_back(TypedPin("Result", ValuePinType(function.result)));
  desc.emit = [&function](Emitter& emit) { EmitCall(emit, LuaPath(function.name), function, 0); };
  library.Add(std::move(desc));
}

void AddConstant(Library& library, const std::string& category, const std::string& title, const ConstantDef& constant) {
  NodeDesc desc{.id = "game.const:" + constant.name, .title = title, .category = category, .description = "The game's constant " + constant.name + ".", .role = NodeRole::kPure};
  desc.pins.outputs = {TypedPin("Value", ValuePinType(TypeOf(constant.value)))};
  desc.expression = [path = LuaPath(constant.name)](Emitter&, std::string_view) { return path; };
  library.Add(std::move(desc));
}

void AddEnum(Library& library, const std::string& category, const std::string& title, const EnumDef& enumeration) {
  NodeDesc desc{.id = "game.enum:" + enumeration.name, .title = title, .category = category, .description = "A value of the game's " + enumeration.name + ".", .role = NodeRole::kPure};
  desc.pins.outputs = {DataPin("Value", PinKind::kInt)};
  SettingDesc value{.name = "Value", .kind = SettingKind::kChoice};
  for (const auto& [name, number] : enumeration.values) value.choices.push_back(name);
  if (!value.choices.empty()) value.initial = value.choices.front();
  desc.settings = {std::move(value)};
  desc.title_of = [name = TitleCase(title)](const Node& node) { return name + "." + SettingOf(node, "Value"); };
  desc.expression = [&enumeration](Emitter& emit, std::string_view) {
    const std::string value = emit.Setting("Value");
    const bool known = std::any_of(enumeration.values.begin(), enumeration.values.end(), [&](const auto& entry) { return entry.first == value; });
    if (!known) emit.Error(enumeration.name + " has no value " + value);
    return LuaPath(enumeration.name) + Member(value);
  };
  library.Add(std::move(desc));
}

void AddType(Library& library, const std::string& category, const std::string& space, const TypeDef& type) {
  const std::string short_name = ShortName(type.name(), space);
  const std::string type_category = category + "/" + short_name;

  NodeDesc make{.id = "game.make:" + type.name(), .title = "Make " + short_name, .category = type_category, .description = "The " + short_name + " at Address, or nil for a null address.", .role = NodeRole::kPure};
  make.pins = {{DataPin("Address", PinKind::kInt, "")}, {ObjectPin(short_name, type.name())}};
  make.expression = [path = LuaPath(type.name())](Emitter& emit, std::string_view) { return path + "(" + emit.Input("Address") + ")"; };
  library.Add(std::move(make));

  for (const FieldDef& field : type.fields()) {
    const std::string label = short_name + "." + field.name;
    const PinType value_type = FieldPinType(field.type);
    // Arrays index from 1, as Lua tables do.
    std::vector<Pin> target = {ObjectPin("Target", type.name())};
    if (field.count) target.push_back(DataPin("Index", PinKind::kInt, "1"));
    const auto access = [name = field.name, array = field.count != 0](Emitter& emit) { return emit.Input("Target") + Member(name) + (array ? "[" + emit.Input("Index") + "]" : std::string()); };

    NodeDesc get{.id = "game.get:" + type.name() + "." + field.name, .title = "Get " + label, .category = type_category, .description = "Reads " + label + (field.count ? ", element Index of " + std::to_string(field.count) : std::string()) + ".", .role = NodeRole::kPure};
    get.pins = {target, {TypedPin("Value", value_type)}};
    get.expression = [access](Emitter& emit, std::string_view) { return access(emit); };
    library.Add(std::move(get));

    if (field.type.kind == FieldKind::kStruct || field.type.kind == FieldKind::kCString) continue;
    NodeDesc set{.id = "game.set:" + type.name() + "." + field.name, .title = "Set " + label, .category = type_category, .description = "Writes " + label + "."};
    set.pins.inputs = {ExecPin()};
    for (const Pin& pin : target) set.pins.inputs.push_back(pin);
    set.pins.inputs.push_back(TypedPin("Value", value_type));
    set.pins.outputs = {ExecPin(kExecOut)};
    set.emit = [access](Emitter& emit) { emit.Line(access(emit) + " = " + emit.Input("Value")); };
    library.Add(std::move(set));
  }

  for (const auto& [name, function] : type.methods()) {
    NodeDesc method{.id = "game.method:" + type.name() + ":" + name, .title = short_name + ":" + name, .category = type_category, .description = "Calls " + short_name + "'s " + name + " on Target."};
    method.pins.inputs = {ExecPin(), ObjectPin("Target", type.name())};
    for (Pin& pin : ArgPins(function, 1)) method.pins.inputs.push_back(std::move(pin));
    method.pins.outputs = {ExecPin(kExecOut)};
    if (function.result != ValueType::kNone) method.pins.outputs.push_back(TypedPin("Result", ValuePinType(function.result)));
    method.emit = [&function, name = name](Emitter& emit) { EmitCall(emit, emit.Input("Target") + ":" + name, function, 1); };
    library.Add(std::move(method));
  }
}

void AddEvent(Library& library, const std::string& category, const std::string& title, const std::string& event) {
  NodeDesc desc{.id = "game.event:" + event, .title = title, .category = category, .description = "Runs when the game fires " + event + ", with whatever it fires it with.", .role = NodeRole::kEvent};
  desc.pins.outputs = {ExecPin(kExecOut)};
  for (int i = 1; i <= kEventArgs; ++i) desc.pins.outputs.push_back(DataPin("Arg " + std::to_string(i), PinKind::kAny));
  desc.emit = [event](Emitter& emit) {
    std::string params;
    for (int i = 1; i <= kEventArgs; ++i) params += (i > 1 ? ", " : "") + emit.Param("Arg " + std::to_string(i));
    emit.Line("rex.on(" + LuaQuote(event) + ", function(" + params + ")");
    emit.Open();
    emit.Chain(kExecOut);
    emit.Close();
    emit.Line("end)");
  };
  library.Add(std::move(desc));
}

}  // namespace

void AddGameNodes(Library& library) {
  // Any game object's address, and whether there is one.
  NodeDesc address{.id = "game.address", .title = "Address Of", .category = "Game", .description = "Where an object is in guest memory.", .role = NodeRole::kPure};
  address.pins = {{ObjectPin("Object", {})}, {DataPin("Address", PinKind::kInt)}};
  address.expression = [](Emitter& emit, std::string_view) { return emit.Input("Object") + ".address"; };
  library.Add(std::move(address));

  // The declarations live as long as the process, so the nodes keep references to them.
  for (const std::unique_ptr<NamespaceDef>& space : Namespaces()) {
    // The namespace's own categories, top level in the add menu; Game/<namespace> for the rest.
    const std::string game = "Game/" + space->name;
    std::vector<Placed> functions, constants, enums, events;
    for (const FunctionDef& function : space->functions) functions.push_back(Place(*space, function.name, game + "/Functions"));
    for (const ConstantDef& constant : space->constants) constants.push_back(Place(*space, constant.name, game + "/Constants"));
    for (const EnumDef& enumeration : space->enums) enums.push_back(Place(*space, enumeration.name, game + "/Enums"));
    for (const std::string& event : space->events) {
      Placed placed = Place(*space, event, game + "/Events");
      placed.title = "On " + placed.title;
      placed.full = "On " + placed.full;
      events.push_back(std::move(placed));
    }
    // Titles two nodes of one category would share ("npcs._get", "player._get": Get) keep their full names there.
    std::map<std::pair<std::string, std::string>, int> titles;
    for (const std::vector<Placed>* list : {&functions, &constants, &enums, &events}) {
      for (const Placed& placed : *list) ++titles[{placed.category, TitleCase(placed.title)}];
    }
    const auto title = [&titles](const Placed& placed) { return titles[{placed.category, TitleCase(placed.title)}] > 1 ? placed.full : placed.title; };
    for (size_t i = 0; i < functions.size(); ++i) AddFunction(library, functions[i].category, title(functions[i]), space->functions[i]);
    for (size_t i = 0; i < constants.size(); ++i) AddConstant(library, constants[i].category, title(constants[i]), space->constants[i]);
    for (size_t i = 0; i < enums.size(); ++i) AddEnum(library, enums[i].category, title(enums[i]), space->enums[i]);
    for (const std::unique_ptr<TypeDef>& type : space->types) AddType(library, CategoryOf(*space, type->name(), game), space->name, *type);
    for (size_t i = 0; i < events.size(); ++i) AddEvent(library, events[i].category, title(events[i]), space->events[i]);
  }
}

}  // namespace rexfabric::graph
