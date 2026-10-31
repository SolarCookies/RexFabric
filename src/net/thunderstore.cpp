#include "thunderstore.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <string_view>
#include <utility>

#include <miniz.h>
#include <nlohmann/json.hpp>

#include "../log.h"
#include "http.h"

namespace rexfabric::net {
namespace {

constexpr const char* kHost = "https://thunderstore.io";
// Enough to keep a page of icons quick without hammering the CDN.
constexpr size_t kWorkers = 3;
// How deep dependencies of dependencies go before giving up on a loop.
constexpr int kMaxDependencyDepth = 8;

std::string Encode(std::string_view text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : text) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

std::string ListingUrl(const std::string& community, const Query& query) {
  std::string url = std::string(kHost) + "/api/cyberstorm/listing/" + Encode(community) + "/?ordering=" + Encode(query.ordering) + "&page=" + std::to_string(std::max(1, query.page));
  if (!query.search.empty()) url += "&q=" + Encode(query.search);
  if (!query.category_id.empty()) url += "&included_categories=" + Encode(query.category_id);
  return url + (query.deprecated ? "&deprecated=True" : "&deprecated=False") + "&nsfw=False";
}

std::string CategoriesUrl(const std::string& community) { return std::string(kHost) + "/api/cyberstorm/community/" + Encode(community) + "/filters/"; }
std::string PackageApiUrl(const std::string& owner, const std::string& name) { return std::string(kHost) + "/api/experimental/package/" + Encode(owner) + "/" + Encode(name) + "/"; }
std::string PackagePageUrl(const std::string& community, const std::string& owner, const std::string& name) { return std::string(kHost) + "/c/" + Encode(community) + "/p/" + Encode(owner) + "/" + Encode(name) + "/"; }

// Fields are all optional to us: a listing that gained or lost a key should still show, with a blank.
std::string String(const nlohmann::json& json, const char* key) {
  const auto it = json.find(key);
  return it != json.end() && it->is_string() ? it->get<std::string>() : std::string();
}

uint64_t Unsigned(const nlohmann::json& json, const char* key) {
  const auto it = json.find(key);
  return it != json.end() && it->is_number_unsigned() ? it->get<uint64_t>() : 0;
}

bool Bool(const nlohmann::json& json, const char* key) {
  const auto it = json.find(key);
  return it != json.end() && it->is_boolean() && it->get<bool>();
}

bool ParseListing(const std::string& body, const std::string& community, Thunderstore::Page& page) {
  const nlohmann::json root = nlohmann::json::parse(body, nullptr, false);
  const auto results = root.is_object() ? root.find("results") : root.end();
  if (results == root.end() || !results->is_array()) return false;
  page.total = static_cast<uint32_t>(Unsigned(root, "count"));
  for (const auto& entry : *results) {
    if (!entry.is_object()) continue;
    Package package{String(entry, "name"), String(entry, "namespace"), String(entry, "description"), String(entry, "icon_url")};
    package.page_url = PackagePageUrl(community, package.owner, package.name);
    package.downloads = Unsigned(entry, "download_count");
    package.likes = static_cast<uint32_t>(Unsigned(entry, "rating_count"));
    package.deprecated = Bool(entry, "is_deprecated");
    if (const auto categories = entry.find("categories"); categories != entry.end() && categories->is_array()) {
      for (const auto& category : *categories) {
        if (category.is_object() && !String(category, "name").empty()) package.categories.push_back(String(category, "name"));
      }
    }
    page.packages.push_back(std::move(package));
  }
  return true;
}

bool ParseCategories(const std::string& body, std::vector<Category>& out) {
  const nlohmann::json root = nlohmann::json::parse(body, nullptr, false);
  const auto categories = root.is_object() ? root.find("package_categories") : root.end();
  if (categories == root.end() || !categories->is_array()) return false;
  for (const auto& entry : *categories) {
    if (entry.is_object() && !String(entry, "id").empty()) out.push_back({String(entry, "id"), String(entry, "name")});
  }
  std::sort(out.begin(), out.end(), [](const Category& a, const Category& b) { return a.name < b.name; });
  return true;
}

// A zip is untrusted: an entry named "../../x" would otherwise land outside the mods folder.
bool SafePath(std::string_view path) {
  if (path.empty() || path.front() == '/' || path.front() == '\\' || path.find(':') != std::string_view::npos) return false;
  for (size_t start = 0; start <= path.size();) {
    const size_t end = std::min(path.find_first_of("/\\", start), path.size());
    if (path.substr(start, end - start) == "..") return false;
    start = end + 1;
  }
  return true;
}

bool Extract(const std::string& archive, const std::filesystem::path& dir, std::string& error) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0)) {
    error = "The download isn't a zip archive.";
    return false;
  }
  bool ok = true;
  const mz_uint count = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < count && ok; ++i) {
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&zip, i, &stat) || !SafePath(stat.m_filename)) {
      error = "The archive has an entry outside the mod folder.";
      ok = false;
      break;
    }
    const std::filesystem::path target = dir / std::filesystem::path(std::string(stat.m_filename)).make_preferred();
    std::error_code fs_error;
    if (mz_zip_reader_is_file_a_directory(&zip, i)) {
      std::filesystem::create_directories(target, fs_error);
      continue;
    }
    std::filesystem::create_directories(target.parent_path(), fs_error);
    // Through memory rather than miniz's own file writer, which goes through fopen and mangles non ASCII
    // paths on Windows.
    size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    ok = data && out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!ok) error = "Could not write " + target.string() + ".";
    mz_free(data);
  }
  mz_zip_reader_end(&zip);
  return ok;
}

