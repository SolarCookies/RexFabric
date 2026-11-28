#include "thumbnails.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>
#include <utility>

#include <rex/platform/file.h>

#include "../content/game_assets.h"
#include "../content/thumbnail_view.h"
#include "../content/viewport.h"
#include "../log.h"
#include "avatar_viewport.h"
#include "image_decode.h"

namespace rexfabric::ui {

struct Thumbnails::Pending {
  std::mutex mutex;
  bool done = false;
  content::ViewportCapture capture;
};

namespace {

using content::AssetInfo;
using content::AssetKind;

// The pictures made: square, this many pixels a side.
constexpr uint32_t kSide = 256;
// How long an asset viewport plays before its picture is taken, without a framing saying: an effect's particles out.
constexpr double kAssetSeconds = 1.0;
// Frames a thumbnail asked for may go unshown and still be made; frames a made one may go unshown and still be kept.
constexpr uint64_t kWantedFrames = 3;
constexpr uint64_t kUnusedFrames = 600;
// How long the renderer has to give back a picture before it's given up on.
constexpr auto kPatience = std::chrono::seconds(30);
// Goes into every hash, so a change to how the pictures are made remakes them all.
constexpr uint64_t kCacheVersion = 1;

constexpr const char* kFolder = "thumbnails";
constexpr const char* kIndexFile = "index.json";
constexpr const char* kIndexKey = "thumbnails";
constexpr const char* kEmptyKey = "empty";
constexpr std::string_view kClassThumbnail = "thumbnail";

std::string Lower(std::string_view text) {
  std::string lower(text);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower;
}

bool Same(std::string_view a, std::string_view b) { return Lower(a) == Lower(b); }

std::vector<uint8_t> ReadFile(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  return in ? std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {}) : std::vector<uint8_t>{};
}

// The path after its mod: "Garden/Rock" of "MyMod/Garden/Rock".
std::string_view InMod(std::string_view path) {
  const size_t slash = path.find('/');
  return slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
}

// 64 bit FNV-1a, over bytes and numbers alike.
class Hash {
 public:
  void Add(std::span<const uint8_t> bytes) {
    for (const uint8_t byte : bytes) value_ = (value_ ^ byte) * kPrime;
  }
  void Add(uint64_t number) {
    for (int i = 0; i < 8; ++i) value_ = (value_ ^ ((number >> (i * 8)) & 0xFF)) * kPrime;
  }
  uint64_t value() const { return value_; }

 private:
  static constexpr uint64_t kPrime = 0x100000001B3ull;
  uint64_t value_ = 0xCBF29CE484222325ull;
};

void ForEachString(const nlohmann::json& value, const std::function<void(const std::string&)>& visit) {
  if (value.is_string()) {
    visit(value.get_ref<const std::string&>());
  } else if (value.is_structured()) {
    for (const nlohmann::json& item : value) ForEachString(item, visit);
  }
}

// A picture `width` by `height` as large as fits the square, its shape kept, in its middle.
void DrawFitted(ImDrawList* draw, ImTextureID id, uint32_t width, uint32_t height, const ImVec2& min, float side, float alpha) {
  if (width == 0 || height == 0) return;
  const float scale = side / static_cast<float>(std::max(width, height));
  const ImVec2 half(static_cast<float>(width) * scale * 0.5f, static_cast<float>(height) * scale * 0.5f);
  const ImVec2 centre(min.x + side * 0.5f, min.y + side * 0.5f);
  draw->AddImageRounded(id, ImVec2(centre.x - half.x, centre.y - half.y), ImVec2(centre.x + half.x, centre.y + half.y), ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)), 3.0f);
}

// The camera a thumbnail is taken with: its framing's, or the viewport's default.
void Frame(content::ViewportView& view, const std::optional<content::ThumbnailView>& framing) {
  view.yaw = framing ? framing->yaw : kViewportStartYaw;
  view.pitch = framing ? framing->pitch : kViewportStartPitch;
  view.zoom = framing ? framing->zoom : 1.0;
  view.width = kSide;
  view.height = kSide;
  view.playing = true;
}

}  // namespace

