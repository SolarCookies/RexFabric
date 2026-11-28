#include "game_assets.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>

#include "../log.h"
#include "assets.h"
#include "localisation.h"
#include "types.h"

namespace rexfabric::content {
namespace {

constexpr std::string_view kObject = "Object";
constexpr std::string_view kTextureClass = "Texture";
constexpr std::string_view kAssetKey = "asset";

std::string Lower(std::string_view text) {
  std::string lower(text);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower;
}

bool Same(std::string_view a, std::string_view b) { return Lower(a) == Lower(b); }

bool ValidPath(std::string_view path, std::string* why) {
  size_t start = 0;
  for (size_t slash = 0; slash != std::string_view::npos; start = slash + 1) {
    slash = path.find('/', start);
    if (!ValidName(path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start), why)) return false;
  }
  return true;
}

// An object's value under a key, ignoring case; null for none.
const nlohmann::json* FindKey(const nlohmann::json& object, std::string_view key) {
  if (!object.is_object()) return nullptr;
  for (const auto& [name, value] : object.items()) {
    if (Same(name, key)) return &value;
  }
  return nullptr;
}

nlohmann::json* FindKey(nlohmann::json& object, std::string_view key) { return const_cast<nlohmann::json*>(FindKey(std::as_const(object), key)); }

// A value as a condition compares it: an enum's or a string's text, a bool's "true" or "false", a number's digits.
std::string ConditionText(const nlohmann::json& value) {
  if (value.is_string()) return value.get<std::string>();
  if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
  if (value.is_number_integer()) return std::to_string(value.get<int64_t>());
  return value.dump();
}

bool Holds(const AssetField& field, const nlohmann::json& value) {
  const std::string text = ConditionText(value);
  return std::any_of(field.when_values.begin(), field.when_values.end(), [&text](const std::string& wanted) { return Same(wanted, text); });
}

// Whether two fields of one name may both be declared: each shown only while the same field holds values the other's
// condition never does.
bool Exclusive(const AssetField& a, const AssetField& b) {
  if (a.when_field.empty() || !Same(a.when_field, b.when_field)) return false;
  return std::none_of(a.when_values.begin(), a.when_values.end(), [&b](const std::string& value) { return std::any_of(b.when_values.begin(), b.when_values.end(), [&value](const std::string& other) { return Same(value, other); }); });
}

// A mod's asset as it loaded: its fitted values, untranslated, and its mod's place in the load order.
struct Loaded {
  std::string path;
  std::string mod;
  nlohmann::json values;
  size_t mod_order = 0;
};

struct Registry {
  std::mutex mutex;
  std::vector<std::shared_ptr<const AssetType>> types;
  std::map<std::string, std::vector<Loaded>> loaded;  // by the type's lower case path, sorted as GameAssets lists them
  std::map<std::string, uint64_t> revisions;          // the same
};

Registry& Get() {
  static auto* registry = new Registry();
  return *registry;
}

// An asset as the game reads it: its texts translated (but a game's own, which aren't a mod's texts) and its single
// fields as values.
GameAsset Present(const AssetType& type, std::string path, std::string mod, nlohmann::json values) {
  if (!mod.empty()) {
    const TypeLookup& types = LoadedTypes();
    ForEachShownValue(type, values, [&](const AssetField& field, nlohmann::json& value) { TranslateTexts(types, field.property.type, value, TextKey(path, field.name())); });
  }
  GameAsset asset{.path = std::move(path), .mod = std::move(mod)};
  for (const AssetField* field : ShownFields(type, values)) {
    if (field->property.type.container != Container::kSingle || field->property.type.type == PropertyType::kStruct) continue;
    if (const nlohmann::json* value = FindKey(values, field->name())) asset.values[field->name()] = ToPropertyValue(*value);
  }
  asset.json = values.dump();
  return asset;
}

// Why a field's declaration doesn't work beside the fields declared before it, or nothing.
std::string FieldProblem(const std::vector<AssetField>& before, const AssetField& field) {
  if (!field.when_field.empty()) {
    if (std::none_of(before.begin(), before.end(), [&field](const AssetField& earlier) { return Same(earlier.name(), field.when_field); })) return "its condition names " + field.when_field + ", which isn't a field declared before it";
    if (field.when_values.empty()) return "its condition on " + field.when_field + " names no values";
  }
  for (const AssetField& earlier : before) {
    if (Same(earlier.name(), field.name()) && !Exclusive(earlier, field)) return "it shares its name with another field shown at the same time";
  }
  return {};
}

// RexFabric's thumbnail field, last in the table, empty by default.
AssetField ThumbnailField() {
  Property property{.name = std::string(kThumbnailField), .type = {.type = PropertyType::kAsset, .of = std::string(kTextureClass)}, .value = nullptr, .help = "A picture the content browser shows for it in place of the one it makes; none for that one"};
  return {.property = std::move(property), .label = "Thumbnail", .own = true};
}

}  // namespace

