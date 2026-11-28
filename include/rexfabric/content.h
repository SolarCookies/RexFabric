// Content: the classes a game declares and the ones mods make as assets in their content folders
// (docs/content_and_classes.md). Every class comes from Object; a class names its parent and keeps only the
// properties it adds and the defaults it changes.
//
// A game class can stand for one of the game's types (rexfabric::Namespace::Type), so the class and the type are one
// thing to graphs and Lua: what the type's objects are, and where they sit in the hierarchy. A class below it without
// a type of its own says which of the type's objects are one of it.
//
//   rexfabric::content::DeclareClass({.path = "TiP/Object", .type = "tip.Object", .help = "Any of the game's objects"});
//   rexfabric::content::DeclareClass({.path = "TiP/Avatar", .parent = "TiP/Object", .type = "tip.Avatar"});
//   rexfabric::content::DeclareClass({.path = "TiP/Pinata", .parent = "TiP/Avatar", .test = IsPinata});
//
// Properties can also hold the game's enums and structs, and arrays and maps of any of their types:
//
//   rexfabric::content::DeclareStruct({.path = "TiP/TagRequirement", .fields = {{.name = "tag", .type = {.type = PropertyType::kInt}}, {.name = "requirement", .type = {.type = PropertyType::kEnum, .of = "tip.Requirement"}}}});

#pragma once

#include <any>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <rexfabric/game_api.h>

namespace rexfabric::content {

// What a property holds. An asset property is a path to another asset ("MyMod/Thumbs/Rock"), or empty for none; an
// enum property one of an enum's values, by name; a struct property a value of each of the struct's fields.
enum class PropertyType { kBool, kInt, kFloat, kString, kText, kAsset, kEnum, kStruct };

// One value, an ordered list of them, or values each under a key.
enum class Container { kSingle, kArray, kMap };

// A property's or a struct field's type: a property type, in a container.
struct ValueType {
  PropertyType type = PropertyType::kInt;
  // What the type names. kEnum: the enum, a game's by its Lua name ("tip.Weather") or a mod's by its path; kStruct:
  // the struct; kAsset: the class the asset must be, empty for any; kString: a data table whose row names it's one of
  // ("TiP/Effects", the game's effects), which the editor offers to pick from, empty for any string.
  std::string of;
  Container container = Container::kSingle;
  // A map's keys: bool, int, string, an enum or an asset, with what that names.
  PropertyType key = PropertyType::kString;
  std::string key_of;

