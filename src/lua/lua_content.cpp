// rex.content: the classes (docs/content_and_classes.md) as Lua reads them. A mod reads RexFabric's and the game's
// classes, its own, and those of the mods it depends on; anything else is an error naming the missing dependency.
//
//   for _, path in ipairs(rex.content.classes()) do rex.info(path) end
//   rex.content.default("MyMod/Garden/Decor_Rock_1", "price")      -- 150
//   rex.content.is_a("MyMod/Garden/Decor_Rock_1", "TiP/Decor")     -- true
//   local pinata = rex.content.cast(avatar, "TiP/Pinata")             -- the avatar as a pinata, or nil
//
// Values come as their types say: an array as a list, a map as a table under its keys, a struct as a table of its
// fields, a mod enum's value as its name and a game enum's as its number, as the game's functions take it. A text comes
// in the language the game shows, when the mod has a translation for it (content/localisation.h).
//
//   for _, value in ipairs(rex.content.enum("MyMod/Colour").values) do rex.info(value.name) end
//   rex.content.default("MyMod/Rock", "colours")                       -- {"Red", "Green"}
//   local whirlm = rex.content.row("TiP/Pinatas", "worm")               -- a row of a data table, as its struct
//
// Avatars of mods' classes are spawned, moved and removed here (content/avatars.h):
//
//   local glow = rex.content.spawn("MyMod/Glow", x, y, z, yaw)          -- yaw in degrees
//   glow.x = glow.x + 2; glow:set("brightness", 3); glow:remove()
//   rex.content.spawn("MyMod/Statue", x, y, z, 0, {saved = true})      -- kept in the world's save
//   rex.content.script_asset()                                          -- "MyMod/Leaves/Maze" in that asset's own graph
//
// An avatar class's own graph (docs/content_and_classes.md, Class graphs) runs through on_class, which runs the handler
// of the avatar's nearest class that has one, and keeps each avatar's own variables with own:
//
//   rex.content.on_class(Class, "avatar_tick", function(Self, seconds) ... end)
//   rex.content.call_parent(Class, "avatar_tick", Self, seconds)       -- the handler of the nearest class above Class
//   local My = rex.content.own(Self, Class, Fresh)                      -- Fresh() the first time, kept until it's gone

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <vector>
#include <string>
#include <string_view>

#include "../content/avatars.h"
#include "../content/classes.h"
#include "../content/localisation.h"
#include "../content/tables.h"
#include "../content/types.h"
#include "lua_vm.h"