bool DeclareAssetType(AssetTypeDecl decl) {
  std::string why;
  const auto fail = [&decl](const std::string& reason) {
    RF_ERROR("content: the game's asset type {}: {}", decl.path, reason);
    return false;
  };
  if (!ValidPath(decl.path, &why)) return fail(why);
  if (!ValidName(decl.kind, &why)) return fail("its kind: " + why);
  if (IsBuiltInKind(decl.kind)) return fail("its kind \"" + decl.kind + "\" is one of RexFabric's own");
  if (decl.viewport && decl.viewport->backing.empty() && !decl.viewport->build) return fail("its viewport has neither a backing nor a build, so it would show nothing");
  auto type = std::make_shared<AssetType>(AssetType{.path = decl.path, .kind = decl.kind, .label = decl.label.empty() ? std::string(NameOf(decl.path)) : decl.label, .category = decl.category, .help = decl.help, .create_with = decl.create_with, .name_field = decl.name_field, .check = decl.check, .warn = decl.warn, .colour = decl.colour, .preview = decl.preview, .editor = decl.editor, .viewport = decl.viewport, .events = decl.events, .asset_events = decl.asset_events});
  const auto anything = [](std::string_view, std::string*) { return true; };
  for (AssetFieldDecl& declared : decl.fields) {
    std::vector<Property> one{{.name = declared.name, .type = declared.type, .value = ToJson(declared.value), .help = declared.help}};
    // A container with no default starts empty rather than none, as files keep it.
    if (one[0].type.container != Container::kSingle && one[0].value.is_null()) one[0].value = nlohmann::json::array();
    if (!CheckDeclared(one, LoadedTypes(), anything, &why)) return fail("its field " + declared.name + ": " + why);
    AssetField field{.property = std::move(one[0]), .label = std::move(declared.label), .when_field = std::move(declared.when_field), .when_values = std::move(declared.when_values), .custom = declared.custom};
    if (const std::string problem = FieldProblem(type->fields, field); !problem.empty()) return fail("its field " + field.name() + ": " + problem);
    type->fields.push_back(std::move(field));
  }
  const auto has_field = [&type](std::string_view name) { return std::any_of(type->fields.begin(), type->fields.end(), [name](const AssetField& field) { return Same(field.name(), name); }); };
  if (!has_field(kThumbnailField)) type->fields.push_back(ThumbnailField());
  for (const std::string& name : type->create_with) {
    if (!has_field(name)) return fail("it's created with " + name + ", which isn't one of its fields");
  }
  if (!type->name_field.empty() && !has_field(type->name_field)) return fail("its name field " + type->name_field + " isn't one of its fields");
  for (GameAsset& asset : decl.built_in) {
    if (!ValidPath(asset.path, &why)) return fail("its asset " + asset.path + ": " + why);
    const nlohmann::json json = nlohmann::json::parse(asset.json.empty() ? std::string("{}") : asset.json, nullptr, false);
    if (!json.is_object()) return fail("its asset " + asset.path + " isn't a JSON object");
    std::vector<std::string> problems;
    nlohmann::json values = FittedValues(LoadedTypes(), *type, json, &problems);
    if (!problems.empty()) return fail("its asset " + asset.path + ": " + problems.front());
    type->built_in.push_back(Present(*type, asset.path, {}, std::move(values)));
  }
  {
    Registry& registry = Get();
    std::lock_guard lock(registry.mutex);
    for (const auto& known : registry.types) {
      if (Same(known->path, type->path)) return fail("it's declared twice");
      if (Same(known->kind, type->kind)) return fail("its kind \"" + type->kind + "\" is " + known->path + "'s");
    }
  }
  // Its class, so an asset property of it picks one of its assets.
  if (!DeclareClass({.path = type->path, .parent = std::string(kObject), .help = type->help})) return false;
  Registry& registry = Get();
  std::lock_guard lock(registry.mutex);
  registry.types.push_back(std::move(type));
  return true;
}

