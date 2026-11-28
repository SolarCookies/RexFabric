// The Avatar editor's viewport (docs/content_and_classes.md, The viewport): one avatar of the class, drawn by the game's
// viewport renderer (rexfabric/content.h), with the class's components over it. Each component with a place is a
// marker, its name beside it; the selected one of the class's own has ImGuizmo's gizmo, which moves, turns or scales
// it, its transform changing as it's dragged.
//
//   drag            go round the avatar (left or right button, away from the gizmo)
//   wheel           nearer and further
//   click a marker  select its component
//   W, E, R         move, turn, scale, while the pointer is over it
//   Play, Replay    its time running or held, and everything from the start (an emitter's play)
//
// The game is given the components too, with their places and values, to show what they do. The game draws with its
// own camera and says how it looks at the avatar, so the gizmos line up with what it drew. With
// no renderer, or one that can't say, the viewport keeps a camera of its own over a plain background and a grid.
//
// A game asset type's viewport (docs/content_and_classes.md, Asset viewports) is the same viewport with playback
// controls in place of the component tools: Play and Pause, Restart, Loop and how long it's played.
//
// Given where to keep it, the toolbar has Capture Thumbnail, which keeps the camera (and an asset viewport's play time)
// as the asset's thumbnail framing (content/thumbnail_view.h), and Clear, which goes back to the default framing.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>
#include <rexfabric/content.h>

#include "../content/assets.h"
#include "../content/components.h"
#include "../content/thumbnail_view.h"

namespace rexfabric::ui {

// Where the camera starts, and a thumbnail is framed from without a framing of its own: a little round from the front
// and above, at the game's own framing.
inline constexpr double kViewportStartYaw = 25.0;
inline constexpr double kViewportStartPitch = 12.0;

// The values a class's avatar is shown with: each inherited property at the class's value for it, then its own.
content::AvatarValues ClassViewValues(const std::vector<content::Property>& inherited, const content::ClassAsset& asset);
// The components as the game is given them (ViewportView::parts): the inherited, then the class's own, each where the
// avatar puts it with its values.
std::vector<content::ViewportPart> ViewportParts(const std::vector<content::Component>& inherited, const std::vector<content::Component>& own);

// What a viewport's toolbar offers beside Play, Pause and its Replay.
struct ViewportControls {
  // The component tools: Move, Turn, Scale and the gizmo's axes, for a viewport whose components are edited in it.
  bool gizmos = true;
  // Playback, for a viewport playing something through (an asset's effect): Replay named Restart, a Loop toggle
  // (ViewportView::looping) and a readout of how long it's played (ViewportImage::time).
  bool playback = false;
  bool looping = true;  // where Loop starts
};

class AvatarViewport {
 public:
  explicit AvatarViewport(ViewportControls controls = {});
  ~AvatarViewport();
  AvatarViewport(const AvatarViewport&) = delete;
  AvatarViewport& operator=(const AvatarViewport&) = delete;

  // Draws it into `size`: the game's image of `view` (its camera filled in here, its own parts kept before the
  // components'), the inherited components fixed, and `own` movable. `selected` names the component with the gizmo,
  // empty for none, and changes when a marker is clicked. True when the gizmo changed one of `own`. With `thumbnail`,
  // the asset's thumbnail framing, the toolbar's Capture Thumbnail sets it to the camera as it is and Clear takes it out.
  bool Draw(const ImVec2& size, content::ViewportView view, const std::vector<content::Component>& inherited, std::vector<content::Component>& own, std::string& selected, std::optional<content::ThumbnailView>* thumbnail = nullptr);
  // The gizmo is being dragged: what it changes is one edit, done when it's let go.
  bool held() const { return held_; }
  // Everything from the start, as Replay (or Restart) does: for an editor whose asset changed what it shows.
  void Restart();

 private:
  enum class Tool { kMove, kTurn, kScale };

  void DrawToolbar(std::optional<content::ThumbnailView>* thumbnail);
  void TakeCamera(bool hovered, bool dragging);

  uint64_t id_;
  double yaw_;
  double pitch_;
  double zoom_;
  ViewportControls controls_;
  Tool tool_ = Tool::kMove;
  bool local_ = true;      // the gizmo along the component's own axes, not the avatar's
  bool dragging_ = false;  // the camera is being dragged round
  bool held_ = false;
  bool playing_ = true;
  bool looping_ = true;
  uint32_t replays_ = 0;
  // How long it's played since it last started: as the game said last, or counted here while it doesn't say.
  double time_ = 0.0;
  // Its own camera's framing (with no camera from the game): the middle of the components and how far they reach.
  std::array<float, 3> own_centre_{};
  float own_radius_ = 1.0f;
};

}  // namespace rexfabric::ui