namespace rexfabric::lua {
namespace {

void PushJson(lua_State* L, const nlohmann::json& value) {
  if (value.is_boolean()) lua_pushboolean(L, value.get<bool>());
  else if (value.is_number_integer()) lua_pushinteger(L, value.get<lua_Integer>());
  else if (value.is_number()) lua_pushnumber(L, value.get<double>());
  else if (value.is_string()) lua_pushstring(L, value.get<std::string>().c_str());
  else lua_pushnil(L);
}

// How deep structs inside structs come before the rest is left out.
constexpr int kMaxDepth = 16;

void PushTyped(lua_State* L, const content::ValueType& type, const nlohmann::json& value, const std::string& key, int depth);

// A value of a single type; `key` is a text's, for its translation.
void PushSingle(lua_State* L, const content::ValueType& type, const nlohmann::json& value, const std::string& key, int depth) {
  luaL_checkstack(L, 4, "content value nested too deep");
  if (type.type == content::PropertyType::kText && value.is_string()) {
    lua_pushstring(L, content::Translate(key, value.get<std::string>()).c_str());
    return;
  }
  if (type.type == content::PropertyType::kEnum && value.is_string()) {
    const std::optional<content::Enum> found = content::FindEnum(type.of);
    const std::string name = value.get<std::string>();
    for (size_t i = 0; found && i < found->numbers.size() && i < found->values.size(); ++i) {
      if (found->values[i].name != name) continue;
      lua_pushinteger(L, static_cast<lua_Integer>(found->numbers[i]));
      return;
    }
    lua_pushstring(L, name.c_str());
    return;
  }
  if (type.type == content::PropertyType::kStruct) {
    const std::optional<content::Struct> found = content::FindStruct(type.of);
    if (!found || !value.is_object() || depth >= kMaxDepth) {
      lua_pushnil(L);
      return;
    }
    lua_createtable(L, 0, static_cast<int>(found->fields.size()));
    for (const content::Property& field : found->fields) {
      // A field the value lacks, or has as the struct has it, is the struct's default, translated as the struct's.
      const auto held = value.find(field.name);
      if (held != value.end() && *held != field.value) PushTyped(L, field.type, *held, content::FieldKey(key, field.name), depth + 1);
      else PushTyped(L, field.type, field.value, content::TextKey(found->path, field.name), depth + 1);
      lua_setfield(L, -2, field.name.c_str());
    }
    return;
  }
  PushJson(L, value);
}

void PushTyped(lua_State* L, const content::ValueType& type, const nlohmann::json& value, const std::string& key, int depth) {
  if (type.container == content::Container::kSingle) return PushSingle(L, type, value, key, depth);
  if (!value.is_array()) {
    lua_pushnil(L);
    return;
  }
  const bool array = type.container == content::Container::kArray;
  lua_createtable(L, array ? static_cast<int>(value.size()) : 0, array ? 0 : static_cast<int>(value.size()));
  lua_Integer index = 0;
  for (const nlohmann::json& entry : value) {
    if (array) {
      PushSingle(L, content::ElementType(type), entry, content::ElementKey(key, static_cast<size_t>(index)), depth);
      lua_rawseti(L, -2, ++index);
      continue;
    }
    // A map's entry under its key; a key of none can't be a Lua key.
    if (!entry.is_array() || entry.size() != 2 || entry[0].is_null()) continue;
    PushSingle(L, content::KeyType(type), entry[0], key, depth);
    PushSingle(L, content::ElementType(type), entry[1], content::EntryKey(key, entry[0]), depth);
    lua_settable(L, -3);
  }
}

// A property or a field as Lua lists it: its name, type, value and help. `owner` is the class or struct its value is
// from, for its texts' translations.
void PushDeclared(lua_State* L, const content::Property& declared, const std::string& owner) {
  lua_createtable(L, 0, 9);
  lua_pushstring(L, declared.name.c_str());
  lua_setfield(L, -2, "name");
  lua_pushstring(L, std::string(content::TypeName(declared.type.type)).c_str());
  lua_setfield(L, -2, "type");
  if (!declared.type.of.empty()) {
    lua_pushstring(L, declared.type.of.c_str());
    lua_setfield(L, -2, "of");
  }
  lua_pushstring(L, std::string(content::ContainerName(declared.type.container)).c_str());
  lua_setfield(L, -2, "container");
  if (declared.type.container == content::Container::kMap) {
    lua_pushstring(L, std::string(content::TypeName(declared.type.key)).c_str());
    lua_setfield(L, -2, "key");
    if (!declared.type.key_of.empty()) {
      lua_pushstring(L, declared.type.key_of.c_str());
      lua_setfield(L, -2, "key_of");
    }
  }
  PushTyped(L, declared.type, declared.value, content::TextKey(owner, declared.name), 0);
  lua_setfield(L, -2, "value");
  if (!declared.declared_by.empty()) {
    lua_pushstring(L, declared.declared_by.c_str());
    lua_setfield(L, -2, "declared_by");
  }
  lua_pushstring(L, declared.help.c_str());
  lua_setfield(L, -2, "help");
}

void PushStrings(lua_State* L, const std::vector<std::string>& strings) {
  lua_createtable(L, static_cast<int>(strings.size()), 0);
  lua_Integer index = 0;
  for (const std::string& text : strings) {
    lua_pushstring(L, text.c_str());
    lua_rawseti(L, -2, ++index);
  }
}

// The class path at `arg`, checked against what the calling mod may use.
std::string CheckPath(lua_State* L, int arg) {
  const std::string path = luaL_checkstring(L, arg);
  std::string why;
  if (!content::MayReference(ContentMod(L), path, &why)) luaL_error(L, "%s", why.c_str());
  return path;
}

int Classes(lua_State* L) {
  const std::string mod = ContentMod(L);
  lua_newtable(L);
  lua_Integer index = 0;
  for (const std::string& path : content::ClassPaths()) {
    if (!content::MayReference(mod, path, nullptr)) continue;
    lua_pushstring(L, path.c_str());
    lua_rawseti(L, -2, ++index);
  }
  return 1;
}

int Class(lua_State* L) {
  const std::optional<content::Class> found = content::FindClass(CheckPath(L, 1));
  if (!found) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 4);
  lua_pushstring(L, found->path.c_str());
  lua_setfield(L, -2, "path");
  lua_pushstring(L, found->parent.c_str());
  lua_setfield(L, -2, "parent");
  lua_pushstring(L, found->mod.c_str());
  lua_setfield(L, -2, "mod");
  lua_pushstring(L, found->help.c_str());
  lua_setfield(L, -2, "help");
  return 1;
}

int IsA(lua_State* L) {
  const std::string path = CheckPath(L, 1);
  lua_pushboolean(L, content::IsA(path, CheckPath(L, 2)));
  return 1;
}

int Properties(lua_State* L) {
  const std::vector<content::Property> properties = content::Properties(CheckPath(L, 1));
  lua_createtable(L, static_cast<int>(properties.size()), 0);
  lua_Integer index = 0;
  for (const content::Property& property : properties) {
    PushDeclared(L, property, property.value_from);
    lua_rawseti(L, -2, ++index);
  }
  return 1;
}

int Default(lua_State* L) {
  const std::string path = CheckPath(L, 1);
  const std::string name = luaL_checkstring(L, 2);
  for (const content::Property& property : content::Properties(path)) {
    if (property.name != name) continue;
    PushTyped(L, property.type, property.value, content::TextKey(property.value_from, property.name), 0);
    return 1;
  }
  return luaL_error(L, "%s has no property %s", path.c_str(), name.c_str());
}

int Enums(lua_State* L) {
  PushStrings(L, content::EnumPaths(ContentMod(L)));
  return 1;
}

int EnumOf(lua_State* L) {
  const std::optional<content::Enum> found = content::FindEnum(CheckPath(L, 1));
  if (!found) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 4);
  lua_pushstring(L, found->path.c_str());
  lua_setfield(L, -2, "path");
  lua_pushstring(L, found->mod.c_str());
  lua_setfield(L, -2, "mod");
  lua_pushstring(L, found->help.c_str());
  lua_setfield(L, -2, "help");
  lua_createtable(L, static_cast<int>(found->values.size()), 0);
  for (size_t i = 0; i < found->values.size(); ++i) {
    const content::EnumValue& value = found->values[i];
    lua_createtable(L, 0, 4);
    lua_pushstring(L, value.name.c_str());
    lua_setfield(L, -2, "name");
    lua_pushstring(L, content::Translate(content::TextKey(found->path, value.name), value.display.empty() ? value.name : value.display).c_str());
    lua_setfield(L, -2, "display");
    lua_pushstring(L, value.help.c_str());
    lua_setfield(L, -2, "help");
    if (i < found->numbers.size()) {
      lua_pushinteger(L, static_cast<lua_Integer>(found->numbers[i]));
      lua_setfield(L, -2, "number");
    }
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  lua_setfield(L, -2, "values");
  return 1;
}

int Structs(lua_State* L) {
  PushStrings(L, content::StructPaths(ContentMod(L)));
  return 1;
}

int StructOf(lua_State* L) {
  const std::optional<content::Struct> found = content::FindStruct(CheckPath(L, 1));
  if (!found) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 4);
  lua_pushstring(L, found->path.c_str());
  lua_setfield(L, -2, "path");
  lua_pushstring(L, found->mod.c_str());
  lua_setfield(L, -2, "mod");
  lua_pushstring(L, found->help.c_str());
  lua_setfield(L, -2, "help");
  lua_createtable(L, static_cast<int>(found->fields.size()), 0);
  lua_Integer index = 0;
  for (const content::Property& field : found->fields) {
    PushDeclared(L, field, found->path);
    lua_rawseti(L, -2, ++index);
  }
  lua_setfield(L, -2, "fields");
  return 1;
}