  bool operator==(const ValueType&) const = default;
};

// A property's value, as a game gives one: none (an empty asset, or a container's or struct's empty value), or a value
// of its type. An enum value is its name.
using PropertyValue = std::variant<std::monostate, bool, int64_t, double, std::string>;

struct PropertyDecl {
  std::string name;
  ValueType type;
  PropertyValue value;
  std::string help;
};

// Whether an object of the class's type is one of the class: is this avatar a pinata. It gets the object as the type
// holds it, on the thread the mod asking runs on.
using ClassTest = std::function<bool(const Object& object)>;

// A game event an avatar class's graph can start with (ClassDecl::events; docs/content_and_classes.md, Class graphs):
// the game fires it for one avatar, its Args naming the avatar first ("tip.avatar_interacted(avatar, button)"), and the
// graph of the avatar's class runs it for that avatar as Self. The game fires it with the avatar's id first, as an
// int64 (ComponentContext::avatar; AvatarOfBacking finds it from a backing's handle). Lua gets that value as the avatar
// itself, in a class graph and in a plain rex.on alike; fired with an id no avatar has now, it reaches no Lua at all.
struct ClassEventDecl {
  std::string event;  // its full name ("tip.avatar_interacted")
  std::string title;  // its node's title on a class graph ("On Interact"); the event's own when empty
};

struct ClassDecl {
  // Under a path of the game's own ("TiP/Pinata").
  std::string path;
  // "Object" or a class declared before it.
  std::string parent = "Object";
  std::string help;
  // The game type the class stands for, by full name ("tip.Avatar"). Empty: its objects are of its parent's type.
  std::string type;
  // For a class without a type of its own, which of its type's objects are one of it. Unset: every one of them that is
  // one of its parent.
  ClassTest test;
  std::vector<PropertyDecl> properties;
  // Game events its avatars' class graphs can start with, beside RexFabric's own (spawned, ticked, removed); a class
  // has its ancestors' too. Each is declared with Namespace::Event, its Args naming the avatar first.
  std::vector<ClassEventDecl> events;
};

// Declares one of the game's classes. Before rexfabric::Start, after the types it names; false (logged) for a bad
// name, a taken path, an unknown parent or an unknown type.
bool DeclareClass(ClassDecl decl);

// A struct of the game's: a named set of typed fields, each with a default, which mods use as they use their own
// structs and can't change. The game's enums are the ones it declares through its API (Namespace::Enum), named as
// Lua names them ("tip.Weather").
struct StructDecl {
  // Under a path of the game's own ("TiP/TagRequirement").
  std::string path;
  std::string help;
  std::vector<PropertyDecl> fields;
};

// Declares one of the game's structs. Before rexfabric::Start, after the enums and structs its fields name; false
// (logged) for a bad name, a taken path or a field of a type that isn't there.
bool DeclareStruct(StructDecl decl);

// A language the game's texts come in: its code, as its files name it ("french"), and its name as the player reads it.
struct LanguageDecl {
  std::string code;
  std::string name;
};

// Declares the game's languages, the first being the one a mod writes in when it doesn't say, and how to tell which one
// the game shows now (from any thread; empty when it can't say yet). Mods' texts translate into it
// (docs/content_and_classes.md, Localisation). Before rexfabric::Start.
void DeclareLanguages(std::vector<LanguageDecl> languages, std::function<std::string()> current);

// One of the game's own texts (docs/content_and_classes.md, The game's texts).
struct GameText {
  // How the game finds it, the same in every language: what a language asset keys its text by.
  std::string id;
  // Its name, where the game's API knows one; empty where it doesn't.
  std::string label;
  // As the language the game shows has it.
  std::string text;
};

// The texts the game keeps together (in Trouble in Paradise, a loctext asset), under the group's name.
struct GameTextGroup {
  std::string name;
  std::vector<GameText> texts;
};

// How a game lets mods change its own texts, with language assets: what its texts are, and how to show the mods'.
struct GameTextsDecl {
  // Lists every text as the language the game shows has it. RexFabric calls it when the texts are first read, and
  // again after GameTextsChanged, on whatever thread reads them. False when it can't now (off the thread the game's
  // data lives on, or before its data is there): the texts stay as they were until the game calls FillGameTexts.
  std::function<bool(std::vector<GameTextGroup>& groups)> list;
  // The mods' texts in place of the game's: by language code, then by text id. Given again whenever they change (mods
  // loaded, a language asset saved), from any thread; a language the game doesn't show now waits until it does.
  std::function<void(std::map<std::string, std::map<std::string, std::string>> texts)> apply;
};

// Declares how mods change the game's texts. Before rexfabric::Start; at most one.
void DeclareGameTexts(GameTextsDecl decl);

// The game's texts changed (a language loaded): they're listed again the next time they're read. Any thread.
void GameTextsChanged();

// Lists the game's texts now, from where its `list` can run; false when it still can't. For a game whose `list`
// refused a reader on another thread.
bool FillGameTexts();

// Builds a game table's rows, one row at a time: Row starts one, and the rest fill its fields. A field left alone is
// its default.
class TableRows {
 public:
  virtual ~TableRows() = default;
  // A row under `name`, which follows the asset name rules and is the table's only row of that name.
  virtual void Row(std::string name) = 0;
  // Sets a field of the row to one value: for a struct field, its fields go in as "field.inner".
  virtual void Set(std::string_view field, PropertyValue value) = 0;
  // Adds a value to an array field.
  virtual void Add(std::string_view field, PropertyValue value) = 0;
  // Puts a value under a key in a map field; for a map of structs, `inner` names the struct's field it sets.
  virtual void Put(std::string_view field, PropertyValue key, PropertyValue value, std::string_view inner = {}) = 0;
};

// A data table of the game's (docs/content_and_classes.md, The game's tables): rows of one of its structs, made from
// the game's data rather than a file, which mods read as they read their own tables and can't change.
struct TableDecl {
  // Under a path of the game's own ("TiP/Pinatas").
  std::string path;
  std::string help;
  // The struct each row is, declared before.
  std::string row;
  // Fills the rows as the game is now. RexFabric calls it when the table is read and its rows are out of date (never
  // made, or TableChanged said so), on whatever thread reads it. False when it can't now (off the thread the game's
  // data lives on, or before the data is there): the rows stay as they were until the game calls FillTable.
  std::function<bool(TableRows& rows)> rows;
};

// Declares one of the game's tables. Before rexfabric::Start, after its row struct; false (logged) for a bad name, a
// taken path or a struct that isn't there.
bool DeclareTable(TableDecl decl);

// What a game table describes changed (a price set, a requirement moved): its rows are made again the next time it's
// read. Any thread.
void TableChanged(std::string_view path);

// Makes a game table's rows now, from where its `rows` can run; false when it still can't. For a game whose `rows`
// refused a reader on another thread.
bool FillTable(std::string_view path);

// One row of a data table, the game's or a loaded mod's, by its name: its fields as a JSON object in text, every field of
// the table's struct there. None for a table or a row that isn't there, as for a game table whose rows aren't made yet
// (they're made first when they're out of date and the game can make them now). Any thread.
std::optional<std::string> FindTableRow(std::string_view table, std::string_view row);

// A list of strings a game offers for a string to be picked from (docs/content_and_classes.md, String lists): named by a
// kString's ValueType::of as a data table is, its editors' pickers offer its strings in its order. Unlike a table it has
// no rows of a struct, so it can mix what no table holds together (Trouble in Paradise's effects: the mods' FX Emitter
// assets by path first, then the game's effects by name). `list` makes it each time a picker opens, on the UI thread.
struct StringListDecl {
  // Under a path of the game's own ("TiP/AnyEffect").
  std::string path;
  std::string help;
  std::function<std::vector<std::string>()> list;
};

// Declares one of the game's string lists. Before rexfabric::Start; false (logged) for a bad path, a taken one (a
// table's, class's or struct's too) or no list.
bool DeclareStringList(StringListDecl decl);

// What a string of ValueType::of `of` is picked from: a string list's strings, or a table's row names; empty for
// neither. Any thread, though a string list is made on the thread asking.
std::vector<std::string> StringChoices(std::string_view of);

// Where something is: a position, a rotation (pitch, yaw and roll, in degrees, turned yaw first, then pitch, then
// roll) and a scale, in the game's units and axes, y up.
struct Transform {
  std::array<double, 3> position{0.0, 0.0, 0.0};
  std::array<double, 3> rotation{0.0, 0.0, 0.0};
  std::array<double, 3> scale{1.0, 1.0, 1.0};
  bool operator==(const Transform&) const = default;
};

// A transform as a 4 x 4 matrix of row vectors (ImGuizmo's layout, and the game's): the x, y and z axes scaled, then the
// position, as the editors place things.
std::array<float, 16> MatrixOf(const Transform& transform);
// A matrix of MatrixOf's layout as a transform, its axes' lengths as the scale.
Transform TransformOf(const std::array<float, 16>& matrix);

// An avatar's property values, as its backing and components see them: every property of its class, by name, as the
// avatar has it. A value no PropertyValue holds (an array, a map, a struct) is none; an enum's is its value's name.
using AvatarValues = std::map<std::string, PropertyValue>;

// What the game makes in the world for an avatar of a mod's class (docs/content_and_classes.md, Avatars): a backing
// is offered to the mod classes whose nearest game class is `for_class`, the first one declared for a class being the
// one its mod classes get when they name none. Every function runs on the thread the avatar is spawned and ticked on,
// the game's, and any may throw to refuse, with the reason as its message.
struct BackingDecl {
  // Under a path of the game's own ("TiP/Place").
  std::string path;
  std::string help;
  // The game class it's for ("TiP/Avatar").
  std::string for_class;
  // Makes it where `at` says, as the avatar's values say; its handle, never 0.
  std::function<uint64_t(const Transform& at, const AvatarValues& values)> make;
  // Where it is now, into `at`; false once it's gone from the world (its world went, the game removed it).
  std::function<bool(uint64_t handle, Transform& at)> where;
  std::function<void(uint64_t handle, const Transform& to)> move;
  std::function<void(uint64_t handle)> remove;
  // The game's object it is, for a backing that makes one; unset, or none, for one that's only a place.
  std::function<Object(uint64_t handle)> object;
  // One of the avatar's values was set on it: the backing shows them as they are now. Unset for a backing that only
  // reads them as it's made.
  std::function<void(uint64_t handle, const AvatarValues& values)> apply;
};

// Declares one of the game's backings. Before rexfabric::Start, after the class it's for; false (logged) for a bad
// path, a taken one, an unknown class or a missing function.
bool DeclareBacking(BackingDecl decl);

// The id of the avatar a backing made: the backing's path and the handle its `make` gave. 0 for none (never made, or
// that avatar removed since). Any thread. A game fires its class events (ClassEventDecl) with this id.
uint32_t AvatarOfBacking(std::string_view backing, uint64_t handle);

// RexFabric's own events of avatars (docs/content_and_classes.md, Class graphs), fired on the thread the avatar is
// spawned, ticked or removed on, each with the avatar's id first: spawned once its components are attached, tick each
// frame after its components ticked (with the seconds since the last), removed before its components detach. Lua gets
// the avatar itself, as for a class event.
inline constexpr std::string_view kAvatarSpawned = "avatar_spawned";
inline constexpr std::string_view kAvatarTick = "avatar_tick";
inline constexpr std::string_view kAvatarRemoved = "avatar_removed";

// What a component's functions are given: the avatar, where the component is, its values, and room for its own state.
struct ComponentContext {
  uint32_t avatar = 0;      // the avatar's id, as Lua has it
  std::string avatar_class;
  std::string name;         // the component's name in its class
  Transform world;          // where it is in the world now: the avatar's place and the components above it
  std::map<std::string, PropertyValue> properties;  // each of its type's properties, as the class sets it
  double seconds = 0.0;     // since the last tick; 0 when attached and detached
  std::any state;           // the component's own, kept from call to call for as long as the avatar is there
};

// A kind of component the game offers (docs/content_and_classes.md, Components): its properties, whether it has a
// place of its own, and what it does when its avatar is spawned, every tick while it's there, and when it goes. Each
// function runs on the game's thread and may throw: a throw is logged, and a tick that throws isn't called again.
struct ComponentDecl {
  // Under a path of the game's own ("TiP/FxEmitter").
  std::string path;
  std::string help;
  // Whether it has a transform, relative to the component above it or to the avatar.
  bool transform = true;
  std::vector<PropertyDecl> properties;
  std::function<void(ComponentContext& context)> attach;
  std::function<void(ComponentContext& context)> tick;
  std::function<void(ComponentContext& context)> detach;
};

// Declares one of the game's components. Before rexfabric::Start; false (logged) for a bad path, a taken one, or a
// property of a type that isn't there.
bool DeclareComponent(ComponentDecl decl);

// A world of the game's (docs/content_and_classes.md, Map edits): a place it loads, which mods' map edits change each
// time it's entered: avatars spawned in it, and later what a game's own kinds of edit do (a model replaced, an actor
// taken away).
struct WorldDecl {
  // Under a path of the game's own ("TiP/TitleScreen").
  std::string path;
  std::string help;
};

// Declares one of the game's worlds. Before rexfabric::Start; false (logged) for a bad path or a taken one.
bool DeclareWorld(WorldDecl decl);

// The world the game is in now, by its path, or empty for none of its declared worlds (between worlds, loading). The
// map edits for it are applied on the next tick, as the game's thread ticks RexFabric, and undone when the game leaves
// it: a new path, or the same one given again after an empty one, is a new visit. A mod's world (docs/
// content_and_classes.md, Worlds) may be given where the game is in the world it's built on: its edits are applied and
// its base's too. Any thread.
void SetWorld(std::string_view path);

// The game's world a world comes down to: itself for one of the game's, and for a mod's the game's world at the end of
// its bases; empty for no world RexFabric knows or one not built on the game's. Any thread.
std::string GameWorldOf(std::string_view path);

// The model file of a loaded mod's mesh asset ("MyMod/Town"; docs/content_and_classes.md, Meshes): the binary glTF
// beside it, for the game to read. None, with `why`, for a path that isn't one. Any thread.
std::optional<std::filesystem::path> MeshModelOf(std::string_view path, std::string* why = nullptr);

// The mods' part of a world's save (docs/content_and_classes.md, Saved with the world): the avatars spawned to be saved
// (rex.content.spawn's `saved`), each as a record of its class, where it is and the values it has of its own. The game
// writes WorldSaveData into its save as it saves a world, and gives RestoreWorldSave what it read back once that world
// has loaded, so they're spawned again where they were. A record whose class isn't loaded (its mod gone or turned off)
// isn't spawned: it's kept as it was and goes into the world's next save, so a mod turned off for a while finds its
// avatars again. Both on the thread the avatars tick on.
std::string WorldSaveData();
// Spawns the records in `data`, as WorldSaveData gave it, and keeps the ones it can't; empty for a world saved without
// any (a new one, or one saved without mods), which forgets the records kept for the last. Gives how many it spawned.
size_t RestoreWorldSave(std::string_view data);

// A part of a world's save the game keeps for itself beside the mods' avatars and data (docs/content_and_classes.md,
// Saved with the world): what it adds to its worlds for mods that isn't an avatar, such as Trouble in Paradise's mod
// packets poured on the ground. `save` gives the part as JSON text, empty for nothing to keep; `restore` gets what the
// world loaded with, empty for none, once its saved avatars are back. Both on the thread the avatars tick on. A part
// a save holds that no declaration names is kept as it came and saved again.
struct WorldSavePartDecl {
  std::string name;
  std::function<std::string()> save;
  std::function<void(std::string_view json)> restore;
};

// Declares a part. Before rexfabric::Start; false (logged) for a bad name, a taken one or a missing function.
bool DeclareWorldSavePart(WorldSavePartDecl decl);

// Game asset types (docs/content_and_classes.md, Game asset types): kinds of asset a game adds beside RexFabric's own,
// each a set of typed fields its files hold at their top level, laid out in a generic editor. RexFabric knows nothing
// of what they mean: it reads, checks, edits, renames, translates and lists them, and the game reads them back.
//
// A field can show only while another field holds one of some values, so one asset type can hold several shapes (a
// packet's shop level and price only while it's sold in the shop). Two fields may even share a name under conditions
// that never hold together, each typing the value its own way (a packet's look, a struct of its base's). A field
// hidden by its condition isn't loaded, checked or saved.
struct AssetFieldDecl {
  std::string name;
  ValueType type;
  // The default, for a single value; none is the type's empty value (an enum's first value, a struct's defaults).
  PropertyValue value;
  // As the editor names it; the name when empty.
  std::string label;
  std::string help;
  // Shown only while the field `when_field`, declared before it, holds one of `when_values`: an enum value's name, a
  // bool's "true" or "false", a number as text. Empty `when_field`: always shown.
  std::string when_field;
  std::vector<std::string> when_values;
  // Edited by the type's own editor (AssetTypeDecl::editor) alone, so left out of the field table; loaded, checked and
  // saved as any field is. Ignored for a type without an editor.
  bool custom = false;
};

// One asset of a game asset type, as the game reads it.
struct GameAsset {
  std::string path;  // "TiP/Sand", "MyMod/RedSand"
  std::string mod;   // the mod it's from, empty for the game's own
  // Every field shown for it, as a JSON object of the fields by name: its value, or the field's default, each fitted to
  // its type (a struct with every field, an empty asset null) and its texts translated into the language the game
  // shows. The JSON is text, so a game reads it with whatever it parses JSON with.
  std::string json;
  // Its single fields as values (an enum's by its name), the containers and structs none: for reading the simple
  // fields without parsing the JSON.
  AvatarValues values;
};

// One layer of a game asset's 2D preview, drawn over the layers before it inside the preview's square: a picture (a
// mod's Texture asset, or an image file of the game's), as large as fits its part of the square with its shape kept,
// or a filled shape filling its part; each tinted.
struct PreviewLayer {
  std::string texture;            // a Texture asset ("MyMod/Icon"), empty for none
  std::filesystem::path file;     // an image file, when there's no texture
  // A filled outline when there's no picture: points as fractions of `rect`, from its top left, in order round it.
  std::vector<std::array<float, 2>> shape;
  std::array<float, 4> rect{0.0f, 0.0f, 1.0f, 1.0f};    // left, top, width and height, as fractions of the square
  std::array<float, 4> colour{1.0f, 1.0f, 1.0f, 1.0f};  // the shape's fill, or the picture's tint: red, green, blue, alpha
  bool stretch = false;  // a picture fills `rect` exactly, its shape not kept (as a game stretches it over a box)
  // The part of the square it's cut to, as `rect` gives one; none while its width is 0.
  std::array<float, 4> clip{0.0f, 0.0f, 0.0f, 0.0f};
};

// One of the class's components as the viewport shows it, for a game that shows what it does (an emitter playing):
// where the avatar puts it and its values. An asset viewport's scene (AssetViewportScene) is made of them too.
struct ViewportPart {
  std::string name;
  std::string type;     // its component type ("TiP/FxEmitter")
  Transform world;      // in the avatar's space
  AvatarValues values;  // each of its type's properties, as the class sets it
  bool own = false;     // the class's own, not inherited
  bool operator==(const ViewportPart&) const = default;
};

// What a game asset type's viewport shows of one asset (AssetViewportDecl::build): the values its backing is shown
// with, and parts on the stage beside it, each shown as the game's viewport renderer shows a component of its type in a
// class's viewport (an emitter playing). A part's type needn't be a declared component, only one the renderer knows.
struct AssetViewportScene {
  AvatarValues values;
  std::vector<ViewportPart> parts;
  bool operator==(const AssetViewportScene&) const = default;
};

// A game asset type's live 3D preview (docs/content_and_classes.md, Asset viewports): the viewport a class has, drawn
// by the game's viewport renderer as it draws a class's, showing an asset as its editor has it now, saved or not. Its
// toolbar plays it: Play and Pause (ViewportView::playing), Restart (ViewportView::replays), Loop
// (ViewportView::looping) and how long it's played (ViewportImage::time). What playing means is the game's to say.
struct AssetViewportDecl {
  // The backing the renderer shows, as a class's viewport shows its class's; empty for none, the parts alone.
  std::string backing;
  // What the viewport shows of the asset, built from its path and its fields as the editor has them now, given as
  // GameAsset::json has it but untranslated. Called as the editor opens and again whenever its fields change, not every
  // frame; a scene unlike the one it gave last starts everything over, as Restart does. May throw to say what it can't
  // show, which the editor shows over the viewport. Unset: the backing alone, with no values. On the UI thread.
  std::function<AssetViewportScene(const std::string& path, const std::string& json)> build;
  // Whether Loop starts on.
  bool looping = true;
};

// An event a game asset's own graph starts with (AssetTypeDecl::events).
struct AssetEventDecl {
  std::string event;  // its full name ("tip.star_leaf_pressed")
  std::string title;  // its node's title on the asset's graph ("On Pressed"); the event's own when empty
  // For an event made from an asset's fields (AssetTypeDecl::asset_events): the values the event's arguments after the
  // asset's path must equal for this node to run, in order ({"Romance"} for tip.catalog_action_bought(catalog, action)
  // and the action named Romance). Empty: the node runs for every firing for the asset.
  std::vector<std::string> match;
};

struct AssetTypeDecl {
  // The class its assets are, under a path of the game's own ("TiP/Packet"): an asset property of it picks one.
  std::string path;
  // What its files say under "asset" ("packet"): unique, and none of RexFabric's own kinds.
  std::string kind;
  // How the content browser names it ("Packet"): its New entry, its tiles, its editor's title.
  std::string label;
  // The submenu of the content browser's New menus it's in ("UI"), beside RexFabric's own Basic and Advanced (a game's
  // type may join either); empty for Other, after the rest.
  std::string category;
  std::string help;
  std::vector<AssetFieldDecl> fields;
  // The fields a new asset is asked for before it's made (a packet's base), each one of `fields`.
  std::vector<std::string> create_with;
  // A text or string field a new asset's name goes into, as a start ("name"); empty for none.
  std::string name_field;
  // What's wrong with an asset, given as GameAsset::json has it but untranslated: each problem a line its editor shows,
  // and the reason it isn't loaded. Unset or empty for nothing wrong. Any thread.
  std::function<std::vector<std::string>(const std::string& json)> check;
  // What's doubtful about an asset that loads all the same (a palette missing a colour, another asset it names not
  // loaded), given as `check` is: each a line its editor shows below the problems, in the warning colour. Unlike
  // `check`, it may read other assets, as it only runs while an editor is drawn, once they've loaded. Unset or empty for
  // nothing. On the UI thread.
  std::function<std::vector<std::string>(const std::string& json)> warn;
  // The game's own assets of the type, read only, listed first and in this order, and offered in every picker of it.
  // Each gives its path (under the game's own) and its JSON; `mod` and `values` are filled in.
  std::vector<GameAsset> built_in;
  // Its tiles' colour in the content browser, red, green and blue from 0 to 1.
  std::array<float, 3> colour{0.6f, 0.7f, 0.8f};
  // A 2D preview its editor shows of an asset as its fields are now, given as GameAsset::json has it but untranslated:
  // the layers to draw, bottom first (a star leaf: the leaf in its colour, its icon over it). Unset for no preview. On
  // the UI thread, every frame the editor is drawn.
  std::function<std::vector<PreviewLayer>(const std::string& json)> preview;
  // The type's own editor, for what the field table can't show well (a palette's colours over the pattern they colour):
  // drawn with ImGui, mouse and all, in the asset's editor below the field table, every frame the editor is drawn, on
  // the UI thread. It gets the asset's path and its fields as JSON text as the editor holds them now (every field shown
  // for it there, as its file has them: not fitted, so read them as a check reads a file), and gives true when it
  // changed them, `json` then holding the asset's fields as they are now. The editor takes that as it takes an edit in
  // the table: undone, redone and saved with the rest. EditorImage shows a picture made in code. Unset for the table
  // alone.
  std::function<bool(const std::string& path, std::string& json)> editor;
  // A live 3D viewport its editor shows of an asset as its fields are now, with Play, Pause, Restart and Loop
  // (AssetViewportDecl): beside the fields, or as the Viewport tab of an asset with a graph, in place of the 2D preview.
  // None for no viewport.
  std::optional<AssetViewportDecl> viewport;
  // Game events the game fires for one asset of the type, by full name ("tip.star_leaf_pressed"), each declared with
  // Namespace::Event and its Args naming the asset's path first. With any, each asset has a node graph of its own,
  // edited in its editor and kept beside its file (Leaf.graph.json, compiled to Leaf.graph.lua and run with the mod),
  // which starts with a node of each event that can't be deleted and runs when the game fires it for that asset.
  std::vector<AssetEventDecl> events;
  // Events made from an asset's own fields, beside `events`: given the asset's fields as JSON (as `check` gets them),
  // the event nodes its graph has for them (a shop catalog's "OnBought_Romance" for each of its actions, each matching
  // its action's name). They follow the fields: a node appears for a new one and goes (with what's linked to it kept as
  // a ghost, Blueprint style) when its field goes. A type with these has a graph even without `events`. On the UI thread.
  std::function<std::vector<AssetEventDecl>(const std::string& json)> asset_events;
};

// Declares one of the game's asset types. Before rexfabric::Start, after the types its fields name; false (logged)
// for a bad path, a taken path or kind, a field of a type that isn't there, a condition on a field that isn't declared
// before it, or a viewport with neither a backing nor a build.
bool DeclareAssetType(AssetTypeDecl decl);

// Every asset of a type: the game's own in their order, then the loaded mods', by mod load order, then by path. Any
// thread.
std::vector<GameAsset> GameAssets(std::string_view type);
// One asset of any game asset type, the game's or a loaded mod's; none for a path that isn't one. Any thread.
std::optional<GameAsset> FindGameAsset(std::string_view path);
// Goes up whenever a type's assets are loaded again (a mod turned on or off, a file changed), for a game keeping what
// it made of them. Any thread.
uint64_t GameAssetsRevision(std::string_view type);

// A picture a game makes in code, for its asset type's own editor (AssetTypeDecl::editor) to draw with ImGui (a palette
// over its pattern): the UI's texture (an ImTextureID) of it, kept under `key`, a name of the game's own for it
// ("TiP/PinataPalette:MyMod/Red:preview", the asset's path in it so two editors don't share one). It's made again from
// `rgba` (RGBA8, rows top first, tightly packed) whenever `version` isn't the one it was last made with, so a game gives
// the same version until its pixels change; `rgba` is read only then. 0 when it can't be made (no UI pictures, a size
// the pixels don't fill) and outside an editor. One not asked for in a while is freed. On the UI thread, inside an
// editor.
uint64_t EditorImage(std::string_view key, uint64_t version, uint32_t width, uint32_t height, std::span<const uint8_t> rgba);

// A kind of map edit the game offers beside RexFabric's own Spawn: a model swapped, an actor removed. Its functions run
// on the thread RexFabric ticks on, the game's, and may throw to refuse, with the reason as their message.
struct MapEditKindDecl {
  // Under a path of the game's own ("TiP/ReplaceModel").
  std::string path;
  std::string help;
  // Whether an edit of it has a place in the world (a transform).
  bool transform = false;
  std::vector<PropertyDecl> properties;
  // Makes the edit in `world` as its values say; a handle of the game's to undo it with, 0 for nothing to undo.
  std::function<uint64_t(std::string_view world, const Transform& at, const AvatarValues& values)> apply;
  // Undoes an edit apply made, the world still there (when the world went first, the game's own leaving undid it).
  std::function<void(uint64_t handle)> undo;
  // Moves what apply made to a new place, when only its place changed (an editor's gizmo): false, or none, and it's
  // undone and made again instead.
  std::function<bool(uint64_t handle, const Transform& at)> move;
};

// Declares one of the game's kinds of map edit. Before rexfabric::Start, after the types its properties name; false
// (logged) for a bad path, a taken one, a property of a type that isn't there, or no apply.
bool DeclareMapEditKind(MapEditKindDecl decl);

// A value a material graph's pin carries: 1 to 4 floats, named as the graph's editor shows it ("Base Colour"). In the
// code a graph makes, an input of the Material node is the MaterialResult field of its name in lower case with
// underscores for spaces (base_colour).
struct MaterialPinDecl {
  std::string name;
  uint8_t width = 3;
  std::array<float, 4> initial{};
  std::string help;
};

// How a graph material's surface is lit (docs/materials.md, Shading models): the game's to say and to draw. Its
// inputs are the Material node's inputs it reads; the node greys out the rest.
struct ShadingModelDecl {
  std::string name;
  std::string help;
  std::vector<std::string> inputs;
};

// A node of the game's for its material graphs: one of its scene's values, each output an HLSL expression of the
// context `c` the game's shader gives the code (c.vertex_colour).
struct MaterialNodeDecl {
  std::string id;
  std::string title;
  // "Scene": slashes nest the add menu.
  std::string category;
  std::string help;
  std::vector<MaterialPinDecl> outputs;
  // One for each output.
  std::vector<std::string> code;
};

// A texture a graph material samples: a mod's Texture asset by its path, or one of the game's textures by its name.
struct MaterialTextureUse {
  std::string texture;
  bool game = false;
  bool cube = false;  // sampled by a direction, as a cube map
  bool operator==(const MaterialTextureUse&) const = default;
};

// A named value of a graph material (a Parameter node's), that what's drawn with the material may change without
// compiling it again: 1 to 4 floats.
struct MaterialParameterUse {
  std::string name;
  uint8_t width = 1;
  std::array<float, 4> value{};
};

// A graph material compiled (docs/materials.md, The code): HLSL defining the struct MaterialResult, with a field
// for each of its type's graph inputs, and `MaterialResult EvaluateMaterial(MaterialContext c)` filling it from the
// graph. The code samples texture n of `textures` with MaterialSample2D(c, n, uv) or MaterialSampleCube(c, n, dir) and
// reads parameter n of `parameters` with MaterialParameter(c, n), all of which the game's shader defines, as it does
// MaterialContext. A graph with problems has its problems and no code.
struct MaterialProgram {
  std::string shading_model;
  std::string code;
  std::vector<MaterialTextureUse> textures;
  std::vector<MaterialParameterUse> parameters;
  // The Material node's inputs the graph links, by name; the others are their initial values.
  std::vector<std::string> linked;
  std::vector<std::string> problems;
  // Of `code`, for the game keeping the shader it compiled from it.
  uint64_t hash = 0;
};

// A kind of material the game offers (docs/content_and_classes.md, Materials): what a material of it sets on what the
// game draws, as properties, and how the material editor previews one. A mod's material asset is of one of these
// types and gives its properties values; whatever the game draws with it (a model) reads them with FindMaterial.
struct MaterialTypeDecl {
  // Under a path of the game's own ("TiP/ModelMaterial").
  std::string path;
  std::string help;
  std::vector<PropertyDecl> properties;
  // The preview: one of the game's components shown alone in the editor's viewport, with these values, and the
  // material's path in its property `preview_material`. `preview_choice` names a property of the component the editor
  // lets its user pick (the model it's shown on), empty for none.
  std::string preview_component;
  AvatarValues preview_values;
  std::string preview_material;
  std::string preview_choice;

