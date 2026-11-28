// The content browser's thumbnails (docs/content_and_classes.md, Thumbnails): what a tile shows of an asset in place of
// its kind's icon, the first of these it has:
//
//   1. a picture of its own: a class's `thumbnail` property, a game asset's thumbnail field (a Texture asset)
//   2. its type's 2D preview (AssetTypeDecl::preview), drawn from its layers, for a game asset type with one
//   3. a picture the game's viewport renderer makes of it (ViewportRenderer::capture): of an avatar class's viewport, or
//      of a game asset type's asset viewport, framed as its thumbnail_view says (content/thumbnail_view.h) or the
//      default way, an asset's played a second first
//   4. none, for the icon, as while a picture is still being made
//
// Made pictures are kept as PNG files outside the mods, so they're never shipped with one: under thumbnails beside the
// executable, thumbnails/<mod>/<path in the mod>.png, with thumbnails/<mod>/index.json giving the hash of what each
// was made from (the asset's file, a class's ancestors' files, the pictures of the Texture assets they name). One is
// made again once that hash changes. They're made lazily, for the tiles and editors that show them, one at a time, and
// one not shown for a while is let go.
//
//   Thumbnails thumbnails(model, types, find);
//   thumbnails.Poll();                                        // once a frame
//   if (!thumbnails.Draw(draw, "MyMod/Rock", min, side)) ...  // the icon

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <rexfabric/content.h>

#include "../content/assets.h"
#include "menu_model.h"

namespace rexfabric::ui {

// A preview's layers (content::PreviewLayer) drawn into the square at `min`, `side` wide, bottom first, each picture
// read through `model`, every colour's alpha times `alpha`.
void DrawPreviewLayers(ImDrawList* draw, MenuModel& model, const std::vector<content::PreviewLayer>& layers, const ImVec2& min, float side, float alpha = 1.0f);

class Thumbnails {
 public:
  // An asset of the content as last read, by its path; null for none.
  using FindAsset = std::function<const content::AssetInfo*(std::string_view path)>;

  // `types` is what a game asset's fields are fitted with, kept by the caller as long as this is.
  Thumbnails(MenuModel& model, const content::TypeLookup& types, FindAsset find);
  ~Thumbnails();
  Thumbnails(const Thumbnails&) = delete;
  Thumbnails& operator=(const Thumbnails&) = delete;

  // Once a frame, before anything is drawn: a finished picture written and shown, the next one asked for started,
  // pictures not shown lately let go.
  void Poll();
  // The content was read again (a save, a refresh): what each asset's thumbnail is made from is worked out again the
  // next time it's shown, and remade when that changed.
  void Forget();

  // Draws an asset's thumbnail into the square at `min`, `side` wide, every colour's alpha times `alpha`; false when it
  // has none to show (yet), for the caller's icon.
  bool Draw(ImDrawList* draw, const std::string& path, const ImVec2& min, float side, float alpha = 1.0f);
  // The same as an item of the window being drawn, framed, `side` wide: for an editor showing the thumbnail its asset
  // gets, as saved.
  void Item(const std::string& path, float side);

 private:
  // Where an asset's thumbnail comes from (the order in the comment above).
  enum class Source { kIcon, kPicture, kPreview, kRendered };

  struct Entry {
    std::string path;
    bool known = false;  // worked out since the content was last read
    Source source = Source::kIcon;
    std::filesystem::path picture;              // kPicture: its Texture asset's picture file
    std::vector<content::PreviewLayer> layers;  // kPreview
    // kRendered: what the renderer is asked for, and the hash of everything making its look.
    content::ViewportView view;
    double seconds = 0.0;
    uint64_t hash = 0;
    // The picture made of it, of the hash it was made from: shown, though out of date, until the new one is made.
    std::unique_ptr<MenuModel::Texture> texture;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t texture_hash = 0;
    uint64_t looked_for = 0;  // the hash the cache was last looked in for
    uint64_t failed = 0;      // the hash a picture couldn't be made of, so it isn't asked for again
    bool queued = false;
    uint64_t used = 0;  // the frame it was last shown in
  };

  // What the renderer gives back for the capture in flight, filled in on whatever thread it calls back on.
  struct Pending;
  struct InFlight {
    std::string key;
    uint64_t hash = 0;
    uint64_t viewport = 0;
    std::chrono::steady_clock::time_point started;
    std::shared_ptr<Pending> pending;
  };

  Entry& EntryOf(const std::string& path);
  // Works out where an entry's thumbnail comes from and the hash of its look, reading its files.
  void Resolve(Entry& entry);
  void ResolveClass(Entry& entry, const content::AssetInfo& info);
  void ResolveGameAsset(Entry& entry, const content::AssetInfo& info);
  // The Texture asset a picture value names, as its picture file; empty for none.
  std::filesystem::path PictureFile(const nlohmann::json& value) const;
  // The hash of everything making a rendered thumbnail's look: these files, and the pictures of the Texture assets
  // they name.
  uint64_t HashOf(const std::vector<std::filesystem::path>& files) const;

  // The rendered picture, from the cache when it's there and up to date.
  bool LoadCached(Entry& entry);
  // Shows pixels as an entry's picture, of the hash they were made from.
  void Show(Entry& entry, uint64_t hash, uint32_t width, uint32_t height, std::span<const uint8_t> rgba);
  void TakeFinished();
  void StartNext();
  // Writes a made picture into the cache, as made from `hash`; with no picture, notes that what `hash` makes shows
  // nothing, so it isn't made again only to show nothing.
  void Store(const std::string& path, uint64_t hash, const content::ViewportCapture* capture);

  // The cache: thumbnails/<mod>/, its index read once. By lower case path in the mod, the hash each picture was made
  // from, and the hashes whose pictures showed nothing (its tile keeps the icon).
  struct ModIndex {
    std::map<std::string, uint64_t> made;
    std::map<std::string, uint64_t> empty;
  };
  std::filesystem::path PngFile(std::string_view path) const;
  ModIndex& IndexOf(std::string_view mod);
  void WriteIndex(std::string_view mod);

  MenuModel& model_;
  const content::TypeLookup& types_;
  FindAsset find_;
  std::filesystem::path root_;
  std::map<std::string, Entry> entries_;                         // by lower case path
  std::map<std::string, ModIndex> index_;                         // by lower case mod
  std::deque<std::string> queue_;                                 // lower case paths, in the order they were asked for
  std::optional<InFlight> in_flight_;
  bool unavailable_ = false;  // the renderer makes no pictures: nothing more is asked of it
  uint64_t frame_ = 0;
};

}  // namespace rexfabric::ui
