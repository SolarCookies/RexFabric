#include "thumbnail_view.h"

#include <cmath>
#include <string>

namespace rexfabric::content {
namespace {

constexpr const char* kYaw = "yaw";
constexpr const char* kPitch = "pitch";
constexpr const char* kZoom = "zoom";
constexpr const char* kTime = "time";

// A value kept to what a file reads well: three decimals.
double Tidy(double value) {
  const double tidy = std::round(value * 1000.0) / 1000.0;
  return tidy == 0.0 ? 0.0 : tidy;
}

}  // namespace

std::optional<ThumbnailView> ReadThumbnailView(const nlohmann::json& file) {
  if (!file.is_object()) return std::nullopt;
  const auto found = file.find(std::string(kThumbnailViewKey));
  if (found == file.end() || !found->is_object()) return std::nullopt;
  const nlohmann::json& stored = *found;
  const auto number = [&stored](const char* key) -> std::optional<double> {
    const auto value = stored.find(key);
    return value != stored.end() && value->is_number() ? std::optional<double>(value->get<double>()) : std::nullopt;
  };
  const std::optional<double> yaw = number(kYaw), pitch = number(kPitch), zoom = number(kZoom);
  if (!yaw || !pitch || !zoom || *zoom <= 0.0) return std::nullopt;
  return ThumbnailView{.yaw = *yaw, .pitch = *pitch, .zoom = *zoom, .time = number(kTime)};
}

void WriteThumbnailView(nlohmann::json& file, const std::optional<ThumbnailView>& view) {
  if (!file.is_object()) return;
  if (!view) {
    file.erase(std::string(kThumbnailViewKey));
    return;
  }
  nlohmann::json stored{{kYaw, Tidy(view->yaw)}, {kPitch, Tidy(view->pitch)}, {kZoom, Tidy(view->zoom)}};
  if (view->time) stored[kTime] = Tidy(*view->time);
  file[std::string(kThumbnailViewKey)] = std::move(stored);
}

}  // namespace rexfabric::content