std::shared_ptr<const AssetType> FindAssetType(std::string_view path) {
  Registry& registry = Get();
  std::lock_guard lock(registry.mutex);
  const auto found = std::find_if(registry.types.begin(), registry.types.end(), [path](const auto& known) { return Same(known->path, path); });
  return found != registry.types.end() ? *found : nullptr;
}

std::shared_ptr<const AssetType> FindAssetTypeByKind(std::string_view kind) {
  Registry& registry = Get();
  std::lock_guard lock(registry.mutex);
  const auto found = std::find_if(registry.types.begin(), registry.types.end(), [kind](const auto& known) { return known->kind == kind; });
  return found != registry.types.end() ? *found : nullptr;
}

std::vector<std::shared_ptr<const AssetType>> AssetTypes() {
  Registry& registry = Get();
  std::lock_guard lock(registry.mutex);
  return registry.types;
}

std::string AssetEventTitle(std::string_view event) {
  for (const std::shared_ptr<const AssetType>& type : AssetTypes()) {
    for (const AssetEventDecl& declared : type->events) {
      if (declared.event == event && !declared.title.empty()) return declared.title;
    }
  }
  return {};
}

std::vector<std::string> BuiltInAssetPaths(std::string_view type) {
  std::vector<std::string> paths;
  if (const std::shared_ptr<const AssetType> found = FindAssetType(type)) {
    for (const GameAsset& asset : found->built_in) paths.push_back(asset.path);
  }
  return paths;
}

std::vector<const AssetField*> ShownFields(const AssetType& type, const nlohmann::json& values) {
  std::vector<const AssetField*> shown;
  const auto shown_named = [&shown](std::string_view name) -> const AssetField* {
    const auto found = std::find_if(shown.begin(), shown.end(), [name](const AssetField* field) { return Same(field->name(), name); });
    return found != shown.end() ? *found : nullptr;
  };
  for (const AssetField& field : type.fields) {
    if (shown_named(field.name())) continue;
    if (!field.when_field.empty()) {
      const AssetField* condition = shown_named(field.when_field);
      if (!condition) continue;
      const nlohmann::json* set = FindKey(values, condition->name());
      if (!Holds(field, set ? *set : condition->property.value)) continue;
    }
    shown.push_back(&field);
  }
  return shown;
}

nlohmann::json FittedValues(const TypeLookup& types, const AssetType& type, const nlohmann::json& values, std::vector<std::string>* problems) {
  nlohmann::json fitted = nlohmann::json::object();
  // The conditions read the fitted values, so a field shows by what its condition's field fitted to.
  for (const AssetField* field : ShownFields(type, values)) {
    const nlohmann::json* set = FindKey(values, field->name());
    nlohmann::json value;
    std::string why;
    if (set && FitValue(types, field->property.type, *set, &value, &why)) {
      fitted[field->name()] = std::move(value);
      continue;
    }
    if (set && problems) problems->push_back(field->Label() + ": " + why);
    fitted[field->name()] = field->property.value;
  }
  return fitted;
}

void ForEachShownValue(const AssetType& type, nlohmann::json& values, const std::function<void(const AssetField& field, nlohmann::json& value)>& visit) {
  for (const AssetField* field : ShownFields(type, values)) {
    if (nlohmann::json* value = FindKey(values, field->name())) visit(*field, *value);
  }
}

std::optional<GameAssetFile> ReadGameAsset(const std::filesystem::path& file, std::string* error) {
  std::ifstream in(file, std::ios::binary);
  nlohmann::json json = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
  if (!json.is_object()) {
    if (error) *error = file.filename().string() + " isn't a JSON object";
    return std::nullopt;
  }
  const std::string kind = json.value(std::string(kAssetKey), std::string());
  std::shared_ptr<const AssetType> type = FindAssetTypeByKind(kind);
  if (!type) {
    if (error) *error = file.filename().string() + " is a \"" + kind + "\", which the game doesn't declare";
    return std::nullopt;
  }
  json.erase(std::string(kAssetKey));
  return GameAssetFile{std::move(type), std::move(json)};
}

