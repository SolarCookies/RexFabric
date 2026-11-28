#include "game_asset_editor.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "../content/game_assets.h"
#include "avatar_viewport.h"
#include "editor_parts.h"
#include "glass.h"
#include "thumbnails.h"
#include "widgets.h"

#if REXFABRIC_WITH_NODE_EDITOR
#include "../graph/graph_document.h"
#include "../graph/node_library.h"
#include "own_graph.h"
#endif

namespace rexfabric::ui {
namespace {

using content::AssetField;
using content::AssetType;

// An asset with a graph is laid out as a blueprint (own_graph.h), in a window this much wider than most.
constexpr float kGraphWidthFactor = 1.4f;
// The preview's side, in multiples of the font size, at most, beside the fields of an asset without a graph; and the
// thumbnail's, under its field.
constexpr float kPreviewSize = 14.0f;
constexpr float kThumbnailSize = 5.0f;

std::string Lower(std::string_view text) {
  std::string lower(text);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower;
}

bool Same(std::string_view a, std::string_view b) { return Lower(a) == Lower(b); }

// The model of the editor whose type's own editor is being drawn, which makes its pictures (content::EditorImage);
// null outside one. The UI thread's alone.
MenuModel* g_drawing = nullptr;

// Sets g_drawing while it's in scope.
class Drawing {
 public:
  explicit Drawing(MenuModel& model) : before_(std::exchange(g_drawing, &model)) {}
  ~Drawing() { g_drawing = before_; }
  Drawing(const Drawing&) = delete;
  Drawing& operator=(const Drawing&) = delete;

 private:
  MenuModel* before_;
};

// The asset with each field's value under the field's own name (files may write it in another case), and each field
// shown and missing at its default.
nlohmann::json Completed(const AssetType& type, nlohmann::json json) {
  for (const AssetField& field : type.fields) {
    for (const auto& [key, value] : nlohmann::json(json).items()) {
      if (key != field.name() && Same(key, field.name())) {
        json[field.name()] = value;
        json.erase(key);
      }
    }
  }
  for (const AssetField* field : content::ShownFields(type, json)) {
    if (!json.contains(field->name())) json[field->name()] = field->property.value;
  }
  return json;
}

// What the editor edits and undoes as one: the asset's fields, and its own graph as its file keeps it (empty for a
// type without graphs).
struct AssetDocument {
  nlohmann::json asset;
  std::string graph;
  bool operator==(const AssetDocument&) const = default;
};

class GameAssetEditor final : public DocumentEditor<AssetDocument> {
 public:
  GameAssetEditor(EditorHost& host, std::string path, std::shared_ptr<const AssetType> type, nlohmann::json json) : DocumentEditor(host, std::move(path), {.asset = Completed(*type, std::move(json))}), type_(std::move(type)) {
#if REXFABRIC_WITH_NODE_EDITOR
    if (type_->HasGraph()) OpenGraph();
#endif
  }

  bool Save() override {
    const std::optional<std::filesystem::path> file = File();
    std::string error;
    if (!file || !content::WriteGameAsset(*file, *type_, document_.asset, &error)) {
      if (file) SetStatus(error, true);
      return false;
    }
    const bool graph_saved = SaveGraph(&error);
    if (!error.empty()) {
      SetStatus(error, true);
      return false;
    }
    MarkSaved();
    host_.Saved(path_);
    // Its graph's script runs from now, as the node editor's Save and Reload has it.
    if (graph_saved) host_.model().ReloadScripts();
    SavedStatus(!Problems().empty());
    return true;
  }

 protected:
  std::string Title() const override { return std::string(content::NameOf(path_)) + " (" + type_->label + ")"; }
  float WidthFactor() const override { return HasGraph() ? kGraphWidthFactor : type_->viewport ? kAvatarEditorWidth : 1.0f; }
  bool HoldsCanvas() const override { return HasGraph(); }

