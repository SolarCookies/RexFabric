// Thunderstore: listing a community's packages, and installing them with their dependencies.
//
// Requests run on worker threads. Every method is for the menu thread, which calls Poll once per frame to
// pick up what the workers finished; everything read back is then owned by that thread alone.

#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rexfabric::net {

struct Package {
  std::string name;
  std::string owner;  // the API's "namespace"
  std::string description;
  std::string icon_url;
  std::string page_url;
  std::vector<std::string> categories;
  uint64_t downloads = 0;
  uint32_t likes = 0;
  bool deprecated = false;
};

struct Category {
  std::string id;
  std::string name;
};

// What decides which packages a listing returns.
struct Query {
  std::string search;
  std::string ordering = "most-downloaded";
  std::string category_id;
  bool deprecated = false;
  int page = 1;  // from 1, kPageSize packages each

  std::string BaseKey() const;  // everything but the page
  std::string Key() const;
};

// The orderings the listing API accepts.
struct Ordering {
  const char* value;
  const char* label;
};
inline constexpr Ordering kOrderings[] = {{"most-downloaded", "Most downloaded"}, {"top-rated", "Top rated"}, {"last-updated", "Recently updated"}, {"newest", "Newest"}};

enum class Status { kIdle, kLoading, kReady, kError };

class Thunderstore {
 public:
  static constexpr int kPageSize = 20;

  struct Page {
    Status status = Status::kIdle;
    std::string error;
    uint32_t total = 0;
    std::vector<Package> packages;
  };

  struct Install {
    Status status = Status::kIdle;
    std::string error;
  };

  Thunderstore(std::string community, std::filesystem::path mods_dir);
  ~Thunderstore();
  Thunderstore(const Thunderstore&) = delete;
  Thunderstore& operator=(const Thunderstore&) = delete;

  void Poll();

  // The cached page, fetched the first time it's asked for. A query with a new BaseKey drops the pages of
  // the old one, so only one result set is ever kept.
  const Page& GetPage(const Query& query);
  const std::vector<Category>& Categories();
  void Invalidate();

  // Icons arrive encoded; TakeIcon hands the bytes over once, the frame they're ready.
  void RequestIcon(const std::string& url);
  bool TakeIcon(const std::string& url, std::vector<uint8_t>& bytes);

  // Installs the latest version into mods_dir/<name>, and any dependency not installed yet.
  void RequestInstall(const Package& package);
  const Install* GetInstall(const Package& package) const;
  // The folders installed since the last call.
  std::vector<std::string> TakeInstalled();

  const std::string& community() const { return community_; }

 private:
  enum class Kind { kListing, kCategories, kIcon, kInstall };

  struct Job {
    Kind kind;
    std::string key;
    std::string url;
    std::string owner;  // kInstall
    std::string name;   // kInstall
  };

  struct Result {
    Kind kind;
    std::string key;
    bool ok = false;
    std::string error;
    Page page;
    std::vector<Category> categories;
    std::vector<uint8_t> bytes;
    std::vector<std::string> folders;
  };

  void Enqueue(Job job);
  void Work();

  std::string community_;
  std::filesystem::path mods_dir_;

  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  std::vector<Result> results_;
  std::vector<std::thread> workers_;
  bool stopping_ = false;

  std::string base_key_;
  std::unordered_map<std::string, Page> pages_;
  std::vector<Category> categories_;
  bool categories_requested_ = false;
  std::unordered_map<std::string, std::pair<Status, std::vector<uint8_t>>> icons_;
  std::unordered_map<std::string, Install> installs_;
  std::vector<std::string> installed_;
};

}  // namespace rexfabric::net