// Avatars of mods' classes (content/avatars.h), as Lua holds them: a handle by id, so one removed since is an error to
// use rather than a crash, with `valid` to ask first.
constexpr const char* kAvatarMeta = "rex.content.Avatar";

struct AvatarUD {
  uint32_t id;
};


AvatarUD* TestAvatar(lua_State* L, int arg) { return static_cast<AvatarUD*>(luaL_testudata(L, arg, kAvatarMeta)); }

content::AvatarView CheckLiveAvatar(lua_State* L, int arg) {
  const auto* avatar = static_cast<AvatarUD*>(luaL_checkudata(L, arg, kAvatarMeta));
  std::optional<content::AvatarView> view = content::FindAvatar(avatar->id);
  if (!view) luaL_error(L, "the avatar is gone");
  return std::move(*view);
}

constexpr int kMaxJsonDepth = 16;

// A Lua value as content keeps a value of `type`: a game enum's number as its value's name, a list as an array, a
// table of a map's keys as its entries. What doesn't fit is left for FitValue to refuse.
nlohmann::json JsonOf(lua_State* L, int index, const content::ValueType& type, int depth);

nlohmann::json SingleJsonOf(lua_State* L, int index, const content::ValueType& type, int depth) {
  index = lua_absindex(L, index);
  switch (lua_type(L, index)) {
    case LUA_TBOOLEAN: return lua_toboolean(L, index) != 0;
    case LUA_TSTRING: return std::string(lua_tostring(L, index));
    case LUA_TNUMBER: {
      if (type.type == content::PropertyType::kEnum) {
        const std::optional<content::Enum> found = content::FindEnum(type.of);
        const lua_Integer number = lua_tointeger(L, index);
        for (size_t i = 0; found && i < found->numbers.size() && i < found->values.size(); ++i) {
          if (found->numbers[i] == number) return found->values[i].name;
        }
      }
      if (lua_isinteger(L, index)) return static_cast<int64_t>(lua_tointeger(L, index));
      return lua_tonumber(L, index);
    }
    case LUA_TTABLE: {
      if (type.type != content::PropertyType::kStruct || depth >= kMaxJsonDepth) return nullptr;
      const std::optional<content::Struct> found = content::FindStruct(type.of);
      nlohmann::json fields = nlohmann::json::object();
      for (const content::Property& field : found ? found->fields : std::vector<content::Property>{}) {
        lua_getfield(L, index, field.name.c_str());
        if (!lua_isnil(L, -1)) fields[field.name] = JsonOf(L, -1, field.type, depth + 1);
        lua_pop(L, 1);
      }
      return fields;
    }
    default: return nullptr;
  }
}