  // A graph material type (docs/materials.md): its materials are node graphs ending in a Material node, whose inputs
  // are these, lit by one of these shading models. The type's properties are the Material node's details (a blend
  // mode, two sided). The game compiles the code a graph makes (MaterialProgram) into its own shader. Empty for a type
  // whose materials only give its properties values.
  std::vector<MaterialPinDecl> graph_inputs;
  std::vector<ShadingModelDecl> shading_models;
  // The game's own nodes for its graphs: values of its scene (the vertex colour, the sun).
  std::vector<MaterialNodeDecl> graph_nodes;
  // The data table whose rows name the game's textures, which a graph's texture nodes may sample as well as the mods'
  // Texture assets ("TiP/Textures"); empty for the mods' alone.
  std::string game_textures;
  // What the game finds wrong with a graph's code once it has it (its shader compiler's errors), which the editor shows
  // with its problems; empty while it's fine or still being worked on. Any thread, and often.
  std::function<std::vector<std::string>(const MaterialProgram& program)> check_program;
};

// Declares one of the game's material types. Before rexfabric::Start, after the types its properties name; false
// (logged) for a bad path, a taken one, or a property of a type that isn't there.
bool DeclareMaterialType(MaterialTypeDecl decl);

// A material as it is now: its type, each single property's value (the material's, or its type's default), each map
// property's entries by key, and a version that goes up with any change (a save, an editor's unsaved change), for a
// game keeping what it made of it. A graph material has its graph compiled.
struct MaterialView {
  std::string type;
  AvatarValues values;
  std::map<std::string, std::map<std::string, PropertyValue>> maps;
  uint64_t version = 0;
  std::shared_ptr<const MaterialProgram> program;
};

// A loaded mod's material ("MyMod/Materials/Gold"), as its editor has it while one is open; none for a path that isn't
// one. Any thread.
std::optional<MaterialView> FindMaterial(std::string_view path);

// The game's picture of the world it's in, for editing it in place (docs/content_and_classes.md, Editing in the
// world): the camera it's drawn with, so the map edit editor draws its markers and gizmos over the picture, and hit
// tests of rays into the world, so it can place things on what's under the pointer.
struct WorldCamera {
  std::array<float, 16> view{};        // row vectors, as ViewportImage's, only turning and moving
  std::array<float, 16> projection{};  // onto the whole of the game's picture
  std::array<double, 3> eye{};
};

// A ray into the world, from one point toward another, and what it hit first: none where it hit nothing on its way.
struct WorldRay {
  std::array<double, 3> from{};
  std::array<double, 3> to{};
  bool operator==(const WorldRay&) const = default;
};
struct WorldHit {
  WorldRay ray;
  std::optional<std::array<double, 3>> point;
};

struct WorldViewDecl {
  // The camera of the picture shown now; false while no world is shown. Any thread.
  std::function<bool(WorldCamera& camera)> camera;
  // The rays the editor wants hit tested, replacing the ones before; the game tests them on its own time. Any thread.
  std::function<void(std::vector<WorldRay> rays)> aim;
  // What the rays last tested hit, each with its ray. Any thread.
  std::function<std::vector<WorldHit>()> hits;
};

// Registers the game's view of the world. Before rexfabric::Start; without one, map edits are edited by their values.
void SetWorldView(WorldViewDecl decl);

// A mod's texture asset's picture (docs/content_and_classes.md, Textures): RGBA8, rows top first, tightly packed.
struct TextureImage {
  std::vector<uint8_t> rgba;
  uint32_t width = 0;
  uint32_t height = 0;
  // Changes when the picture's file does, for a game keeping what it made of it.
  uint64_t version = 0;
};

// The picture of a loaded mod's texture asset ("MyMod/Pictures/Logo"), read from its file; none, with `why`, for a
// path that isn't one or a picture that can't be read. Any thread.
std::optional<TextureImage> LoadTextureImage(std::string_view path, std::string* why = nullptr);
// The version LoadTextureImage would give the picture now, without reading it: for a game checking whether what it made
// of one is out of date. None, with `why`, where LoadTextureImage finds no picture to read. Any thread.
std::optional<uint64_t> TextureImageVersion(std::string_view path, std::string* why = nullptr);

// The Avatar editor's viewport (docs/content_and_classes.md, The viewport): one avatar of the class being edited,
// alone, drawn by the game and shown by the editor, which draws the components' gizmos over it. What's asked for: the
// class as the editor has it now, saved or not, its components, and how the editor's camera looks at it.
struct ViewportView {
  std::string class_path;
  std::string game_class;  // its nearest game class ("TiP/Signpost")
  std::string backing;     // the backing it has, its own or inherited
  AvatarValues values;     // every property's value, as the editor has them
  double yaw = 0.0;        // degrees the camera has gone round the avatar's up axis
  double pitch = 0.0;      // degrees the camera is above it
  double zoom = 1.0;       // 1 for the game's framing of the avatar and its components, more to come nearer
  uint32_t width = 0;      // the viewport's size, in pixels
  uint32_t height = 0;
  std::vector<ViewportPart> parts;  // every component with its place: the inherited first, each tree in order
  bool playing = true;     // its time runs (an emitter plays); paused, everything holds still
  bool looping = true;     // what plays starts again once it's done; off, it plays once and stays done (Loop)
  uint32_t replays = 0;    // goes up each time the editor asks for everything to start again
  bool hold_framing = false;  // a component is being dragged: the framing stays as it is, so the camera keeps still
  // A game asset type's viewport (AssetViewportDecl): the asset's path, `class_path` then empty, the backing the type's
  // and the values and parts its scene's. Empty for a class's viewport.
  std::string asset;
};

// What the game drew last for a viewport.
struct ViewportImage {
  uint64_t texture = 0;  // the UI's texture of it (an ImTextureID); 0 while there's none
  std::array<float, 2> uv_max{1.0f, 1.0f};  // the part of the texture it fills, from the top left
  // How a point in the avatar's space lands on it, for the gizmos: clip = point * view * projection with row vectors
  // (the layout ImGuizmo takes), x and y from -1 to 1 across the image, y up. The view only turns and moves (any scale
  // goes in the projection), as the gizmos are sized by it. `camera` false when the game can't say.
  bool camera = false;
  bool orthographic = false;
  std::array<float, 16> view{};
  std::array<float, 16> projection{};
  std::string message;  // what to show in its place while there's no image, as why
  // How long what it shows has played since it last started (ViewportView::replays), in seconds, for the time readout
  // of a viewport with one (an asset's); none when the game doesn't say, and the editor counts it itself.
  std::optional<double> time;
};

// A still picture of a viewport, for the content browser's thumbnails (docs/content_and_classes.md, Thumbnails):
// RGBA8, rows top first, tightly packed, its alpha straight (not premultiplied), what the stage left clear see through.
// No pixels, with `message` saying why, when the game couldn't make one.
struct ViewportCapture {
  std::vector<uint8_t> rgba;
  uint32_t width = 0;
  uint32_t height = 0;
  std::string message;
};

// What draws the viewports, each known by a number of its own. `show` comes from the UI thread every frame a viewport
// is seen and gives what was drawn last, the game drawing on its own time; `close` when the viewport goes. Either may
// be called on any thread, and neither should wait for the game.
//
// `capture`, which a game may leave unset, makes one picture of `view` on a stage of its own, under a viewport number
// of its own: as `show` would draw it at the view's width and height, after its time has run `seconds` of the stage's
// clock from the start (an effect played that far, so its particles are out; 0 for as soon as it shows). It's called on
// the UI thread and returns at once; the game draws on its own time and calls `done` once, on any thread, a few frames
// later, with the picture or with why there's none. `close` with the capture's number means it's no longer wanted:
// the game may stop, and a `done` after it is ignored. RexFabric asks for one capture at a time.
struct ViewportRenderer {
  std::function<ViewportImage(uint64_t viewport, const ViewportView& view)> show;
  std::function<void(uint64_t viewport)> close;
  std::function<void(uint64_t viewport, const ViewportView& view, double seconds, std::function<void(ViewportCapture)> done)> capture;
};

// Registers the game's viewport renderer, at any time, a later one replacing it. Until one is, the viewport says so and
// shows the components over a plain background.
void SetViewportRenderer(ViewportRenderer renderer);

// Whether a name works as an asset or folder name: letters, digits and _, starting with a letter, at most 64. With
// `why`, the reason it doesn't.
bool ValidName(std::string_view name, std::string* why = nullptr);

}  // namespace rexfabric::content