  void DrawContents() override {
    if (!HasGraph()) {
      DrawToolbar(CanUndo(), CanRedo());
      if (type_->viewport) DrawBesideViewport();
      else DrawFields();
      Remember();
      return;
    }
#if REXFABRIC_WITH_NODE_EDITOR
    // An undo of the editor's may have taken the graph somewhere else; then the canvas's light, the toolbar over it, and
    // the blueprint's tabs and panes.
    graph_->Follow(document_.graph);
    graph_->DrawBackdrop();
    DrawToolbar(CanUndo(), CanRedo());
    graph_->Draw([this] { DrawViewportTab(); }, [this] { DrawFields(); }, std::string(content::NameOf(path_)) + ".graph.lua, beside it");
    // Before the history keeps the edit, so a field's edit and the field events it changes are one step.
    FollowFieldEvents();
    if (!graph_->moving()) Remember();
#endif
  }

 private:
  // An asset without a graph whose type has a viewport, laid out as a material is: the viewport on the left, the
  // fields beside it.
  void DrawBesideViewport() {
    if (!ImGui::BeginTable("##game_asset_editor", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail())) return;
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::TableSetupColumn("##viewport", ImGuiTableColumnFlags_WidthFixed, std::max(width * kViewportShare, ImGui::GetFontSize() * kViewportMinWidth));
    ImGui::TableSetupColumn("##body", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    DrawViewport();
    ImGui::TableNextColumn();
    ImGui::BeginChild("##body");
    DrawFields();
    ImGui::EndChild();
    ImGui::EndTable();
  }

  // The type's viewport (AssetTypeDecl::viewport): the scene it builds of the asset as it is now, played by the game's
  // viewport renderer with the playback controls over it.
  void DrawViewport() {
    const content::AssetViewportDecl& decl = *type_->viewport;
    if (!viewport_) viewport_ = std::make_unique<AvatarViewport>(ViewportControls{.gizmos = false, .playback = true, .looping = decl.looping});
    BuildScene(decl);
    if (!scene_problem_.empty()) ImGui::TextColored(widgets::kWarningColor, "%s", scene_problem_.c_str());
    content::ViewportView view{.backing = decl.backing, .values = scene_.values, .parts = scene_.parts, .asset = path_};
    // Its thumbnail framing, kept beside its fields; written back only when the toolbar changed it.
    const std::optional<content::ThumbnailView> stored = content::ReadThumbnailView(document_.asset);
    std::optional<content::ThumbnailView> thumbnail = stored;
    viewport_->Draw(ImGui::GetContentRegionAvail(), std::move(view), no_components_, no_components_, selected_, &thumbnail);
    if (thumbnail != stored) content::WriteThumbnailView(document_.asset, thumbnail);
  }

  // The viewport's scene built again from the fields when they've changed since it was last built; a scene unlike the
  // last one starts the viewport over.
  void BuildScene(const content::AssetViewportDecl& decl) {
    std::string json = content::FittedValues(host_.Types(), *type_, document_.asset).dump();
    if (built_from_ == json) return;
    content::AssetViewportScene scene;
    scene_problem_.clear();
    if (decl.build) {
      try {
        scene = decl.build(path_, json);
      } catch (const std::exception& error) {
        scene_problem_ = error.what();
      }
    }
    if (built_from_ && scene != scene_) viewport_->Restart();
    built_from_ = std::move(json);
    scene_ = std::move(scene);
  }

  // The fields, the type's own editor and what's wrong; for an asset without a graph, its preview first.
  void DrawFields() {
    if (!HasGraph() && type_->preview) DrawPreview(std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * kPreviewSize));
    const TypeChoices choices = host_.Choices(content::ModOf(path_));
    const std::vector<const AssetField*> before = content::ShownFields(*type_, document_.asset);
    bool changed = false;
    if (BeginLabelTable("##game_asset")) {
      for (const AssetField* field : before) {
        if (field->custom && type_->editor) continue;
        LabelRow(field->Label().c_str());
        changed |= ValueField(("##" + field->name()).c_str(), field->property.type, document_.asset[field->name()], host_.Types(), choices);
        if (!field->property.help.empty()) ImGui::SetItemTooltip("%s", field->property.help.c_str());
        // Under RexFabric's thumbnail field, the thumbnail the content browser shows of it.
        if (field->own && host_.thumbnails()) host_.thumbnails()->Item(path_, ImGui::GetFontSize() * kThumbnailSize);
      }
      ImGui::EndTable();
    }
    changed |= DrawOwnEditor();
    if (changed) StartShownOver(before);
    for (const std::string& problem : Problems()) ImGui::TextColored(widgets::kErrorColor, "%s", problem.c_str());
    if (type_->warn) {
      for (const std::string& warning : type_->warn(content::FittedValues(host_.Types(), *type_, document_.asset).dump())) ImGui::TextColored(widgets::kWarningColor, "%s", warning.c_str());
    }
  }

