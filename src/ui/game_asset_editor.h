// The editor of an asset of one of the game's asset types (docs/content_and_classes.md, Game asset types): each field
// its type shows for it, as its type and condition say, edited as any value is, with the type's checks below.
//
// A field shows only while its condition holds, so editing the field a condition reads shows other fields. A field shown
// as another of its type's fields than before the edit (a packet's look, a struct of its base's) starts over at that
// field's default, as the old value was of another shape; one shown again keeps its value where it still fits.
//
// A type with an editor of its own (AssetTypeDecl::editor) draws it below the field table, which leaves out the fields
// it marks custom; its edits are the editor's as the table's are. The type's warnings show below its problems.

#pragma once

#include <memory>
#include <string>

#include "asset_editors.h"

namespace rexfabric::ui {

std::unique_ptr<AssetEditor> OpenGameAssetEditor(EditorHost& host, const std::string& path);

}  // namespace rexfabric::ui
