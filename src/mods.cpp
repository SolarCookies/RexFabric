#include "mods.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace rexfabric {
namespace {

#if defined(_WIN32)
constexpr std::array<std::string_view, 2> kNativeExtensions = {".asi", ".dll"};
#elif defined(__APPLE__)
constexpr std::array<std::string_view, 2> kNativeExtensions = {".asi", ".dylib"};
#else
constexpr std::array<std::string_view, 2> kNativeExtensions = {".asi", ".so"};
#endif

bool IsNative(const std::filesystem::path& file) {
  const std::string extension = file.extension().string();
  return std::any_of(kNativeExtensions.begin(), kNativeExtensions.end(), [&](std::string_view known) { return extension == known; });
}

void CollectNative(const std::filesystem::path& dir, std::vector<std::filesystem::path>& out) {
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(dir, error)) {
    if (entry.is_regular_file(error) && IsNative(entry.path())) out.push_back(entry.path());
  }
}

}  // namespace

std::vector<ModFolder> FindMods(const std::filesystem::path& root) {
  std::vector<ModFolder> mods;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
    const std::string name = entry.path().filename().string();
    if (entry.is_directory(error) && !name.starts_with(".")) mods.push_back({name, entry.path()});
  }
  std::sort(mods.begin(), mods.end(), [](const ModFolder& a, const ModFolder& b) { return a.name < b.name; });
  return mods;
}

std::vector<std::filesystem::path> NativePlugins(const ModFolder& mod) {
  std::vector<std::filesystem::path> plugins;
  CollectNative(mod.dir, plugins);
  CollectNative(mod.dir / "plugins", plugins);
  std::sort(plugins.begin(), plugins.end());
  return plugins;
}

}  // namespace rexfabric
