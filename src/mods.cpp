#include "mods.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include "log.h"

namespace rexfabric {
namespace {

#if defined(_WIN32)
constexpr std::array<std::string_view, 2> kNativeExtensions = {".asi", ".dll"};
#elif defined(__APPLE__)
constexpr std::array<std::string_view, 2> kNativeExtensions = {".asi", ".dylib"};
#else
constexpr std::array<std::string_view, 2> kNativeExtensions = {".asi", ".so"};
#endif

constexpr const char* kOrderFile = "mods.json";

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

std::vector<std::filesystem::path> FilesWith(const std::filesystem::path& dir, auto&& accept) {
  std::vector<std::filesystem::path> files;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(dir, error)) {
    if (entry.is_regular_file(error) && accept(Lower(entry.path().extension().string()))) files.push_back(entry.path());
  }
  return files;
}

nlohmann::json ReadJson(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return {};
  return nlohmann::json::parse(in, nullptr, false);
}

std::string JsonString(const nlohmann::json& json, const char* key) {
  const auto it = json.find(key);
  return it != json.end() && it->is_string() ? it->get<std::string>() : std::string();
}

void ReadMetadata(ModInfo& mod) {
  const nlohmann::json manifest = ReadJson(mod.dir / "manifest.json");
  if (manifest.is_object()) {
    mod.name = JsonString(manifest, "name");
    mod.description = JsonString(manifest, "description");
    mod.version = JsonString(manifest, "version_number");
  }
  std::error_code error;
  const std::filesystem::path toml_file = mod.dir / "mod.toml";
  if (std::filesystem::is_regular_file(toml_file, error)) {
    try {
      const toml::table table = toml::parse_file(toml_file.string());
      if (auto name = table["name"].value<std::string>(); name && !name->empty()) mod.name = *name;
      if (auto description = table["description"].value<std::string>()) mod.description = *description;
      if (auto version = table["version"].value<std::string>()) mod.version = *version;
      if (auto author = table["author"].value<std::string>()) mod.author = *author;
    } catch (const toml::parse_error& parse_error) {
      RF_WARN("{}: unreadable mod.toml: {}", mod.folder, parse_error.description());
    }
  }
  if (mod.name.empty()) mod.name = mod.folder;
}

std::vector<std::string> StringList(const nlohmann::json& json, const char* key) {
  std::vector<std::string> names;
  const auto it = json.is_object() ? json.find(key) : json.end();
  if (it == json.end() || !it->is_array()) return names;
  for (const auto& name : *it) {
    if (name.is_string()) names.push_back(name.get<std::string>());
  }
  return names;
}

bool Listed(const std::vector<std::string>& names, std::string_view folder) {
  return std::any_of(names.begin(), names.end(), [&](const std::string& name) { return SameModName(name, folder); });
}

}  // namespace

bool SameModName(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

void ModList::Scan() {
  std::vector<ModInfo> found;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
    const std::string folder = entry.path().filename().string();
    // Dot folders are tool state, and a half finished install is not a mod yet.
    if (!entry.is_directory(error) || folder.starts_with(".") || folder.ends_with(".installing")) continue;
    ModInfo mod{folder, entry.path()};
    ReadMetadata(mod);
    found.push_back(std::move(mod));
  }

  const nlohmann::json order = ReadJson(root_ / kOrderFile);
  const std::vector<std::string> load_order = StringList(order, "load_order");
  const std::vector<std::string> disabled = StringList(order, "disabled");

  mods_.clear();
  for (const std::string& name : load_order) {
    auto it = std::find_if(found.begin(), found.end(), [&](const ModInfo& mod) { return SameModName(mod.folder, name); });
    if (it == found.end() || Listed(disabled, it->folder)) continue;
    mods_.push_back(std::move(*it));
    found.erase(it);
  }
  // What the file doesn't place: new folders join the load order by name, the rest stay off.
  std::sort(found.begin(), found.end(), [](const ModInfo& a, const ModInfo& b) { return Lower(a.folder) < Lower(b.folder); });
  std::vector<ModInfo> off;
  for (ModInfo& mod : found) {
    mod.enabled = !Listed(disabled, mod.folder);
    (mod.enabled ? mods_ : off).push_back(std::move(mod));
  }
  for (ModInfo& mod : off) mods_.push_back(std::move(mod));
  SortDisabled();
}

bool ModList::Save() const {
  nlohmann::json order = {{"load_order", nlohmann::json::array()}, {"disabled", nlohmann::json::array()}};
  for (const ModInfo& mod : mods_) order[mod.enabled ? "load_order" : "disabled"].push_back(mod.folder);
  std::error_code error;
  std::filesystem::create_directories(root_, error);
  std::ofstream out(root_ / kOrderFile, std::ios::binary | std::ios::trunc);
  out << order.dump(2) << '\n';
  if (!out) RF_ERROR("can't write {}", (root_ / kOrderFile).string());
  return static_cast<bool>(out);
}

size_t ModList::EnabledCount() const { return static_cast<size_t>(std::count_if(mods_.begin(), mods_.end(), [](const ModInfo& mod) { return mod.enabled; })); }

const ModInfo* ModList::Find(std::string_view folder) const {
  auto it = std::find_if(mods_.begin(), mods_.end(), [&](const ModInfo& mod) { return SameModName(mod.folder, folder); });
  return it != mods_.end() ? &*it : nullptr;
}

void ModList::SetEnabled(size_t index, bool enabled) {
  if (index >= mods_.size() || mods_[index].enabled == enabled) return;
  ModInfo mod = std::move(mods_[index]);
  mods_.erase(mods_.begin() + static_cast<ptrdiff_t>(index));
  mod.enabled = enabled;
  mods_.insert(mods_.begin() + static_cast<ptrdiff_t>(EnabledCount()), std::move(mod));
  SortDisabled();
  Save();
}

void ModList::Move(size_t index, int delta) {
  const size_t enabled = EnabledCount();
  const ptrdiff_t target = static_cast<ptrdiff_t>(index) + delta;
  if (index >= enabled || target < 0 || target >= static_cast<ptrdiff_t>(enabled)) return;
  std::swap(mods_[index], mods_[static_cast<size_t>(target)]);
  Save();
}

void ModList::SortDisabled() {
  const auto first_disabled = mods_.begin() + static_cast<ptrdiff_t>(EnabledCount());
  std::sort(first_disabled, mods_.end(), [](const ModInfo& a, const ModInfo& b) { return Lower(a.name) < Lower(b.name); });
}

std::vector<std::filesystem::path> NativePlugins(const std::filesystem::path& dir) {
  const auto native = [](const std::string& extension) { return std::find(kNativeExtensions.begin(), kNativeExtensions.end(), extension) != kNativeExtensions.end(); };
  std::vector<std::filesystem::path> plugins = FilesWith(dir, native);
  std::vector<std::filesystem::path> nested = FilesWith(dir / "plugins", native);
  plugins.insert(plugins.end(), nested.begin(), nested.end());
  std::sort(plugins.begin(), plugins.end());
  return plugins;
}

std::vector<std::filesystem::path> LuaScripts(const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> scripts = FilesWith(dir / "scripts", [](const std::string& extension) { return extension == ".lua"; });
  std::sort(scripts.begin(), scripts.end());
  return scripts;
}

}  // namespace rexfabric