bool WriteGameAsset(const std::filesystem::path& file, const AssetType& type, const nlohmann::json& json, std::string* error) {
  nlohmann::ordered_json out{{std::string(kAssetKey), type.kind}};
  for (const AssetField* field : ShownFields(type, json)) {
    const nlohmann::json* value = FindKey(json, field->name());
    if (value && !(field->own && value->is_null())) out[field->name()] = *value;
  }
  const auto known = [&type](std::string_view key) { return Same(key, kAssetKey) || std::any_of(type.fields.begin(), type.fields.end(), [key](const AssetField& field) { return Same(field.name(), key); }); };
  for (const auto& [key, value] : json.items()) {
    if (!known(key)) out[key] = value;
  }
  std::error_code code;
  std::filesystem::create_directories(file.parent_path(), code);
  std::ofstream stream(file, std::ios::binary | std::ios::trunc);
  stream << out.dump(2) << "\n";
  if (!stream && error) *error = "couldn't write " + file.string();
  return static_cast<bool>(stream);
}

void LoadGameAssets(const std::vector<ModInfo>& mods) {
  const std::vector<std::shared_ptr<const AssetType>> types = AssetTypes();
  if (types.empty()) return;
  std::map<std::string, std::vector<Loaded>> loaded;
  for (size_t order = 0; order < mods.size(); ++order) {
    const ModInfo& mod = mods[order];
    for (const AssetInfo& info : ScanContent(mod).assets) {
      if (info.kind != AssetKind::kGame) continue;
      std::string error;
      std::optional<GameAssetFile> file = ReadGameAsset(info.file, &error);
      if (!file) {
        RF_WARN("content: [{}] {}: {}", mod.folder, info.path, error);
        continue;
      }
      std::vector<std::string> problems;
      nlohmann::json values = FittedValues(LoadedTypes(), *file->type, file->json, &problems);
      if (problems.empty() && file->type->check) problems = file->type->check(values.dump());
      if (!problems.empty()) {
        RF_WARN("content: [{}] {}: {}", mod.folder, info.path, problems.front());
        continue;
      }
      loaded[Lower(file->type->path)].push_back({.path = info.path, .mod = mod.folder, .values = std::move(values), .mod_order = order});
    }
  }
  for (auto& [type, assets] : loaded) {
    std::stable_sort(assets.begin(), assets.end(), [](const Loaded& a, const Loaded& b) { return a.mod_order != b.mod_order ? a.mod_order < b.mod_order : Lower(a.path) < Lower(b.path); });
  }
  Registry& registry = Get();
  std::lock_guard lock(registry.mutex);
  for (const auto& type : types) {
    const std::string key = Lower(type->path);
    const auto found = loaded.find(key);
    const size_t count = found != loaded.end() ? found->second.size() : 0;
    RF_INFO("content: {} {} assets from mods", count, type->label);
    registry.loaded[key] = found != loaded.end() ? std::move(found->second) : std::vector<Loaded>{};
    ++registry.revisions[key];
  }
}

std::vector<GameAsset> GameAssets(std::string_view type_path) {
  const std::shared_ptr<const AssetType> type = FindAssetType(type_path);
  if (!type) return {};
  std::vector<Loaded> mods;
  {
    Registry& registry = Get();
    std::lock_guard lock(registry.mutex);
    if (const auto found = registry.loaded.find(Lower(type->path)); found != registry.loaded.end()) mods = found->second;
  }
  // Outside the lock: a translation may read a string table.
  std::vector<GameAsset> assets = type->built_in;
  for (Loaded& asset : mods) assets.push_back(Present(*type, std::move(asset.path), std::move(asset.mod), std::move(asset.values)));
  return assets;
}

std::optional<GameAsset> FindGameAsset(std::string_view path) {
  for (const std::shared_ptr<const AssetType>& type : AssetTypes()) {
    for (const GameAsset& asset : type->built_in) {
      if (Same(asset.path, path)) return asset;
    }
    std::optional<Loaded> found;
    {
      Registry& registry = Get();
      std::lock_guard lock(registry.mutex);
      if (const auto assets = registry.loaded.find(Lower(type->path)); assets != registry.loaded.end()) {
        for (const Loaded& asset : assets->second) {
          if (Same(asset.path, path)) found = asset;
        }
      }
    }
    if (found) return Present(*type, std::move(found->path), std::move(found->mod), std::move(found->values));
  }
  return std::nullopt;
}

uint64_t GameAssetsRevision(std::string_view type) {
  Registry& registry = Get();
  std::lock_guard lock(registry.mutex);
  const auto found = registry.revisions.find(Lower(type));
  return found != registry.revisions.end() ? found->second : 0;
}

}  // namespace rexfabric::content
