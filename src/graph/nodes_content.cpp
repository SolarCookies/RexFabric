// Nodes for content's enums, structs and data tables (docs/content_and_classes.md, Structs and enums, Data tables),
// typed ones for maps, avatars of mods' classes (Avatars), and an avatar class's own graph (Class graphs): its avatars'
// events, Self, Call Parent and the avatar's property values.
//
// A struct's Make and Break, an enum's value and text, and a table's rows pick the struct, enum or table with a
// setting, so the add menu offers one of each for every one the graph's mod may use, and they follow it as it changes.
// A table's row names are picked on the node from the rows it has. Map nodes take their types from the map linked into
// them.

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../content/assets.h"
#include "../content/classes.h"
#include "../content/tables.h"
#include "../content/types.h"
#include "lua_compiler.h"
#include "node_library.h"

namespace rexfabric::graph {
namespace {

constexpr std::string_view kStructSetting = "Struct";
constexpr std::string_view kEnumSetting = "Enum";
constexpr std::string_view kTableSetting = "Table";
constexpr std::string_view kRowNamePin = "Row Name";
constexpr std::string_view kClassSetting = "Class";
constexpr std::string_view kAvatarPin = "Avatar";
constexpr std::string_view kPropertySetting = "Property";
constexpr std::string_view kSelf = "Self";

// A key in a table constructor: name for a name Lua takes as one, ["odd name"] for the rest.
std::string TableKey(const std::string& name) {
  const std::string member = LuaMember(name);
  return member.starts_with(".") ? name : member;
}

// An input for a struct's field or a class's property, its default typed in: under the field's name unless given one.
Pin FieldPin(const content::Property& field, std::string_view name = {}) {
  const PinType type = PinTypeOf(field.type);
  Pin pin = ValuePin(name.empty() ? std::string_view(field.name) : name, type);
  if (type.container == PinContainer::kSingle && type.kind != PinKind::kStruct && !field.value.is_null()) pin.initial = TypedValue(type.kind, field.value);
  return pin;
}

// The type of a map linked into `input`, when a map is.
std::optional<PinType> LinkedMap(const Node& node, const Graph& graph, std::string_view input) {
  const std::optional<PinType> linked = LinkedType(node, graph, input);
  return linked && linked->container == PinContainer::kMap ? linked : std::nullopt;
}

PinType AnyArray() { return {PinKind::kAny, {}, PinContainer::kArray}; }

std::string BriefName(const Node& node, std::string_view setting) { return std::string(content::NameOf(SettingOf(node, setting))); }

void AddStructs(Library& library) {
  const std::string category = "Structs";
  const SettingDesc struct_setting{.name = std::string(kStructSetting), .kind = SettingKind::kStruct, .tooltip = "The struct"};

  library.Add({.id = "struct.make", .title = "Make", .category = category,
      .description = "A value of the struct, from each of its fields. A field left unlinked is the value typed into it.",
      .keywords = "struct new build construct",
      .role = NodeRole::kPure, .settings = {struct_setting},
      .shape = [](const Node& node, const Graph&, NodePins& pins) {
        const std::string path = SettingOf(node, kStructSetting);
        const std::optional<content::Struct> found = content::FindStruct(path);
        if (!found) return;
        for (const content::Property& field : found->fields) pins.inputs.push_back(FieldPin(field));
        pins.outputs.push_back(ValuePin("Value", {PinKind::kStruct, path}));
      },
      .title_of = [](const Node& node) { return "Make " + BriefName(node, kStructSetting); },
      .expression = [](Emitter& emit, std::string_view) -> std::string {
        const std::string path = emit.Setting(kStructSetting);
        const std::optional<content::Struct> found = content::FindStruct(path);
        if (!found) {
          emit.Error("there's no struct " + path);
          return "nil";
        }
        std::string out = "{";
        for (size_t i = 0; i < found->fields.size(); ++i) out += (i ? ", " : "") + TableKey(found->fields[i].name) + " = " + emit.Input(found->fields[i].name);
        return out + "}";
      }});

  library.Add({.id = "struct.break", .title = "Break", .category = category,
      .description = "Each field of a value of the struct.",
      .keywords = "struct fields split members",
      .role = NodeRole::kPure, .settings = {struct_setting},
      .shape = [](const Node& node, const Graph&, NodePins& pins) {
        const std::string path = SettingOf(node, kStructSetting);
        const std::optional<content::Struct> found = content::FindStruct(path);
        if (!found) return;
        pins.inputs.push_back(ValuePin("Value", {PinKind::kStruct, path}));
        for (const content::Property& field : found->fields) pins.outputs.push_back(ValuePin(field.name, PinTypeOf(field.type)));
      },
      .title_of = [](const Node& node) { return "Break " + BriefName(node, kStructSetting); },
      .expression = [](Emitter& emit, std::string_view output) { return "(" + emit.Input("Value") + ")" + LuaMember(output); }});
}

void AddEnums(Library& library) {
  const std::string category = "Enums";
  const SettingDesc enum_setting{.name = std::string(kEnumSetting), .kind = SettingKind::kEnum, .tooltip = "The enum"};

  library.Add({.id = "enum.value", .title = "Value", .category = category,
      .description = "One of the enum's values, picked on the node.",
      .keywords = "enum literal constant",
      .role = NodeRole::kPure, .settings = {enum_setting},
      .shape = [](const Node& node, const Graph&, NodePins& pins) {
        const PinType type{PinKind::kEnum, SettingOf(node, kEnumSetting)};
        pins.inputs.push_back(ValuePin("Value", type));
        pins.outputs.push_back(ValuePin("Value", type));
      },
      .title_of = [](const Node& node) { return BriefName(node, kEnumSetting); },
      .expression = [](Emitter& emit, std::string_view) { return emit.Input("Value"); }});

  library.Add({.id = "enum.text", .title = "Text", .category = category,
      .description = "What the player sees for a value of the enum: its display name, or its name when it has none. Nil for a value it doesn't have.",
      .keywords = "enum name display string label",
      .role = NodeRole::kPure, .settings = {enum_setting},
      .shape = [](const Node& node, const Graph&, NodePins& pins) {
        pins.inputs.push_back(ValuePin("Value", {PinKind::kEnum, SettingOf(node, kEnumSetting)}));
        pins.outputs.push_back(DataPin("Text", PinKind::kString));
      },
      .title_of = [](const Node& node) { return BriefName(node, kEnumSetting) + " As Text"; },
      .expression = [](Emitter& emit, std::string_view) -> std::string {
        const std::string path = emit.Setting(kEnumSetting);
        const std::optional<content::Enum> found = content::FindEnum(path);
        if (!found) {
          emit.Error("there's no enum " + path);
          return "nil";
        }
        // A table from each value, as Lua has it, to its text.
        std::string table = "({";
        for (size_t i = 0; i < found->values.size(); ++i) {
          const content::EnumValue& value = found->values[i];
          const std::string key = i < found->numbers.size() ? std::to_string(found->numbers[i]) : LuaQuote(value.name);
          table += (i ? ", " : "") + std::string("[") + key + "] = " + LuaQuote(value.display.empty() ? value.name : value.display);
        }
        return table + "})[" + emit.Input("Value") + "]";
      }});
}

// The list a table's Row Name is picked from: its rows' names as they are now. Lives as long as the program.
const ChoiceSource* RowChoices(std::string_view table) {
  static std::mutex mutex;
  static std::map<std::string, std::unique_ptr<ChoiceSource>, std::less<>> sources;
  std::lock_guard lock(mutex);
  if (const auto found = sources.find(table); found != sources.end()) return found->second.get();
  auto source = std::make_unique<ChoiceSource>();
  source->live = [path = std::string(table)] {
    ChoiceList list;
    for (const std::string& name : content::RowNames(path)) list.emplace_back(name, name);
    return list;
  };
  return sources.emplace(std::string(table), std::move(source)).first->second.get();
}

Pin RowNamePin(const std::string& table) {
  Pin pin = DataPin(kRowNamePin, PinKind::kString);
  pin.choices = RowChoices(table);
  return pin;
}

PinType RowOf(const std::string& table) { return {PinKind::kStruct, content::RowStruct(table)}; }

void AddTables(Library& library) {
  const std::string category = "Data Tables";
  const SettingDesc table_setting{.name = std::string(kTableSetting), .kind = SettingKind::kTable, .tooltip = "The data table"};

  library.Add({.id = "datatable.get_row", .title = "Get Row", .category = category,
      .description = "The row of the table under Row Name, as its struct, through Found; or Not Found when it has no such row.",
      .keywords = "data table row lookup find",
      .pins = {{ExecPin()}, {}}, .settings = {table_setting},
      .shape = [](const Node& node, const Graph&, NodePins& pins) {
        const std::string table = SettingOf(node, kTableSetting);
        pins.inputs.push_back(RowNamePin(table));
        pins.outputs = {ExecPin("Found"), ExecPin("Not Found"), ValuePin("Row", RowOf(table))};
      },
      .title_of = [](const Node& node) { return "Get " + BriefName(node, kTableSetting) + " Row"; },
      .emit = [](Emitter& emit) {
        const std::string row = emit.Output("Row");
        emit.Line(row + " = rex.content.row(" + LuaQuote(emit.Setting(kTableSetting)) + ", " + emit.Input(kRowNamePin) + ")");
        emit.Line("if " + row + " ~= nil then");
        emit.Indent();
        emit.Chain("Found");
        emit.Dedent();
        if (emit.Linked("Not Found")) {
          emit.Line("else");
          emit.Indent();
          emit.Chain("Not Found");
          emit.Dedent();
        }
        emit.Line("end");
      }});

  library.Add({.id = "datatable.has_row", .title = "Has Row", .category = category,
      .description = "Whether the table has a row under Row Name.",
      .keywords = "data table row contains exists",
      .role = NodeRole::kPure, .settings = {table_setting},
      .shape = [](const Node& node, const Graph&, NodePins& pins) {
        pins.inputs.push_back(RowNamePin(SettingOf(node, kTableSetting)));
        pins.outputs.push_back(DataPin("Result", PinKind::kBool));
      },
      .title_of = [](const Node& node) { return BriefName(node, kTableSetting) + " Has Row"; },
      .expression = [](Emitter& emit, std::string_view) { return "rex.content.has_row(" + LuaQuote(emit.Setting(kTableSetting)) + ", " + emit.Input(kRowNamePin) + ")"; }});

  library.Add({.id = "datatable.row_names", .title = "Row Names", .category = category,
      .description = "Every row's name, in the table's order.",
      .keywords = "data table rows list keys",
      .role = NodeRole::kPure, .settings = {table_setting},
      .shape = [](const Node&, const Graph&, NodePins& pins) { pins.outputs.push_back(ValuePin("Names", {PinKind::kString, {}, PinContainer::kArray})); },
      .title_of = [](const Node& node) { return BriefName(node, kTableSetting) + " Row Names"; },
      .expression = [](Emitter& emit, std::string_view) { return "rex.content.row_names(" + LuaQuote(emit.Setting(kTableSetting)) + ")"; }});

  library.Add({.id = "datatable.for_each_row", .title = "For Each Row", .category = category,
      .description = "Runs Body once for each of the table's rows in order, then goes on through Completed. Name and Row only exist in Body's chain.",
      .keywords = "data table loop rows foreach",
      .pins = {{ExecPin()}, {}}, .settings = {table_setting},
      .shape = [](const Node& node, const Graph&, NodePins& pins) {
        pins.outputs = {ExecPin("Body"), DataPin("Name", PinKind::kString), ValuePin("Row", RowOf(SettingOf(node, kTableSetting))), ExecPin("Completed")};
      },
      .title_of = [](const Node& node) { return "For Each " + BriefName(node, kTableSetting) + " Row"; },
      .emit = [](Emitter& emit) {
        const std::string table = LuaQuote(emit.Setting(kTableSetting));
        emit.Line("for _, " + emit.Param("Name") + " in ipairs(rex.content.row_names(" + table + ")) do");
        emit.Indent();
        emit.Line("local " + emit.Param("Row") + " = rex.content.row(" + table + ", " + emit.Param("Name") + ")");
        emit.Chain("Body");
        emit.Dedent();
        emit.Line("end");
        emit.Chain("Completed");
      }});
}

void AddMaps(Library& library) {
  const std::string category = "Tables";
  const PinType any_map{PinKind::kAny, {}, PinContainer::kMap, PinKind::kAny};

  library.Add({.id = "map.find", .title = "Find", .category = category,
      .description = "The map's value under Key, and whether it has one. Value is nil when it doesn't.",
      .keywords = "map lookup get key contains has",
      .role = NodeRole::kPure,
      .pins = {{ValuePin("Map", any_map), DataPin("Key", PinKind::kAny)}, {DataPin("Value", PinKind::kAny), DataPin("Found", PinKind::kBool)}},
      .shape = [](const Node& node, const Graph& graph, NodePins& pins) {
        if (const std::optional<PinType> map = LinkedMap(node, graph, "Map")) {
          Retype(pins.inputs, "Key", KeyOf(*map));
          Retype(pins.outputs, "Value", ElementOf(*map));
        }
      },
      .expression = [](Emitter& emit, std::string_view output) {
        const std::string found = "(" + emit.Input("Map") + ")[" + emit.Input("Key") + "]";
        return output == "Found" ? "(" + found + " ~= nil)" : found;
      }});

  library.Add({.id = "map.add", .title = "Add", .category = category,
      .description = "Puts Value in the map under Key, in place of what was there.",
      .keywords = "map set put insert",
      .pins = {{ExecPin(), ValuePin("Map", any_map), DataPin("Key", PinKind::kAny), DataPin("Value", PinKind::kAny)}, {ExecPin(kExecOut)}},
      .shape = [](const Node& node, const Graph& graph, NodePins& pins) {
        if (const std::optional<PinType> map = LinkedMap(node, graph, "Map")) {
          Retype(pins.inputs, "Key", KeyOf(*map));
          Retype(pins.inputs, "Value", ElementOf(*map));
        }
      },
      .emit = [](Emitter& emit) {
        emit.Line("(" + emit.Input("Map") + ")[" + emit.Input("Key") + "] = " + emit.Input("Value"));
        emit.Chain(kExecOut);
      }});

  library.Add({.id = "map.remove", .title = "Remove", .category = category,
      .description = "Takes the map's entry under Key out.",
      .keywords = "map delete erase",
      .pins = {{ExecPin(), ValuePin("Map", any_map), DataPin("Key", PinKind::kAny)}, {ExecPin(kExecOut)}},
      .shape = [](const Node& node, const Graph& graph, NodePins& pins) {
        if (const std::optional<PinType> map = LinkedMap(node, graph, "Map")) Retype(pins.inputs, "Key", KeyOf(*map));
      },
      .emit = [](Emitter& emit) {
        emit.Line("(" + emit.Input("Map") + ")[" + emit.Input("Key") + "] = nil");
        emit.Chain(kExecOut);
      }});

  library.Add({.id = "map.keys", .title = "Keys", .category = category,
      .description = "Every key of the map, as an array, in no set order.",
      .keywords = "map list",
      .role = NodeRole::kPure,
      .pins = {{ValuePin("Map", any_map)}, {ValuePin("Keys", AnyArray())}},
      .shape = [](const Node& node, const Graph& graph, NodePins& pins) {
        if (const std::optional<PinType> map = LinkedMap(node, graph, "Map")) Retype(pins.outputs, "Keys", {map->key, map->key_object, PinContainer::kArray});
      },
      .expression = [](Emitter& emit, std::string_view) { return "(function(map) local keys = {} for key in pairs(map) do keys[#keys + 1] = key end return keys end)(" + emit.Input("Map") + ")"; }});

  library.Add({.id = "map.values", .title = "Values", .category = category,
      .description = "Every value of the map, as an array, in no set order.",
      .keywords = "map list",
      .role = NodeRole::kPure,
      .pins = {{ValuePin("Map", any_map)}, {ValuePin("Values", AnyArray())}},
      .shape = [](const Node& node, const Graph& graph, NodePins& pins) {
        if (const std::optional<PinType> map = LinkedMap(node, graph, "Map")) Retype(pins.outputs, "Values", {map->kind, map->object, PinContainer::kArray});
      },
      .expression = [](Emitter& emit, std::string_view) { return "(function(map) local values = {} for _, value in pairs(map) do values[#values + 1] = value end return values end)(" + emit.Input("Map") + ")"; }});

  library.Add({.id = "map.count", .title = "Count", .category = category,
      .description = "How many entries the map has.",
      .keywords = "map length size number",
      .role = NodeRole::kPure,
      .pins = {{ValuePin("Map", any_map)}, {DataPin("Count", PinKind::kInt)}},
      .expression = [](Emitter& emit, std::string_view) { return "(function(map) local count = 0 for _ in pairs(map) do count = count + 1 end return count end)(" + emit.Input("Map") + ")"; }});

  library.Add({.id = "map.for_each", .title = "For Each Entry", .category = category,
      .description = "Runs Body once for each of the map's entries, in no set order, then goes on through Completed. Key and Value only exist in Body's chain.",
      .keywords = "map loop pairs foreach",
      .pins = {{ExecPin(), ValuePin("Map", any_map)}, {ExecPin("Body"), DataPin("Key", PinKind::kAny), DataPin("Value", PinKind::kAny), ExecPin("Completed")}},
      .shape = [](const Node& node, const Graph& graph, NodePins& pins) {
        if (const std::optional<PinType> map = LinkedMap(node, graph, "Map")) {
          Retype(pins.outputs, "Key", KeyOf(*map));
          Retype(pins.outputs, "Value", ElementOf(*map));
        }
      },
      .emit = [](Emitter& emit) {
        emit.Line("for " + emit.Param("Key") + ", " + emit.Param("Value") + " in pairs(" + emit.Input("Map") + ") do");
        emit.Indent();
        emit.Chain("Body");
        emit.Dedent();
        emit.Line("end");
        emit.Chain("Completed");
      }});

  library.Add({.id = "map.clear", .title = "Clear", .category = category,
      .description = "Takes every entry out of a map, or every item out of an array.",
      .keywords = "map array empty reset",
      .pins = {{ExecPin(), DataPin("Table", PinKind::kAny)}, {ExecPin(kExecOut)}},
      .emit = [](Emitter& emit) {
        emit.Line("do local table = " + emit.Input("Table") + " for key in pairs(table) do table[key] = nil end end");
        emit.Chain(kExecOut);
      }});
}

}  // namespace

// The inputs of a place: X, Y and Z, and the yaw in degrees.
std::vector<Pin> PlacePins() { return {DataPin("X", PinKind::kFloat, 0.0), DataPin("Y", PinKind::kFloat, 0.0), DataPin("Z", PinKind::kFloat, 0.0), DataPin("Yaw", PinKind::kFloat, 0.0)}; }

std::string PlaceArgs(Emitter& emit) { return emit.Input("X") + ", " + emit.Input("Y") + ", " + emit.Input("Z") + ", " + emit.Input("Yaw"); }

namespace {

// An avatar node's Avatar input: in a class graph it may be left unlinked, for Self, as a blueprint's Target is self.
Pin AvatarPin(const Graph& graph) {
  Pin pin = ObjectPin(kAvatarPin, "");
  pin.required = graph.self_class().empty();
  return pin;
}

// What the Avatar input brings, or Self when a class graph leaves it unlinked.
std::string AvatarInput(Emitter& emit) { return emit.Linked(kAvatarPin) || !emit.ClassGraph() ? emit.Input(kAvatarPin) : std::string(kSelf); }

// The property a Get Value or Set Value picks, of the class of the avatar it acts on; none when that class has no such.
std::optional<content::Property> PickedProperty(const Node& node, const Graph& graph) {
  const std::string name = SettingOf(node, kPropertySetting);
  for (const content::Property& property : content::Properties(AvatarClassOf(node, graph))) {
    if (property.name == name) return property;
  }
  return std::nullopt;
}

// The name of the property a node picks, or an error when the avatar's class has no such.
std::string PropertyName(Emitter& emit) {
  const std::string name = emit.Setting(kPropertySetting);
  if (PickedProperty(emit.node(), emit.graph())) return name;
  const std::string of = AvatarClassOf(emit.node(), emit.graph());
  emit.Error(name.empty() ? "Pick a property" : (of.empty() ? std::string("The avatar's class") : of) + " has no property " + name);
  return name;
}

bool InClassGraph(const Graph& graph) { return !graph.self_class().empty(); }

}  // namespace

void AddAvatars(Library& library) {
  const std::string category = "Avatars";
  const SettingDesc class_setting{.name = std::string(kClassSetting), .kind = SettingKind::kAvatarClass, .tooltip = "The class it spawns an avatar of"};

  library.Add({.id = "avatar.spawn", .title = "Spawn Avatar", .category = category,
      .description = "Spawns an avatar of the class at X, Y and Z, turned Yaw degrees: its backing made there and its components attached. Saved keeps it in the world's save, so it's there again when the world loads. An error when the class can't be spawned now (no garden, say).",
      .keywords = "avatar spawn create place new actor save persist",
      .pins = {{ExecPin()}, {ExecPin(kExecOut)}}, .settings = {class_setting},
      .shape = [](const Node& node, const Graph&, NodePins& pins) {
        for (Pin& pin : PlacePins()) pins.inputs.push_back(std::move(pin));
        pins.inputs.push_back(DataPin("Saved", PinKind::kBool, false));
        pins.outputs.push_back(ObjectPin(kAvatarPin, SettingOf(node, kClassSetting)));
      },
      .title_of = [](const Node& node) { return "Spawn " + BriefName(node, kClassSetting); },
      .emit = [](Emitter& emit) { emit.Line(emit.Output(kAvatarPin) + " = rex.content.spawn(" + LuaQuote(emit.Setting(kClassSetting)) + ", " + PlaceArgs(emit) + ", {saved = " + emit.Input("Saved") + "})"); }});

  library.Add({.id = "avatar.remove", .title = "Remove Avatar", .category = category,
      .description = "Removes an avatar of a mod's class: its components detached and its backing removed. Nothing for one gone already. In a class graph, Avatar left unlinked is Self.",
      .keywords = "avatar remove destroy delete despawn",
      .pins = {{ExecPin()}, {ExecPin(kExecOut)}},
      .shape = [](const Node&, const Graph& graph, NodePins& pins) { pins.inputs.push_back(AvatarPin(graph)); },
      .emit = [](Emitter& emit) { emit.Line("rex.content.remove(" + AvatarInput(emit) + ")"); }});

  library.Add({.id = "avatar.move", .title = "Move Avatar", .category = category,
      .description = "Moves an avatar of a mod's class to X, Y and Z, turned Yaw degrees; its components go with it. In a class graph, Avatar left unlinked is Self.",
      .keywords = "avatar move teleport set location place",
      .pins = {{ExecPin()}, {ExecPin(kExecOut)}},
      .shape = [](const Node&, const Graph& graph, NodePins& pins) {
        pins.inputs.push_back(AvatarPin(graph));
        for (Pin& pin : PlacePins()) pins.inputs.push_back(std::move(pin));
      },
      .emit = [](Emitter& emit) { emit.Line("(" + AvatarInput(emit) + "):move(" + PlaceArgs(emit) + ")"); }});

  library.Add({.id = "avatar.location", .title = "Avatar Location", .category = category,
      .description = "Where an avatar of a mod's class is: X, Y, Z and its yaw in degrees. In a class graph, Avatar left unlinked is Self.",
      .keywords = "avatar position location where get",
      .role = NodeRole::kPure,
      .pins = {{}, {DataPin("X", PinKind::kFloat), DataPin("Y", PinKind::kFloat), DataPin("Z", PinKind::kFloat), DataPin("Yaw", PinKind::kFloat)}},
      .shape = [](const Node&, const Graph& graph, NodePins& pins) { pins.inputs.push_back(AvatarPin(graph)); },
      .expression = [](Emitter& emit, std::string_view output) { return "(" + AvatarInput(emit) + ")" + LuaMember(output == "Yaw" ? "yaw" : output == "X" ? "x" : output == "Y" ? "y" : "z"); }});

  library.Add({.id = "avatar.is_valid", .title = "Is Avatar Valid", .category = category,
      .description = "Whether an avatar of a mod's class is still there: not removed, and its world not gone. In a class graph, Avatar left unlinked is Self.",
      .keywords = "avatar valid alive exists",
      .role = NodeRole::kPure,
      .pins = {{}, {DataPin("Result", PinKind::kBool)}},
      .shape = [](const Node&, const Graph& graph, NodePins& pins) { pins.inputs.push_back(AvatarPin(graph)); },
      .expression = [](Emitter& emit, std::string_view) { return "rex.content.is_avatar(" + AvatarInput(emit) + ")"; }});

  // The avatar's property values, as its class declares them: in a class graph, the add menu offers a Get and a Set of
  // each of Self's.
  const SettingDesc property_setting{.name = std::string(kPropertySetting), .kind = SettingKind::kProperty, .tooltip = "The property, of the class of the avatar it acts on"};

  library.Add({.id = "avatar.get_value", .title = "Get Value", .category = category,
      .description = "One of an avatar's property values: the one set on it, or else its class's. In a class graph, Avatar left unlinked is Self.",
      .keywords = "avatar property value get read member",
      .role = NodeRole::kPure, .settings = {property_setting},
      .shape = [](const Node& node, const Graph& graph, NodePins& pins) {
        pins.inputs.push_back(AvatarPin(graph));
        const std::optional<content::Property> property = PickedProperty(node, graph);
        pins.outputs.push_back(ValuePin("Value", property ? PinTypeOf(property->type) : PinType{PinKind::kAny}));
      },
      .title_of = [](const Node& node) { return SettingOf(node, kPropertySetting).empty() ? std::string("Get Value") : "Get " + SettingOf(node, kPropertySetting); },
      .expression = [](Emitter& emit, std::string_view) { return "(" + AvatarInput(emit) + "):get(" + LuaQuote(PropertyName(emit)) + ")"; }});

  library.Add({.id = "avatar.set_value", .title = "Set Value", .category = category,
      .description = "Sets one of an avatar's property values on it alone; its backing shows it. In a class graph, Avatar left unlinked is Self.",
      .keywords = "avatar property value set write member",
      .pins = {{ExecPin()}, {ExecPin(kExecOut)}}, .settings = {property_setting},
      .shape = [](const Node& node, const Graph& graph, NodePins& pins) {
        pins.inputs.push_back(AvatarPin(graph));
        const std::optional<content::Property> property = PickedProperty(node, graph);
        pins.inputs.push_back(property ? FieldPin(*property, "Value") : DataPin("Value", PinKind::kAny));
      },
      .title_of = [](const Node& node) { return SettingOf(node, kPropertySetting).empty() ? std::string("Set Value") : "Set " + SettingOf(node, kPropertySetting); },
      .emit = [](Emitter& emit) { emit.Line("(" + AvatarInput(emit) + "):set(" + LuaQuote(PropertyName(emit)) + ", " + emit.Input("Value") + ")"); }});
}

namespace {

// A class graph's own nodes: RexFabric's events of avatars (the game's are nodes_game.cpp's), Self and Call Parent.
void AddClassGraphs(Library& library) {
  const std::string category = "Avatars";

  // RexFabric's own events of avatars (content.h, kAvatarSpawned), as a blueprint's BeginPlay, Tick and Destroyed.
  struct Own {
    std::string_view event;
    std::string title;
    std::string description;
    std::vector<Pin> outputs;
  };
  const Own own[] = {
      {content::kAvatarSpawned, "On Spawned", "Runs for each avatar of the class once it's spawned, its components attached.", {}},
      {content::kAvatarTick, "On Tick", "Runs for each avatar of the class every frame, after its components ticked, with the seconds since the last.", {DataPin("Seconds", PinKind::kFloat)}},
      {content::kAvatarRemoved, "On Removed", "Runs for each avatar of the class as it goes (removed, or gone with its world), before its components detach. Self can still be read.", {}},
  };
  for (const Own& event : own) {
    NodeDesc desc{.id = ClassEventNode(event.event), .title = event.title, .description = event.description, .role = NodeRole::kEvent, .delegate = true};
    desc.pins.outputs = {ExecPin(kExecOut)};
    std::vector<std::string> names;
    for (const Pin& pin : event.outputs) {
      desc.pins.outputs.push_back(pin);
      names.push_back(pin.name);
    }
    desc.emit = [event = std::string(event.event), names](Emitter& emit) {
      std::vector<std::string> params;
      for (const std::string& name : names) params.push_back(emit.Param(name));
      EmitClassEvent(emit, event, params);
    };
    library.Add(std::move(desc));
  }

  library.Add({.id = "avatar.self", .title = "Self", .category = category,
      .description = "The avatar the class graph runs for: the one its event was fired for.",
      .keywords = "self this me avatar reference target",
      .role = NodeRole::kPure, .compact = true,
      .shape = [](const Node&, const Graph& graph, NodePins& pins) { pins.outputs.push_back(ObjectPin(kSelf, graph.self_class())); },
      .expression = [](Emitter& emit, std::string_view) {
        if (!emit.ClassGraph()) emit.Error("Self is only in an avatar class's own graph");
        return std::string(kSelf);
      },
      .offered = InClassGraph});

  library.Add({.id = "class.call_parent", .title = "Call Parent", .category = category,
      .description = "Runs the parent class's own chain of this event for Self, as a blueprint's Parent node does. An event linked in a class's graph runs in place of its parent's, so this is how both run.",
      .keywords = "parent super base inherited override",
      .pins = {{ExecPin()}, {ExecPin(kExecOut)}},
      .emit = [](Emitter& emit) {
        const std::string event = ClassEventOf(emit.event());
        if (!emit.ClassGraph() || event.empty()) {
          emit.Error("Call Parent runs in the chain of one of the avatar's own events");
          return;
        }
        std::string args = "Class, " + LuaQuote(event) + ", " + std::string(kSelf);
        for (const std::string& param : emit.EventParams()) args += ", " + param;
        emit.Line("rex.content.call_parent(" + args + ")");
      },
      .offered = InClassGraph});
}

}  // namespace

std::string AvatarClassOf(const Node& node, const Graph& graph) {
  const std::optional<PinType> linked = LinkedType(node, graph, kAvatarPin);
  if (!linked) return graph.self_class();
  return linked->kind == PinKind::kObject && content::FindClass(linked->object) ? linked->object : std::string();
}

std::vector<std::string> ClassEventNodes(std::string_view game_class) {
  std::vector<std::string> types = {ClassEventNode(content::kAvatarSpawned), ClassEventNode(content::kAvatarTick), ClassEventNode(content::kAvatarRemoved)};
  for (const content::ClassEventDecl& event : content::ClassEventsOf(game_class)) types.push_back(ClassEventNode(event.event));
  return types;
}

void EmitClassEvent(Emitter& emit, std::string_view event, const std::vector<std::string>& params, const std::function<void()>& convert) {
  if (!emit.ClassGraph()) {
    emit.Error("This event only runs in an avatar class's own graph");
    return;
  }
  // A ghost, as a blueprint's unlinked event is: it replaces nothing, so the parent class's chain of it runs.
  if (!emit.Linked(kExecOut) && !emit.Linked(kDelegatePin)) return;
  std::string list(kSelf);
  for (const std::string& param : params) list += ", " + param;
  emit.Line("rex.content.on_class(Class, " + LuaQuote(event) + ", function(" + list + ")");
  emit.Open();
  if (convert) convert();
  emit.Chain(kExecOut);
  emit.Close();
  emit.Line("end)");
}

void AddContentNodes(Library& library) {
  AddStructs(library);
  AddEnums(library);
  AddTables(library);
  AddMaps(library);
  AddAvatars(library);
  AddClassGraphs(library);
}

}  // namespace rexfabric::graph