nlohmann::json JsonOf(lua_State* L, int index, const content::ValueType& type, int depth) {
  if (type.container == content::Container::kSingle || !lua_istable(L, index) || depth >= kMaxJsonDepth) return SingleJsonOf(L, index, type, depth);
  index = lua_absindex(L, index);
  nlohmann::json list = nlohmann::json::array();
  if (type.container == content::Container::kArray) {
    const lua_Integer length = static_cast<lua_Integer>(lua_rawlen(L, index));
    for (lua_Integer i = 1; i <= length; ++i) {
      lua_rawgeti(L, index, i);
      list.push_back(SingleJsonOf(L, -1, content::ElementType(type), depth + 1));
      lua_pop(L, 1);
    }
    return list;
  }
  lua_pushnil(L);
  while (lua_next(L, index) != 0) {
    list.push_back(nlohmann::json::array({SingleJsonOf(L, -2, content::KeyType(type), depth + 1), SingleJsonOf(L, -1, content::ElementType(type), depth + 1)}));
    lua_pop(L, 1);
  }
  return list;
}

// Where the avatar at `arg` is, with one of x, y, z and yaw set to the number at `value`.
content::Transform Moved(lua_State* L, const content::AvatarView& view, std::string_view key, int value) {
  content::Transform to = view.at;
  const double number = luaL_checknumber(L, value);
  if (key == "x") to.position[0] = number;
  else if (key == "y") to.position[1] = number;
  else if (key == "z") to.position[2] = number;
  else to.rotation[1] = number;
  return to;
}

void MoveOrError(lua_State* L, uint32_t id, const content::Transform& to) {
  std::string why;
  if (!content::MoveAvatar(id, to, &why)) luaL_error(L, "%s", why.c_str());
}

int AvatarRemove(lua_State* L) {
  const auto* avatar = static_cast<AvatarUD*>(luaL_checkudata(L, 1, kAvatarMeta));
  lua_pushboolean(L, content::RemoveAvatar(avatar->id));
  return 1;
}

int AvatarMove(lua_State* L) {
  const content::AvatarView view = CheckLiveAvatar(L, 1);
  content::Transform to = view.at;
  to.position = {luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4)};
  to.rotation[1] = luaL_optnumber(L, 5, view.at.rotation[1]);
  MoveOrError(L, view.id, to);
  return 0;
}

int AvatarGet(lua_State* L) {
  const content::AvatarView view = CheckLiveAvatar(L, 1);
  const std::string name = luaL_checkstring(L, 2);
  for (const content::Property& property : content::Properties(view.path)) {
    if (property.name != name) continue;
    const nlohmann::json value = content::AvatarValue(view.id, name).value_or(property.value);
    // A text as its class has it translates as the class's; one set on the avatar is as it was set.
    PushTyped(L, property.type, value, value == property.value ? content::TextKey(property.value_from, property.name) : std::string(), 0);
    return 1;
  }
  return luaL_error(L, "%s has no property %s", view.path.c_str(), name.c_str());
}

int AvatarSet(lua_State* L) {
  const content::AvatarView view = CheckLiveAvatar(L, 1);
  const std::string name = luaL_checkstring(L, 2);
  luaL_checkany(L, 3);
  for (const content::Property& property : content::Properties(view.path)) {
    if (property.name != name) continue;
    std::string why;
    if (!content::SetAvatarValue(view.id, name, JsonOf(L, 3, property.type, 0), &why)) return luaL_error(L, "%s: %s", name.c_str(), why.c_str());
    return 0;
  }
  return luaL_error(L, "%s has no property %s", view.path.c_str(), name.c_str());
}

