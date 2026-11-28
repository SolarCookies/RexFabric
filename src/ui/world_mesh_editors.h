// The editors of a mod's meshes and worlds (docs/content_and_classes.md, Meshes and Worlds): a mesh's help and the
// model beside it, which Replace takes another in place of; a world's base and help.

#pragma once

#include <memory>
#include <string>

#include "asset_editors.h"

namespace rexfabric::ui {

std::unique_ptr<AssetEditor> OpenMeshEditor(EditorHost& host, const std::string& path);
std::unique_ptr<AssetEditor> OpenWorldEditor(EditorHost& host, const std::string& path);

}  // namespace rexfabric::ui