void DrawPreviewLayers(ImDrawList* draw, MenuModel& model, const std::vector<content::PreviewLayer>& layers, const ImVec2& at, float side, float alpha) {
  for (const content::PreviewLayer& layer : layers) {
    const ImVec2 min(at.x + layer.rect[0] * side, at.y + layer.rect[1] * side);
    const ImVec2 extent(layer.rect[2] * side, layer.rect[3] * side);
    const ImU32 colour = ImGui::ColorConvertFloat4ToU32(ImVec4(layer.colour[0], layer.colour[1], layer.colour[2], layer.colour[3] * alpha));
    const bool clipped = layer.clip[2] > 0.0f;
    if (clipped) draw->PushClipRect(ImVec2(at.x + layer.clip[0] * side, at.y + layer.clip[1] * side), ImVec2(at.x + (layer.clip[0] + layer.clip[2]) * side, at.y + (layer.clip[1] + layer.clip[3]) * side), true);
    // A layer's picture: its Texture asset's, or its own file.
    std::filesystem::path file = layer.file;
    if (!layer.texture.empty()) {
      const ModInfo* mod = model.mods().Find(content::ModOf(layer.texture));
      file = mod ? content::TextureImageFile(content::AssetFile(*mod, layer.texture)) : std::filesystem::path();
    }
    const MenuModel::Picture picture = file.empty() ? MenuModel::Picture{} : model.AssetPicture(file);
    if (picture.id != ImTextureID_Invalid && picture.width && picture.height) {
      // As large as fits its part of the square, its shape kept, centred.
      const float scale = std::min(extent.x / static_cast<float>(picture.width), extent.y / static_cast<float>(picture.height));
      const ImVec2 fitted = layer.stretch ? extent : ImVec2(static_cast<float>(picture.width) * scale, static_cast<float>(picture.height) * scale);
      const ImVec2 corner(min.x + (extent.x - fitted.x) * 0.5f, min.y + (extent.y - fitted.y) * 0.5f);
      draw->AddImage(ImTextureRef(picture.id), corner, ImVec2(corner.x + fitted.x, corner.y + fitted.y), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), colour);
    } else if (layer.shape.size() >= 3) {
      std::vector<ImVec2> points;
      for (const auto& [x, y] : layer.shape) points.emplace_back(min.x + x * extent.x, min.y + y * extent.y);
      draw->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), colour);
    }
    if (clipped) draw->PopClipRect();
  }
}

Thumbnails::Thumbnails(MenuModel& model, const content::TypeLookup& types, FindAsset find) : model_(model), types_(types), find_(std::move(find)), root_(rex::platform::GetExecutableFolder() / kFolder) {}

Thumbnails::~Thumbnails() {
  if (in_flight_) content::CloseViewport(in_flight_->viewport);
  // The frame being drawn may still hold them.
  for (auto& [key, entry] : entries_) model_.Retire(std::move(entry.texture));
}

void Thumbnails::Poll() {
  ++frame_;
  TakeFinished();
  StartNext();
  for (auto& [key, entry] : entries_) {
    if (!entry.texture || frame_ - entry.used <= kUnusedFrames) continue;
    model_.Retire(std::move(entry.texture));
    entry.texture_hash = 0;
    entry.looked_for = 0;
  }
}

void Thumbnails::Forget() {
  for (auto& [key, entry] : entries_) entry.known = false;
}

Thumbnails::Entry& Thumbnails::EntryOf(const std::string& path) {
  auto [at, added] = entries_.try_emplace(Lower(path));
  if (added) at->second.path = path;
  return at->second;
}

bool Thumbnails::Draw(ImDrawList* draw, const std::string& path, const ImVec2& min, float side, float alpha) {
  Entry& entry = EntryOf(path);
  entry.used = frame_;
  if (!entry.known) Resolve(entry);
  switch (entry.source) {
    case Source::kIcon: return false;
    case Source::kPicture: {
      const MenuModel::Picture picture = model_.AssetPicture(entry.picture);
      if (picture.id == ImTextureID_Invalid) return false;
      DrawFitted(draw, picture.id, picture.width, picture.height, min, side, alpha);
      return true;
    }
    case Source::kPreview: DrawPreviewLayers(draw, model_, entry.layers, min, side, alpha); return true;
    case Source::kRendered: {
      if (entry.texture_hash != entry.hash && entry.looked_for != entry.hash) LoadCached(entry);
      const bool wanted = entry.texture_hash != entry.hash && entry.failed != entry.hash;
      if (wanted && !entry.queued && !unavailable_) {
        entry.queued = true;
        queue_.push_back(Lower(path));
      }
      // An out of date picture shows until the new one is made.
      if (!entry.texture) return false;
      DrawFitted(draw, entry.texture->id(), entry.width, entry.height, min, side, alpha);
      return true;
    }
  }
  return false;
}