int AvatarIndex(lua_State* L) {
  const auto* avatar = static_cast<AvatarUD*>(luaL_checkudata(L, 1, kAvatarMeta));
  const std::string_view key = luaL_checkstring(L, 2);
  if (key == "id") {
    lua_pushinteger(L, avatar->id);
    return 1;
  }
  if (key == "valid") {
    lua_pushboolean(L, content::FindAvatar(avatar->id).has_value());
    return 1;
  }
  static constexpr luaL_Reg kMethods[] = {{"remove", AvatarRemove}, {"move", AvatarMove}, {"get", AvatarGet}, {"set", AvatarSet}};
  for (const luaL_Reg& method : kMethods) {
    if (key != method.name) continue;
    lua_pushcfunction(L, method.func);
    return 1;
  }
  const content::AvatarView view = CheckLiveAvatar(L, 1);
  if (key == "class") lua_pushstring(L, view.path.c_str());
  else if (key == "x") lua_pushnumber(L, view.at.position[0]);
  else if (key == "y") lua_pushnumber(L, view.at.position[1]);
  else if (key == "z") lua_pushnumber(L, view.at.position[2]);
  else if (key == "yaw") lua_pushnumber(L, view.at.rotation[1]);
  else if (key == "object") PushValue(L, view.object ? Value(view.object) : Value());
  else if (key == "saved") lua_pushboolean(L, view.saved);
  else return luaL_error(L, "an avatar has no %s", std::string(key).c_str());
  return 1;
}

int AvatarNewIndex(lua_State* L) {
  const std::string_view key = luaL_checkstring(L, 2);
  if (key == "saved") {
    const content::AvatarView view = CheckLiveAvatar(L, 1);
    content::SetAvatarSaved(view.id, lua_toboolean(L, 3) != 0);
    return 0;
  }
  if (key != "x" && key != "y" && key != "z" && key != "yaw") return luaL_error(L, "an avatar's %s can't be set", std::string(key).c_str());
  const content::AvatarView view = CheckLiveAvatar(L, 1);
  MoveOrError(L, view.id, Moved(L, view, key, 3));
  return 0;
}

int AvatarEq(lua_State* L) {
  const AvatarUD* a = TestAvatar(L, 1);
  const AvatarUD* b = TestAvatar(L, 2);
  lua_pushboolean(L, a && b && a->id == b->id);
  return 1;
}

int AvatarToString(lua_State* L) {
  const auto* avatar = static_cast<AvatarUD*>(luaL_checkudata(L, 1, kAvatarMeta));
  const std::optional<content::AvatarView> view = content::FindAvatar(avatar->id);
  lua_pushfstring(L, "rex.content.Avatar %d (%s)", static_cast<int>(avatar->id), view ? view->path.c_str() : "gone");
  return 1;
}

// rex.content.spawn(path, x, y, z, yaw, options): options.saved saves it with its world.
int Spawn(lua_State* L) {
  const std::string path = CheckPath(L, 1);
  content::Transform at;
  at.position = {luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4)};
  at.rotation[1] = luaL_optnumber(L, 5, 0.0);
  bool saved = false;
  if (!lua_isnoneornil(L, 6)) {
    luaL_checktype(L, 6, LUA_TTABLE);
    lua_getfield(L, 6, "saved");
    saved = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
  }
  std::string why;
  const uint32_t id = content::SpawnAvatar(path, at, &why);
  if (id == 0) return luaL_error(L, "can't spawn %s: %s", path.c_str(), why.c_str());
  if (saved) content::SetAvatarSaved(id, true);
  PushAvatar(L, id);
  return 1;
}

// Removes the avatar given, if it's one still there: what Remove Avatar compiles to.
int Remove(lua_State* L) {
  const AvatarUD* avatar = TestAvatar(L, 1);
  if (!avatar && !lua_isnoneornil(L, 1)) return luaL_argerror(L, 1, "an avatar of a mod's class");
  lua_pushboolean(L, avatar && content::RemoveAvatar(avatar->id));
  return 1;
}

int IsAvatar(lua_State* L) {
  const AvatarUD* avatar = TestAvatar(L, 1);
  lua_pushboolean(L, avatar && content::FindAvatar(avatar->id).has_value());
  return 1;
}