// Unpacks beside the mod and swaps it in, so a failure leaves what was installed alone and an update keeps
// no stale files from the version before.
bool InstallArchive(const std::string& archive, const std::filesystem::path& dir, std::string& error) {
  const std::filesystem::path staging = dir.parent_path() / (dir.filename().string() + ".installing");
  std::error_code fs_error;
  std::filesystem::remove_all(staging, fs_error);
  std::filesystem::create_directories(staging, fs_error);
  if (fs_error || !Extract(archive, staging, error)) {
    if (error.empty()) error = "Could not create " + staging.string() + ".";
    std::filesystem::remove_all(staging, fs_error);
    return false;
  }
  std::filesystem::remove_all(dir, fs_error);
  if (!fs_error) std::filesystem::rename(staging, dir, fs_error);
  if (fs_error) {
    error = "Could not replace " + dir.string() + "; close anything using its files and try again.";
    std::filesystem::remove_all(staging, fs_error);
    return false;
  }
  return true;
}

// "Owner-Name-1.2.3" as owner and name. Thunderstore names have no dashes of their own.
bool SplitDependency(const std::string& text, std::string& owner, std::string& name) {
  const size_t first = text.find('-');
  const size_t last = text.rfind('-');
  if (first == std::string::npos || last == first) return false;
  owner = text.substr(0, first);
  name = text.substr(first + 1, last - first - 1);
  return !owner.empty() && !name.empty();
}

// Installs a package and every dependency not installed yet. `folders` gets each folder installed.
bool InstallPackage(Http& http, const std::filesystem::path& mods_dir, const std::string& owner, const std::string& name, bool replace, int depth, std::set<std::string>& seen, std::vector<std::string>& folders, std::string& error) {
  if (!seen.insert(owner + "-" + name).second) return true;
  std::error_code fs_error;
  if (!replace && std::filesystem::is_directory(mods_dir / name, fs_error)) return true;
  if (depth > kMaxDependencyDepth) {
    error = "The dependencies go too deep.";
    return false;
  }
  std::string body;
  if (!http.Get(PackageApiUrl(owner, name), body, error)) return false;
  const nlohmann::json root = nlohmann::json::parse(body, nullptr, false);
  const auto latest = root.is_object() ? root.find("latest") : root.end();
  if (latest == root.end() || !latest->is_object() || String(*latest, "download_url").empty()) {
    error = "Thunderstore offered no download for " + name + ".";
    return false;
  }
  if (const auto dependencies = latest->find("dependencies"); dependencies != latest->end() && dependencies->is_array()) {
    for (const auto& dependency : *dependencies) {
      std::string dependency_owner, dependency_name;
      if (!dependency.is_string() || !SplitDependency(dependency.get<std::string>(), dependency_owner, dependency_name)) continue;
      if (!InstallPackage(http, mods_dir, dependency_owner, dependency_name, false, depth + 1, seen, folders, error)) return false;
    }
  }
  std::string archive;
  if (!http.Get(String(*latest, "download_url"), archive, error) || !InstallArchive(archive, mods_dir / name, error)) return false;
  RF_INFO("installed {} {} from Thunderstore", name, String(*latest, "version_number"));
  folders.push_back(name);
  return true;
}

}  // namespace

std::string Query::BaseKey() const { return ordering + '\x1f' + category_id + '\x1f' + search + '\x1f' + (deprecated ? '1' : '0'); }
std::string Query::Key() const { return BaseKey() + '\x1f' + std::to_string(page); }

Thunderstore::Thunderstore(std::string community, std::filesystem::path mods_dir) : community_(std::move(community)), mods_dir_(std::move(mods_dir)) {}

Thunderstore::~Thunderstore() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    jobs_.clear();
  }
  wake_.notify_all();
  for (std::thread& worker : workers_) worker.join();
}