void Thumbnails::Item(const std::string& path, float side) {
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(side, side));
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(at, ImVec2(at.x + side, at.y + side), ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
  if (!Draw(draw, path, at, side)) {
    const Entry& entry = EntryOf(path);
    const char* none = entry.source == Source::kRendered && entry.failed != entry.hash && !unavailable_ ? "Making..." : "Icon";
    const ImVec2 text = ImGui::CalcTextSize(none);
    draw->AddText(ImVec2(at.x + (side - text.x) * 0.5f, at.y + (side - text.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), none);
  }
  ImGui::SetItemTooltip("The thumbnail the content browser shows for it, as it was last saved: its own picture, its preview, or one made of its viewport (Capture Thumbnail frames it)");
}

// Where a thumbnail comes from ---------------------------------------------------------------------------------------

void Thumbnails::Resolve(Entry& entry) {
  entry.known = true;
  entry.source = Source::kIcon;
  entry.picture.clear();
  entry.layers.clear();
  entry.view = {};
  entry.seconds = 0.0;
  entry.hash = 0;
  const AssetInfo* info = find_(entry.path);
  if (!info) return;
  if (info->kind == AssetKind::kClass) ResolveClass(entry, *info);
  else if (info->kind == AssetKind::kGame) ResolveGameAsset(entry, *info);
}

void Thumbnails::ResolveClass(Entry& entry, const AssetInfo& info) {
  const std::optional<content::ClassAsset> asset = content::ReadClassAsset(info.file, nullptr);
  if (!asset) return;
  const ModList& mods = model_.mods();
  const std::vector<content::Property> inherited = content::InheritedProperties(mods, asset->parent);
  // Its own picture: the class's thumbnail, or the one it inherits.
  nlohmann::json picture;
  for (const auto& [key, value] : asset->defaults.items()) {
    if (picture.is_null() && Same(key, kClassThumbnail)) picture = value;
  }
  for (const content::Property& property : inherited) {
    if (picture.is_null() && Same(property.name, kClassThumbnail)) picture = property.value;
  }
  if (std::filesystem::path file = PictureFile(picture); !file.empty()) {
    entry.source = Source::kPicture;
    entry.picture = std::move(file);
    return;
  }
  // A picture of its viewport, for an avatar class (as its editor shows the viewport).
  const content::InheritedAvatar avatar = content::InheritAvatar(mods, asset->parent);
  if (avatar.backing.empty() && asset->backing.empty() && asset->components.empty()) return;
  const std::optional<content::ThumbnailView> framing = content::ReadThumbnailView(asset->rest);
  entry.view = {.class_path = info.path, .game_class = avatar.game_class, .backing = asset->backing.empty() ? avatar.backing : asset->backing, .values = ClassViewValues(inherited, *asset), .parts = ViewportParts(avatar.components, asset->components)};
  Frame(entry.view, framing);
  entry.seconds = framing && framing->time ? *framing->time : 0.0;
  // Its look is its file's and its ancestors'.
  std::vector<std::filesystem::path> files{info.file};
  for (const std::string& ancestor : content::ClassChain(mods, info.path)) {
    const AssetInfo* found = find_(ancestor);
    if (found && found->kind == AssetKind::kClass && found->file != info.file) files.push_back(found->file);
  }
  entry.hash = HashOf(files);
  entry.source = Source::kRendered;
}

void Thumbnails::ResolveGameAsset(Entry& entry, const AssetInfo& info) {
  const std::optional<content::GameAssetFile> file = content::ReadGameAsset(info.file, nullptr);
  if (!file) return;
  const content::AssetType& type = *file->type;
  const nlohmann::json fitted = content::FittedValues(types_, type, file->json);
  const auto own = fitted.find(std::string(content::kThumbnailField));
  if (std::filesystem::path picture = own != fitted.end() ? PictureFile(*own) : std::filesystem::path(); !picture.empty()) {
    entry.source = Source::kPicture;
    entry.picture = std::move(picture);
    return;
  }
  if (type.preview) {
    try {
      entry.layers = type.preview(fitted.dump());
      entry.source = Source::kPreview;
    } catch (const std::exception& error) {
      RF_WARN("content browser: the preview of {}: {}", info.path, error.what());
    }
    return;
  }
  if (!type.viewport) return;
  content::AssetViewportScene scene;
  if (type.viewport->build) {
    try {
      scene = type.viewport->build(info.path, fitted.dump());
    } catch (const std::exception&) {
      // What it can't show, its editor says; its tile keeps the icon.
      return;
    }
  }
  const std::optional<content::ThumbnailView> framing = content::ReadThumbnailView(file->json);
  entry.view = {.backing = type.viewport->backing, .values = std::move(scene.values), .parts = std::move(scene.parts), .asset = info.path};
  Frame(entry.view, framing);
  entry.view.looping = type.viewport->looping;
  entry.seconds = framing && framing->time ? *framing->time : kAssetSeconds;
  entry.hash = HashOf({info.file});
  entry.source = Source::kRendered;
}

std::filesystem::path Thumbnails::PictureFile(const nlohmann::json& value) const {
  if (!value.is_string() || value.get_ref<const std::string&>().empty()) return {};
  const AssetInfo* texture = find_(value.get_ref<const std::string&>());
  return texture && texture->kind == AssetKind::kTexture ? content::TextureImageFile(texture->file) : std::filesystem::path();
}

uint64_t Thumbnails::HashOf(const std::vector<std::filesystem::path>& files) const {
  Hash hash;
  hash.Add(kCacheVersion);
  hash.Add(static_cast<uint64_t>(kSide));
  for (const std::filesystem::path& file : files) {
    const std::vector<uint8_t> bytes = ReadFile(file);
    hash.Add(bytes);
    // The pictures of the Texture assets it names, by when they were written and how large they are.
    const nlohmann::json json = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
    ForEachString(json, [&](const std::string& text) {
      if (text.find('/') == std::string::npos) return;
      const std::filesystem::path picture = PictureFile(text);
      std::error_code code;
      if (picture.empty()) return;
      hash.Add(static_cast<uint64_t>(std::filesystem::last_write_time(picture, code).time_since_epoch().count()));
      hash.Add(static_cast<uint64_t>(std::filesystem::file_size(picture, code)));
    });
  }
  return hash.value();
}

// Making and keeping the pictures ------------------------------------------------------------------------------------

bool Thumbnails::LoadCached(Entry& entry) {
  entry.looked_for = entry.hash;
  const ModIndex& index = IndexOf(content::ModOf(entry.path));
  const std::string key = Lower(InMod(entry.path));
  if (const auto empty = index.empty.find(key); empty != index.empty.end() && empty->second == entry.hash) {
    entry.failed = entry.hash;
    return false;
  }
  const auto made = index.made.find(key);
  if (made == index.made.end() || made->second != entry.hash) return false;
  const std::vector<uint8_t> png = ReadFile(PngFile(entry.path));
  int width = 0, height = 0;
  const std::vector<uint8_t> rgba = DecodeImageRGBA(png.data(), png.size(), width, height);
  if (rgba.empty()) return false;
  Show(entry, entry.hash, static_cast<uint32_t>(width), static_cast<uint32_t>(height), rgba);
  return entry.texture != nullptr;
}

void Thumbnails::Show(Entry& entry, uint64_t hash, uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
  std::unique_ptr<MenuModel::Texture> texture = model_.UploadPicture(width, height, rgba);
  if (!texture) return;
  model_.Retire(std::move(entry.texture));
  entry.texture = std::move(texture);
  entry.width = width;
  entry.height = height;
  entry.texture_hash = hash;
}

void Thumbnails::TakeFinished() {
  if (!in_flight_) return;
  content::ViewportCapture capture;
  bool done = false;
  {
    std::lock_guard lock(in_flight_->pending->mutex);
    done = in_flight_->pending->done;
    if (done) capture = std::move(in_flight_->pending->capture);
  }
  const auto entry = entries_.find(in_flight_->key);
  if (!done) {
    if (std::chrono::steady_clock::now() - in_flight_->started < kPatience) return;
    content::CloseViewport(in_flight_->viewport);
    RF_WARN("content browser: no thumbnail of {}: the game took too long to make one", in_flight_->key);
    if (entry != entries_.end()) entry->second.failed = in_flight_->hash;
    in_flight_.reset();
    return;
  }
  const bool made = !capture.rgba.empty() && capture.width > 0 && capture.height > 0 && capture.rgba.size() >= static_cast<size_t>(capture.width) * capture.height * 4;
  bool seen = false;
  for (size_t alpha = 3; made && !seen && alpha < capture.rgba.size(); alpha += 4) seen = capture.rgba[alpha] != 0;
  if (!made) {
    RF_WARN("content browser: no thumbnail of {}: {}", in_flight_->key, capture.message.empty() ? std::string("the game made no picture") : capture.message);
    if (entry != entries_.end()) entry->second.failed = in_flight_->hash;
  } else if (entry != entries_.end()) {
    // A picture of nothing (a class whose avatar shows nothing of the game's) keeps its icon.
    Store(entry->second.path, in_flight_->hash, seen ? &capture : nullptr);
    if (!seen) entry->second.failed = in_flight_->hash;
    // Shown when it's still of the asset as it is.
    else if (entry->second.hash == in_flight_->hash) Show(entry->second, in_flight_->hash, capture.width, capture.height, capture.rgba);
  }
  in_flight_.reset();
}

void Thumbnails::StartNext() {
  if (in_flight_ || unavailable_) return;
  while (!queue_.empty()) {
    const std::string key = std::move(queue_.front());
    queue_.pop_front();
    const auto found = entries_.find(key);
    if (found == entries_.end()) continue;
    Entry& entry = found->second;
    entry.queued = false;
    // Only one still shown and still to be made.
    const bool wanted = entry.known && entry.source == Source::kRendered && entry.texture_hash != entry.hash && entry.failed != entry.hash;
    if (!wanted || frame_ - entry.used > kWantedFrames) continue;
    auto pending = std::make_shared<Pending>();
    const uint64_t viewport = content::NewViewport();
    const auto done = [pending](content::ViewportCapture capture) {
      std::lock_guard lock(pending->mutex);
      pending->capture = std::move(capture);
      pending->done = true;
    };
    if (!content::CaptureViewport(viewport, entry.view, entry.seconds, done)) {
      // The game makes no pictures: its tiles keep their icons.
      unavailable_ = true;
      queue_.clear();
      return;
    }
    in_flight_ = InFlight{.key = key, .hash = entry.hash, .viewport = viewport, .started = std::chrono::steady_clock::now(), .pending = std::move(pending)};
    return;
  }
}

void Thumbnails::Store(const std::string& path, uint64_t hash, const content::ViewportCapture* capture) {
  ModIndex& index = IndexOf(content::ModOf(path));
  const std::string key = Lower(InMod(path));
  index.made.erase(key);
  index.empty.erase(key);
  if (!capture) {
    index.empty[key] = hash;
    WriteIndex(content::ModOf(path));
    return;
  }
  const std::vector<uint8_t> png = EncodePngRGBA(capture->rgba.data(), static_cast<int>(capture->width), static_cast<int>(capture->height));
  const std::filesystem::path file = PngFile(path);
  std::error_code code;
  std::filesystem::create_directories(file.parent_path(), code);
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  if (png.empty() || !out) {
    RF_WARN("content browser: couldn't write the thumbnail {}", file.string());
    return;
  }
  index.made[key] = hash;
  WriteIndex(content::ModOf(path));
}

std::filesystem::path Thumbnails::PngFile(std::string_view path) const { return root_ / std::string(content::ModOf(path)) / (std::string(InMod(path)) + ".png"); }

Thumbnails::ModIndex& Thumbnails::IndexOf(std::string_view mod) {
  auto [at, added] = index_.try_emplace(Lower(mod));
  if (!added) return at->second;
  const std::vector<uint8_t> bytes = ReadFile(root_ / std::string(mod) / kIndexFile);
  const nlohmann::json json = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
  if (!json.is_object()) return at->second;
  const auto read = [&json](const char* name, std::map<std::string, uint64_t>& into) {
    const auto found = json.find(name);
    if (found == json.end() || !found->is_object()) return;
    for (const auto& [path, hash] : found->items()) {
      if (!hash.is_string()) continue;
      try {
        into[Lower(path)] = std::stoull(hash.get<std::string>(), nullptr, 16);
      } catch (const std::exception&) {
        // A hash that can't be read is a picture to make again.
      }
    }
  };
  read(kIndexKey, at->second.made);
  read(kEmptyKey, at->second.empty);
  return at->second;
}

void Thumbnails::WriteIndex(std::string_view mod) {
  const ModIndex& index = IndexOf(mod);
  const auto written = [](const std::map<std::string, uint64_t>& hashes) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    for (const auto& [path, hash] : hashes) {
      char text[17];
      std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
      out[path] = text;
    }
    return out;
  };
  nlohmann::ordered_json json = nlohmann::ordered_json::object();
  json[kIndexKey] = written(index.made);
  json[kEmptyKey] = written(index.empty);
  const std::filesystem::path file = root_ / std::string(mod) / kIndexFile;
  std::error_code code;
  std::filesystem::create_directories(file.parent_path(), code);
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << json.dump(2) << "\n";
}

}  // namespace rexfabric::ui