int Avatars(lua_State* L) {
  const std::string path = lua_isnoneornil(L, 1) ? std::string() : CheckPath(L, 1);
  const std::vector<uint32_t> ids = content::AvatarsOf(path);
  lua_createtable(L, static_cast<int>(ids.size()), 0);
  lua_Integer index = 0;
  for (const uint32_t id : ids) {
    PushAvatar(L, id);
    lua_rawseti(L, -2, ++index);
  }
  return 1;
}

// rex.content.avatar(value): the avatar a value stands for, an avatar or its id (as the game fires class events with),
// while it's there; nil for anything else.
int AvatarFromValue(lua_State* L) {
  uint32_t id = 0;
  if (const AvatarUD* avatar = TestAvatar(L, 1)) id = avatar->id;
  else if (lua_isinteger(L, 1) && lua_tointeger(L, 1) > 0 && lua_tointeger(L, 1) <= UINT32_MAX) id = static_cast<uint32_t>(lua_tointeger(L, 1));
  if (id == 0 || content::ClassOfAvatar(id).empty()) lua_pushnil(L);
  else PushAvatar(L, id);
  return 1;
}

// Class graphs ---------------------------------------------------------------------------------------------

// Each avatar event's handlers, by event, then by the lower case path of the class whose graph it's from.
constexpr const char* kClassEventsTable = "rexfabric.class_events";
// Each avatar's own tables (rex.content.own), by its id, then by key; dropped once the avatar is gone.
constexpr const char* kOwnTable = "rexfabric.avatar_own";

std::string Lower(std::string_view text) {
  std::string lower(text);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower;
}

// Runs the handler of `event` of the first of `classes` that has one, with the values from `first` to the top of the
// stack (the avatar, then the event's own). Nothing when none has one.
void RunHandler(lua_State* L, const char* event, const std::vector<std::string>& classes, int first) {
  const int last = lua_gettop(L);
  RegistryTable(L, kClassEventsTable);
  lua_getfield(L, -1, event);
  if (lua_istable(L, -1)) {
    for (const std::string& path : classes) {
      lua_getfield(L, -1, Lower(path).c_str());
      if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        continue;
      }
      for (int at = first; at <= last; ++at) lua_pushvalue(L, at);
      lua_call(L, last - first + 1, 0);
      break;
    }
  }
  lua_settop(L, last);
}

// The listener on_class puts on an avatar event: the handler of the avatar's own class, or of the nearest class above
// it that has one, as a blueprint's event runs its most derived override. Its upvalue is the event's name.
int RunClassEvent(lua_State* L) {
  const AvatarUD* avatar = TestAvatar(L, 1);
  const std::string path = avatar ? content::ClassOfAvatar(avatar->id) : std::string();
  if (!path.empty()) RunHandler(L, lua_tostring(L, lua_upvalueindex(1)), content::AncestryOf(path), 1);
  return 0;
}

// rex.content.on_class(class, event, fn): fn(avatar, ...) runs when `event` (an avatar event) is fired for an avatar of
// the class or a class below it, unless a class between them has a handler of its own for it, which runs instead.
int OnClass(lua_State* L) {
  const std::string path = luaL_checkstring(L, 1);
  const std::string event = luaL_checkstring(L, 2);
  luaL_checktype(L, 3, LUA_TFUNCTION);
  if (!content::IsAvatarEvent(event)) return luaL_argerror(L, 2, "an event of avatars: RexFabric's own, or one a game class declares");
  RegistryTable(L, kClassEventsTable);
  lua_getfield(L, -1, event.c_str());
  const bool first = !lua_istable(L, -1);
  if (first) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, event.c_str());
  }
  lua_pushvalue(L, 3);
  lua_setfield(L, -2, Lower(path).c_str());
  lua_pop(L, 2);
  if (!first) return 0;
  // One listener for the event, through rex.on, so it goes with the scripts as theirs do.
  lua_getglobal(L, "rex");
  lua_getfield(L, -1, "on");
  lua_pushstring(L, event.c_str());
  lua_pushstring(L, event.c_str());
  lua_pushcclosure(L, RunClassEvent, 1);
  lua_call(L, 2, 0);
  lua_pop(L, 1);
  return 0;
}

// rex.content.call_parent(class, event, avatar, ...): the handler of `event` of the nearest class above `class` that
// has one, as a blueprint's Parent node runs the event it overrides. Nothing when none has.
int CallParent(lua_State* L) {
  const std::string path = luaL_checkstring(L, 1);
  const char* event = luaL_checkstring(L, 2);
  if (!TestAvatar(L, 3)) return luaL_argerror(L, 3, "an avatar of a mod's class");
  std::vector<std::string> above = content::AncestryOf(path);
  if (!above.empty()) above.erase(above.begin());
  RunHandler(L, event, above, 3);
  return 0;
}

