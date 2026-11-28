// How an asset's automatic thumbnail is framed (docs/content_and_classes.md, Thumbnails): the camera its viewport had
// when Capture Thumbnail was pressed, and for an asset viewport how long it had played. Kept in the asset's own file
// under "thumbnail_view", at the top level beside its other keys (a class file's and a game asset's alike), so the
// thumbnail anyone makes of it looks the same:
//
//   "thumbnail_view": {"yaw": 40.0, "pitch": 18.0, "zoom": 1.6, "time": 1.25}

#pragma once

#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

namespace rexfabric::content {

inline constexpr std::string_view kThumbnailViewKey = "thumbnail_view";

struct ThumbnailView {
  double yaw = 0.0;
  double pitch = 0.0;
  double zoom = 1.0;
  std::optional<double> time;  // an asset viewport's play time; none for a class's

  bool operator==(const ThumbnailView&) const = default;
};

// The framing a file's top level keeps; none when it keeps none or one that can't be read.
std::optional<ThumbnailView> ReadThumbnailView(const nlohmann::json& file);
// Keeps `view` in a file's top level, or takes the framing out for none.
void WriteThumbnailView(nlohmann::json& file, const std::optional<ThumbnailView>& view);

}  // namespace rexfabric::content
