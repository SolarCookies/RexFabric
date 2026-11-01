#include "menu_model.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include <rex/logging.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>
#include <rexfabric/rexfabric.h>

#include "../log.h"
#include "../platform.h"
#include "../lua/lua_scripts.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace rexfabric::ui {
namespace {

// Thunderstore icons kept around for paging back and forth.
constexpr size_t kMaxPackageIcons = 96;

std::vector<uint8_t> ReadFile(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  return in ? std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {}) : std::vector<uint8_t>{};
}

// Starts this program again with the same arguments.
bool Relaunch() {
#if defined(_WIN32)
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  std::wstring command = GetCommandLineW();
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return false;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
#elif defined(__linux__)
  std::ifstream in("/proc/self/cmdline", std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), {});
  std::vector<char*> args;
  for (size_t start = 0; start < text.size(); start = text.find('\0', start) + 1) args.push_back(text.data() + start);
  args.push_back(nullptr);
  if (fork() == 0) {
    execv("/proc/self/exe", args.data());
    _exit(1);
  }
  return true;
#else
  return false;
#endif
}

}  // namespace

MenuModel::MenuModel(std::filesystem::path mods_dir, const std::string& community, rex::ui::ImmediateDrawer* images) : mods_(mods_dir), images_(images) {
  if (!community.empty()) thunderstore_ = std::make_unique<net::Thunderstore>(community, mods_dir);
  mods_.Scan();
  seen_revision_ = ModsRevision();
}

MenuModel::~MenuModel() = default;

void MenuModel::Poll() {
  ++frame_;
  if (thunderstore_) {
    thunderstore_->Poll();
    for (net::Thunderstore::Failure& failure : thunderstore_->TakeFailed()) failures_.push_back(std::move(failure));
    if (!thunderstore_->TakeInstalled().empty()) {
      mods_.Save();
      NoteModsChanged();
      changed_ = true;
    }
  }
  // Something changed the mods since the last scan: an install, or the game.
  if (ModsRevision() != seen_revision_) {
    seen_revision_ = ModsRevision();
    mods_.Scan();
    mod_icons_.clear();
  }
}

void MenuModel::SetEnabled(size_t index, bool enabled) {
  mods_.SetEnabled(index, enabled);
  NoteModsChanged();
  seen_revision_ = ModsRevision();
  changed_ = true;
}

void MenuModel::Move(size_t index, int delta) {
  mods_.Move(index, delta);
  NoteModsChanged();
  seen_revision_ = ModsRevision();
  changed_ = true;
}

void MenuModel::Install(const net::Package& package) {
  if (thunderstore_) thunderstore_->RequestInstall(package);
}

bool MenuModel::Restart() {
  rexfabric::Stop();
  if (!Relaunch()) {
    RF_ERROR("couldn't restart; restart the game to apply mod changes");
    return false;
  }
  rex::FlushLogging();
  std::_Exit(0);
}

void MenuModel::ReloadScripts() {
  std::vector<ModInfo> enabled(mods_.mods().begin(), mods_.mods().begin() + static_cast<ptrdiff_t>(mods_.EnabledCount()));
  lua::Reload(enabled);
}

std::unique_ptr<rex::ui::ImmediateTexture> MenuModel::Texture(const std::vector<uint8_t>& encoded) {
  if (!images_ || encoded.empty()) return nullptr;
  int width = 0;
  int height = 0;
  const std::vector<uint8_t> rgba = rex::ui::DecodeImageRGBA(encoded.data(), encoded.size(), width, height);
  if (rgba.empty() || width <= 0 || height <= 0) return nullptr;
  return images_->CreateTexture(static_cast<uint32_t>(width), static_cast<uint32_t>(height), rex::ui::ImmediateTextureFilter::kLinear, false, rgba.data());
}

rex::ui::ImmediateTexture* MenuModel::ModIcon(const ModInfo& mod) {
  auto [it, added] = mod_icons_.try_emplace(mod.folder);
  // Made once per scan, failed or not, so a missing icon isn't looked for every frame.
  if (added) it->second.texture = Texture(ReadFile(mod.dir / "icon.png"));
  return it->second.texture.get();
}

rex::ui::ImmediateTexture* MenuModel::PackageIcon(const net::Package& package, bool* loading) {
  if (!thunderstore_ || package.icon_url.empty()) return nullptr;
  if (auto it = package_icons_.find(package.icon_url); it != package_icons_.end()) {
    it->second.used = frame_;
    return it->second.texture.get();
  }
  thunderstore_->RequestIcon(package.icon_url);
  std::vector<uint8_t> encoded;
  if (!thunderstore_->TakeIcon(package.icon_url, encoded)) {
    if (loading) *loading = thunderstore_->IconLoading(package.icon_url);
    return nullptr;
  }
  if (package_icons_.size() >= kMaxPackageIcons) PrunePackageIcons();
  CachedIcon& icon = package_icons_[package.icon_url];
  icon = {Texture(encoded), frame_};
  return icon.texture.get();
}

void MenuModel::PrunePackageIcons() {
  // The least recently drawn go first. This frame's are kept, as its draw list may hold them.
  std::vector<std::pair<uint64_t, std::string>> by_use;
  for (const auto& [url, icon] : package_icons_) {
    if (icon.used < frame_) by_use.emplace_back(icon.used, url);
  }
  std::sort(by_use.begin(), by_use.end());
  const size_t drop = std::min(by_use.size(), package_icons_.size() - kMaxPackageIcons * 3 / 4);
  for (size_t i = 0; i < drop; ++i) {
    package_icons_.erase(by_use[i].second);
    thunderstore_->ForgetIcon(by_use[i].second);
  }
}

void MenuModel::OpenModsFolder() const {
  std::error_code error;
  std::filesystem::create_directories(mods_.root(), error);
  OpenFolder(mods_.root());
}

}  // namespace rexfabric::ui