// rex.content.own(avatar, key, make): the avatar's own table under `key` (a class graph's is its class), kept for as
// long as the avatar is there: make() gives it the first time it's asked for (an empty table without a make), and it's
// dropped once the avatar is gone. An error for an avatar that's gone.
int Own(lua_State* L) {
  const auto* avatar = static_cast<AvatarUD*>(luaL_checkudata(L, 1, kAvatarMeta));
  const char* key = luaL_checkstring(L, 2);
  if (content::ClassOfAvatar(avatar->id).empty()) return luaL_error(L, "the avatar is gone");
  RegistryTable(L, kOwnTable);
  lua_rawgeti(L, -1, avatar->id);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, avatar->id);
  }
  lua_getfield(L, -1, key);
  if (lua_istable(L, -1)) return 1;
  lua_pop(L, 1);
  if (lua_isnoneornil(L, 3)) {
    lua_newtable(L);
  } else {
    luaL_checktype(L, 3, LUA_TFUNCTION);
    lua_pushvalue(L, 3);
    lua_call(L, 0, 1);
    if (!lua_istable(L, -1)) return luaL_error(L, "make gave a %s, not a table", luaL_typename(L, -1));
  }
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, key);
  return 1;
}

// An avatar is gone for good: its own tables go too.
void ForgetOwn(uint32_t id) {
  VM& vm = Vm();
  std::lock_guard lock(vm.mutex);
  if (!vm.L) return;
  RegistryTable(vm.L, kOwnTable);
  lua_pushnil(vm.L);
  lua_rawseti(vm.L, -2, id);
  lua_pop(vm.L, 1);
}

// The value as an object of a type or class, or nil when it isn't one: the cast node's Lua. An avatar is one of its class
// and the mod classes above it, and as a game type or class it's its backing's object; a game object is one of a mod's
// class when it's the object of an avatar of it.
int Cast(lua_State* L) {
  const std::string name = luaL_checkstring(L, 2);
  if (const AvatarUD* avatar = TestAvatar(L, 1)) {
    const std::optional<content::AvatarView> view = content::FindAvatar(avatar->id);
    // Cast to a mod's class it's itself; to a game type or class, its backing's object.
    const std::optional<content::Class> to_class = FindType(name) ? std::nullopt : content::FindClass(CheckPath(L, 2));
    if (view && to_class && !to_class->mod.empty() && content::IsA(view->path, to_class->path)) {
      lua_pushvalue(L, 1);
      return 1;
    }
    if (!view || !view->object) {
      lua_pushnil(L);
      return 1;
    }
    PushValue(L, Value(view->object));
    lua_replace(L, 1);
  }
  if (const TypeDef* type = FindType(name)) {
    PushValue(L, Value(AsObject(L, 1, *type)));
    return 1;
  }
  const std::string path = CheckPath(L, 2);
  const std::optional<content::Class> cast_to = content::FindClass(path);
  if (!cast_to) return luaL_error(L, "there's no class or type %s", path.c_str());
  const TypeDef* type = FindType(content::TypeOfClass(path));
  const Object object = type ? AsObject(L, 1, *type) : Object{};
  if (!cast_to->mod.empty()) {
    const uint32_t id = object ? content::AvatarOfObject(object) : 0;
    const std::optional<content::AvatarView> view = id ? content::FindAvatar(id) : std::nullopt;
    if (view && content::IsA(view->path, path)) PushAvatar(L, id);
    else lua_pushnil(L);
    return 1;
  }
  std::string refusal;
  bool is = false;
  try {
    is = object && content::IsOfClass(path, object);
  } catch (const std::exception& error) {
    refusal = error.what();
  }
  if (!refusal.empty()) return luaL_error(L, "%s: %s", path.c_str(), refusal.c_str());
  PushValue(L, is ? Value(object) : Value());
  return 1;
}

int Tables(lua_State* L) {
  PushStrings(L, content::TablePaths(ContentMod(L)));
  return 1;
}

int TableOf(lua_State* L) {
  const std::optional<content::Table> found = content::FindTable(CheckPath(L, 1));
  if (!found) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 6);
  lua_pushstring(L, found->path.c_str());
  lua_setfield(L, -2, "path");
  lua_pushstring(L, found->mod.c_str());
  lua_setfield(L, -2, "mod");
  lua_pushstring(L, found->help.c_str());
  lua_setfield(L, -2, "help");
  lua_pushstring(L, found->row.c_str());
  lua_setfield(L, -2, "row");
  lua_pushboolean(L, found->game);
  lua_setfield(L, -2, "game");
  std::vector<std::string> names;
  for (const auto& [name, value] : found->rows) names.push_back(name);
  PushStrings(L, names);
  lua_setfield(L, -2, "names");
  return 1;
}