  // The type's own editor (AssetTypeDecl::editor), below the table: the asset's fields as JSON text, taken back when it
  // says it changed them. True when it did.
  bool DrawOwnEditor() {
    if (!type_->editor) return false;
    std::string json = document_.asset.dump();
    bool changed = false;
    ImGui::PushID("##own_editor");
    {
      const Drawing drawing(host_.model());
      changed = type_->editor(path_, json);
    }
    ImGui::PopID();
    if (!changed) return false;
    nlohmann::json edited = nlohmann::json::parse(json, nullptr, false);
    if (!edited.is_object()) {
      SetStatus("Its type's editor gave back fields that aren't a JSON object", true);
      return false;
    }
    document_.asset = std::move(edited);
    return true;
  }

  // The type's 2D preview of the asset as it is now (AssetTypeDecl::preview): its layers in a square `size` wide.
  void DrawPreview(float size) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + size, at.y + size), ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
    DrawPreviewLayers(draw, host_.model(), type_->preview(content::FittedValues(host_.Types(), *type_, document_.asset).dump()), at, size);
  }

  // After an edit: each field now shown as another of the type's fields than before starts at that field's default, and
  // one shown again keeps its value where it fits.
  void StartShownOver(const std::vector<const AssetField*>& before) {
    nlohmann::json& asset = document_.asset;
    for (const AssetField* field : content::ShownFields(*type_, asset)) {
      const auto was = std::find_if(before.begin(), before.end(), [field](const AssetField* earlier) { return Same(earlier->name(), field->name()); });
      if (was != before.end() && *was == field) continue;
      nlohmann::json fitted;
      std::string why;
      const bool fits = was == before.end() && asset.contains(field->name()) && content::FitValue(host_.Types(), field->property.type, asset[field->name()], &fitted, &why);
      asset[field->name()] = fits ? fitted : field->property.value;
    }
  }

  // What's wrong with it: a value that doesn't fit its field, then what the game's check finds.
  std::vector<std::string> Problems() const {
    std::vector<std::string> problems;
    const nlohmann::json fitted = content::FittedValues(host_.Types(), *type_, document_.asset, &problems);
    if (problems.empty() && type_->check) problems = type_->check(fitted.dump());
    return problems;
  }

#if REXFABRIC_WITH_NODE_EDITOR
  bool HasGraph() const { return graph_ != nullptr; }

  // The asset's own graph beside its file, read, with a node of each of its type's events it lacks and its field events
  // as its fields are now; the editor's history starts from it. A settled edit of the canvas goes into that history
  // with the fields.
  void OpenGraph() {
    const ModInfo* mod = Mod();
    if (mod == nullptr) return;
    std::vector<std::string> events;
    for (const content::AssetEventDecl& event : type_->events) events.push_back(graph::AssetEventNode(event.event));
    graph_ = std::make_unique<OwnGraph>(OwnGraph::Host{.status = [this](std::string text, bool error) { SetStatus(std::move(text), error); }, .edited = [this](std::string text) { document_.graph = std::move(text); }});
    std::string fields = content::FittedValues(host_.Types(), *type_, document_.asset).dump();
    const std::optional<std::vector<content::AssetEventDecl>> field_events = FieldEvents(fields);
    // A type whose field events couldn't be made keeps the nodes the graph has until they can.
    const std::string text = graph_->Open(mod->dir, mod->folder, std::string(content::NameOf(path_)), graph::AssetGraphLocation(mod->dir, content::AssetFile(*mod, path_), path_), events, field_events ? &*field_events : nullptr);
    if (field_events) events_from_ = std::move(fields);
    Reload({.asset = document_.asset, .graph = text});
  }

