// The game's viewport renderer (rexfabric/content.h, SetViewportRenderer), as the Avatar editor's viewports reach it.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include <rexfabric/content.h>

namespace rexfabric::content {

// A number for a new viewport, never 0 and never given twice.
uint64_t NewViewport();

// What the renderer drew last for `viewport`, asked as `view` says; none when no renderer is registered. UI thread.
std::optional<ViewportImage> ShowViewport(uint64_t viewport, const ViewportView& view);

// The viewport is gone: the renderer may let go of what it made for it.
void CloseViewport(uint64_t viewport);

// Asks the renderer for a picture of `view` (ViewportRenderer::capture), `done` called on any thread once it has it.
// False, and `done` never called, when there's no renderer or it makes no pictures. UI thread.
bool CaptureViewport(uint64_t viewport, const ViewportView& view, double seconds, std::function<void(ViewportCapture)> done);

}  // namespace rexfabric::content