int Row(lua_State* L) {
  const std::string path = CheckPath(L, 1);
  const std::optional<nlohmann::json> row = content::TableRow(path, luaL_checkstring(L, 2));
  if (!row) {
    lua_pushnil(L);
    return 1;
  }
  const std::string name = luaL_checkstring(L, 2);
  PushTyped(L, {.type = content::PropertyType::kStruct, .of = content::RowStruct(path)}, *row, content::TextKey(path, name), 0);
  return 1;
}

int RowNames(lua_State* L) {
  PushStrings(L, content::RowNames(CheckPath(L, 1)));
  return 1;
}

int HasRow(lua_State* L) {
  const std::string path = CheckPath(L, 1);
  lua_pushboolean(L, content::HasRow(path, luaL_checkstring(L, 2)));
  return 1;
}

int ValidName(lua_State* L) {
  std::string why;
  const bool valid = content::ValidName(luaL_checkstring(L, 1), &why);
  lua_pushboolean(L, valid);
  if (valid) return 1;
  lua_pushstring(L, why.c_str());
  return 2;
}

// The game asset whose own graph the calling script is: the script compiled beside the asset in its mod's content
// folder ("mods/MyMod/content/Leaves/Maze.graph.lua" is "MyMod/Leaves/Maze"), so a renamed or copied asset's graph
// knows its asset without being compiled again. Nil for any other script.
int ScriptAsset(lua_State* L) {
  constexpr std::string_view kContent = "/content/";
  constexpr std::string_view kGraphScript = ".graph.lua";
  lua_Debug ar;
  for (int level = 1; lua_getstack(L, level, &ar); ++level) {
    lua_getinfo(L, "S", &ar);
    const std::string_view source = ar.source ? ar.source : "";
    const size_t content = source.find(kContent);
    if (source.empty() || source.front() != '@' || !source.ends_with(kGraphScript) || content == std::string_view::npos) continue;
    const std::string_view mod_dir = source.substr(0, content);
    const std::string_view inside = source.substr(content + kContent.size(), source.size() - content - kContent.size() - kGraphScript.size());
    const std::string asset = std::string(mod_dir.substr(mod_dir.rfind('/') + 1)) + "/" + std::string(inside);
    lua_pushstring(L, asset.c_str());
    return 1;
  }
  lua_pushnil(L);
  return 1;
}

constexpr luaL_Reg kContentFuncs[] = {
    {"classes", Classes}, {"class", Class}, {"is_a", IsA}, {"properties", Properties}, {"default", Default}, {"cast", Cast}, {"valid_name", ValidName},
    {"enums", Enums}, {"enum", EnumOf}, {"structs", Structs}, {"struct", StructOf},
    {"tables", Tables}, {"table", TableOf}, {"row", Row}, {"row_names", RowNames}, {"has_row", HasRow},
    {"spawn", Spawn}, {"remove", Remove}, {"is_avatar", IsAvatar}, {"avatars", Avatars}, {"avatar", AvatarFromValue},
    {"script_asset", ScriptAsset}, {"on_class", OnClass}, {"call_parent", CallParent}, {"own", Own},
    {nullptr, nullptr},
};

}  // namespace

void PushAvatar(lua_State* L, uint32_t id) {
  auto* avatar = static_cast<AvatarUD*>(lua_newuserdata(L, sizeof(AvatarUD)));
  avatar->id = id;
  luaL_setmetatable(L, kAvatarMeta);
}

bool AvatarObject(lua_State* L, int arg, Object& object) {
  const AvatarUD* avatar = TestAvatar(L, arg);
  if (!avatar) return false;
  const std::optional<content::AvatarView> view = content::FindAvatar(avatar->id);
  object = view ? view->object : Object{};
  return true;
}

void OpenContent(lua_State* L) {
  luaL_newmetatable(L, kAvatarMeta);
  lua_pushcfunction(L, AvatarIndex);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, AvatarNewIndex);
  lua_setfield(L, -2, "__newindex");
  lua_pushcfunction(L, AvatarEq);
  lua_setfield(L, -2, "__eq");
  lua_pushcfunction(L, AvatarToString);
  lua_setfield(L, -2, "__tostring");
  lua_pop(L, 1);
  luaL_newlib(L, kContentFuncs);
  lua_setfield(L, -2, "content");
  content::OnAvatarGone(ForgetOwn);
}

}  // namespace rexfabric::lua