void Thunderstore::Enqueue(Job job) {
  {
    std::lock_guard lock(mutex_);
    jobs_.push_back(std::move(job));
    // Workers start on first use, so a game that never opens the browser pays for no threads.
    while (workers_.size() < kWorkers) workers_.emplace_back([this] { Work(); });
  }
  wake_.notify_one();
}

void Thunderstore::Work() {
  Http http;
  for (;;) {
    Job job;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (stopping_) return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    Result result{job.kind, job.key};
    std::string body;
    switch (job.kind) {
      case Kind::kListing:
        result.ok = http.Get(job.url, body, result.error) && ParseListing(body, community_, result.page);
        if (!result.ok && result.error.empty()) result.error = "Thunderstore sent a listing that can't be read.";
        break;
      case Kind::kCategories:
        result.ok = http.Get(job.url, body, result.error) && ParseCategories(body, result.categories);
        break;
      case Kind::kIcon:
        result.ok = http.Get(job.url, body, result.error) && !body.empty();
        result.bytes.assign(body.begin(), body.end());
        break;
      case Kind::kInstall: {
        std::set<std::string> seen;
        result.ok = InstallPackage(http, mods_dir_, job.owner, job.name, true, 0, seen, result.folders, result.error);
        break;
      }
    }
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    results_.push_back(std::move(result));
  }
}

void Thunderstore::Poll() {
  std::vector<Result> ready;
  {
    std::lock_guard lock(mutex_);
    ready.swap(results_);
  }
  for (Result& result : ready) {
    switch (result.kind) {
      case Kind::kListing: {
        // Gone when the query changed while this was in flight.
        const auto it = pages_.find(result.key);
        if (it == pages_.end()) break;
        if (result.ok) it->second = std::move(result.page);
        it->second.status = result.ok ? Status::kReady : Status::kError;
        it->second.error = std::move(result.error);
        break;
      }
      case Kind::kCategories:
        if (result.ok) categories_ = std::move(result.categories);
        break;
      case Kind::kIcon:
        icons_[result.key] = {result.ok ? Status::kReady : Status::kError, std::move(result.bytes)};
        break;
      case Kind::kInstall: {
        Install& install = installs_[result.key];
        install.status = result.ok ? Status::kReady : Status::kError;
        install.error = std::move(result.error);
        if (!result.ok) RF_ERROR("installing {} failed: {}", result.key, install.error);
        installed_.insert(installed_.end(), result.folders.begin(), result.folders.end());
        break;
      }
    }
  }
}

const Thunderstore::Page& Thunderstore::GetPage(const Query& query) {
  if (query.BaseKey() != base_key_) {
    pages_.clear();
    base_key_ = query.BaseKey();
  }
  const std::string key = query.Key();
  const auto [it, inserted] = pages_.try_emplace(key);
  if (inserted) {
    it->second.status = Status::kLoading;
    Enqueue({Kind::kListing, key, ListingUrl(community_, query)});
  }
  return it->second;
}

const std::vector<Category>& Thunderstore::Categories() {
  if (!categories_requested_) {
    categories_requested_ = true;
    Enqueue({Kind::kCategories, community_, CategoriesUrl(community_)});
  }
  return categories_;
}

void Thunderstore::Invalidate() {
  pages_.clear();
  base_key_.clear();
}

void Thunderstore::RequestIcon(const std::string& url) {
  if (url.empty() || icons_.contains(url)) return;
  icons_[url] = {Status::kLoading, {}};
  Enqueue({Kind::kIcon, url, url});
}

bool Thunderstore::TakeIcon(const std::string& url, std::vector<uint8_t>& bytes) {
  const auto it = icons_.find(url);
  if (it == icons_.end() || it->second.first != Status::kReady) return false;
  bytes = std::move(it->second.second);
  // Kept as a marker that it was fetched, without the bytes.
  it->second = {Status::kIdle, {}};
  return !bytes.empty();
}

void Thunderstore::RequestInstall(const Package& package) {
  if (package.name.empty() || package.owner.empty()) return;
  const std::string key = package.owner + "/" + package.name;
  Install& install = installs_[key];
  if (install.status == Status::kLoading) return;
  install = {Status::kLoading, {}};
  Enqueue({Kind::kInstall, key, {}, package.owner, package.name});
}

const Thunderstore::Install* Thunderstore::GetInstall(const Package& package) const {
  const auto it = installs_.find(package.owner + "/" + package.name);
  return it != installs_.end() ? &it->second : nullptr;
}

std::vector<std::string> Thunderstore::TakeInstalled() { return std::exchange(installed_, {}); }

}  // namespace rexfabric::net