  // The field events (AssetTypeDecl::asset_events) of the asset's fields as `fields` has them, fitted: none for a type
  // without them, or one whose asset_events threw, which the status says.
  std::optional<std::vector<content::AssetEventDecl>> FieldEvents(const std::string& fields) {
    if (!type_->asset_events) return std::vector<content::AssetEventDecl>{};
    try {
      return type_->asset_events(fields);
    } catch (const std::exception& error) {
      SetStatus(std::string("Its type couldn't say which events its fields give: ") + error.what(), true);
      return std::nullopt;
    }
  }

  // The graph's field events follow the fields once an edit of them settles (not while a value is still being typed, so
  // a name half typed doesn't take a node's links): a node for each event they give now, and the ones they no longer
  // give gone, with what was linked from them left as ghosts.
  void FollowFieldEvents() {
    if (!type_->asset_events || ImGui::IsAnyItemActive()) return;
    std::string fields = content::FittedValues(host_.Types(), *type_, document_.asset).dump();
    if (events_from_ == fields) return;
    const std::optional<std::vector<content::AssetEventDecl>> field_events = FieldEvents(fields);
    events_from_ = std::move(fields);
    if (!field_events) return;
    // An undo this frame took the graph back with the fields: the canvas has it first, so it's that graph that follows.
    graph_->Follow(document_.graph);
    if (std::optional<std::string> text = graph_->FollowFieldEvents(*field_events)) document_.graph = std::move(*text);
  }

  // The Viewport tab's page: the type's viewport, or its preview as large as fits.
  void DrawViewportTab() {
    if (type_->viewport) {
      DrawViewport();
      return;
    }
    const ImVec2 room = ImGui::GetContentRegionAvail();
    const float side = std::min(room.x, room.y);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (room.x - side) * 0.5f);
    if (type_->preview) DrawPreview(side);
    else ImGui::TextDisabled("Its type has no preview");
  }

  // Writes its graph and the script it compiles to beside it; true when it did. `error` says why it couldn't.
  bool SaveGraph(std::string* error) { return graph_ && graph_->Save(error); }

  std::unique_ptr<OwnGraph> graph_;
  // The fitted fields the graph's field events were last made from; none until they first are.
  std::optional<std::string> events_from_;
#else
  bool HasGraph() const { return false; }
  bool SaveGraph(std::string*) { return false; }
#endif

  std::shared_ptr<const AssetType> type_;
  // The type's viewport, made when it's first drawn, and the scene it shows: built from the fields as `built_from_`
  // has them (none until it's first built), with what the build couldn't show.
  std::unique_ptr<AvatarViewport> viewport_;
  std::vector<content::Component> no_components_;  // a game asset's viewport has no components of its own
  std::string selected_;
  std::optional<std::string> built_from_;
  content::AssetViewportScene scene_;
  std::string scene_problem_;
};

}  // namespace

std::unique_ptr<AssetEditor> OpenGameAssetEditor(EditorHost& host, const std::string& path) {
  const ModInfo* mod = host.model().mods().Find(content::ModOf(path));
  std::optional<content::GameAssetFile> asset = mod ? content::ReadGameAsset(content::AssetFile(*mod, path), nullptr) : std::nullopt;
  return asset ? std::make_unique<GameAssetEditor>(host, path, std::move(asset->type), std::move(asset->json)) : nullptr;
}

}  // namespace rexfabric::ui

namespace rexfabric::content {

uint64_t EditorImage(std::string_view key, uint64_t version, uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
  if (ui::g_drawing == nullptr) return 0;
  return static_cast<uint64_t>(ui::g_drawing->CodePicture(std::string(key), version, width, height, rgba));
}

}  // namespace rexfabric::content
