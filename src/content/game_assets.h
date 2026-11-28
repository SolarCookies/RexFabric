// Game asset types (docs/content_and_classes.md, Game asset types): the kinds of asset a game declares
// (rexfabric/content.h, DeclareAssetType), the mods' assets of each as they loaded, and what the files, the editors,
// renames and translations need to know of them. RexFabric knows nothing of what a type means: a type is fields, some
// shown only while another field holds certain values, and a check the game runs.
//
// An asset of a game type is its file's JSON object as it is: its fields at the top level beside "asset", which names
// the type's kind. Keys no field of the type names are kept as they were.

#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <rexfabric/content.h>

#include "../mods.h"
#include "values.h"

namespace rexfabric::content {

// The field RexFabric adds to every game asset type that doesn't declare one of the name (docs/content_and_classes.md,
// Thumbnails): a Texture asset its tiles show in place of the picture made of it, none for that one.
inline constexpr std::string_view kThumbnailField = "thumbnail";

// A field of a game asset type: a property, as a struct's field is, with when it's shown.
struct AssetField {
  Property property;  // its default fitted to its type
  std::string label;
  std::string when_field;
  std::vector<std::string> when_values;
  bool custom = false;  // edited by the type's own editor alone
  bool own = false;     // RexFabric's (the thumbnail), not the game's: left out of a file while it's empty

  const std::string& name() const { return property.name; }
  const std::string& Label() const { return label.empty() ? property.name : label; }
};

struct AssetType {
  std::string path;
  std::string kind;
  std::string label;
  std::string category;
  std::string help;
  std::vector<AssetField> fields;
  std::vector<std::string> create_with;
  std::string name_field;
  std::function<std::vector<std::string>(const std::string& json)> check;
  std::function<std::vector<std::string>(const std::string& json)> warn;
  std::vector<GameAsset> built_in;
  std::array<float, 3> colour{};
  std::function<std::vector<PreviewLayer>(const std::string& json)> preview;
  std::function<bool(const std::string& path, std::string& json)> editor;
  std::optional<AssetViewportDecl> viewport;
  std::vector<AssetEventDecl> events;
  std::function<std::vector<AssetEventDecl>(const std::string& json)> asset_events;

  // Each asset has a graph of its own: the type fires events for its assets, declared or made from their fields.
  bool HasGraph() const { return !events.empty() || asset_events != nullptr; }
};

std::shared_ptr<const AssetType> FindAssetType(std::string_view path);
// The type whose files say `kind` under "asset"; null for none.
std::shared_ptr<const AssetType> FindAssetTypeByKind(std::string_view kind);
// Every game asset type, in the order they were declared.
std::vector<std::shared_ptr<const AssetType>> AssetTypes();
// The title an asset event's node has on an asset's graph: the first asset type naming the event gives it, empty for
// none (the event's own title then).
std::string AssetEventTitle(std::string_view event);
// The paths of the game's own assets of a type, in their order.
std::vector<std::string> BuiltInAssetPaths(std::string_view type);

// The fields shown for an asset holding `values`: for each name, the first of its fields whose condition holds, in the
// order they're declared. A condition reads the field it names as `values` has it, or its default.
std::vector<const AssetField*> ShownFields(const AssetType& type, const nlohmann::json& values);

// An asset's shown fields, each fitted to its type: its value, or the field's default where it has none or the value
// doesn't fit (with the field and why in `problems`). Keys of no shown field are left out.
nlohmann::json FittedValues(const TypeLookup& types, const AssetType& type, const nlohmann::json& values, std::vector<std::string>* problems = nullptr);

// Calls `visit` with each shown field's value and type, for renames and texts: the value as the asset has it.
void ForEachShownValue(const AssetType& type, nlohmann::json& values, const std::function<void(const AssetField& field, nlohmann::json& value)>& visit);

// A file of a game asset: its JSON object, and the type its "asset" names. None, with `error`, for a file that isn't
// one of a declared type.
struct GameAssetFile {
  std::shared_ptr<const AssetType> type;
  nlohmann::json json;
};
std::optional<GameAssetFile> ReadGameAsset(const std::filesystem::path& file, std::string* error);
// Writes the asset: "asset" first, then its shown fields in the order they're declared, then the keys of no field of
// its type as they were. A field hidden by its condition isn't written.
bool WriteGameAsset(const std::filesystem::path& file, const AssetType& type, const nlohmann::json& json, std::string* error);

// Reads the game assets of the enabled mods. With the classes, after the types.
void LoadGameAssets(const std::vector<ModInfo>& mods);

}  // namespace rexfabric::content
