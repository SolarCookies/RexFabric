#include "guide_menu.h"

#include <algorithm>
#include <string>
#include <vector>

#include <rexglue_guide/guide.h>

#include "../platform.h"

namespace rexfabric::ui::guide {
namespace {

using rexglue_guide::Menu;

constexpr std::string_view kBladeId = "rexfabric_mods";
constexpr std::string_view kWindowId = "rexfabric_mods_window";

MenuModel* model = nullptr;

std::string Detail(const ModInfo& mod) {
  std::string detail = mod.version.empty() ? std::string() : "v" + mod.version;
  if (!mod.author.empty()) detail += (detail.empty() ? "by " : " by ") + mod.author;
  return detail;
}

const ModInfo* FindMod(const std::string& folder, size_t* index = nullptr) {
  const std::vector<ModInfo>& mods = model->mods().mods();
  const auto it = std::find_if(mods.begin(), mods.end(), [&](const ModInfo& mod) { return SameModName(mod.folder, folder); });
  if (it == mods.end()) return nullptr;
  if (index) *index = static_cast<size_t>(it - mods.begin());
  return &*it;
}

void RestartPage(Menu& menu) {
  if (menu.Button("Restart Now")) model->Restart();
  if (menu.Button("Not Now")) menu.Back();
}

// A page of its own, so a press meant for the row a page opens on never restarts the game.
void RestartRow(Menu& menu) {
  if (model->restart_needed()) menu.Page("Restart to Apply Changes", RestartPage);
}

void ModPage(Menu& menu, const std::string& folder) {
  size_t index = 0;
  const ModInfo* mod = FindMod(folder, &index);
  if (!mod) {
    menu.Back();
    return;
  }
  if (rex::ui::ImmediateTexture* icon = model->ModIcon(*mod)) menu.Image(icon, 1.0f, 200.0f);
  menu.Text(Detail(*mod), Menu::Tone::kDim);
  menu.Text(mod->description);
  RestartRow(menu);
  bool enabled = mod->enabled;
  if (menu.Toggle("Enabled", &enabled)) {
    model->SetEnabled(index, enabled);
    return;
  }
  const size_t count = model->mods().EnabledCount();
  int position = static_cast<int>(index) + 1;
  if (enabled && count > 1 && menu.Number("Load Order", &position, 1, static_cast<int>(count))) model->Move(index, position - 1 - static_cast<int>(index));
  if (menu.Button("Open Folder")) OpenFolder(mod->dir);
}

Menu::Look ModLook(const ModInfo& mod) {
  return {.sub = Detail(mod), .icon = [mod](bool*) { return model->ModIcon(mod); }, .icon_dim = !mod.enabled};
}

void PackagePage(Menu& menu, const net::Package& package) {
  net::Thunderstore& store = *model->thunderstore();
  const net::Thunderstore::Details& details = store.GetDetails(package);
  const bool ready = details.status == net::Status::kReady;
  if (rex::ui::ImmediateTexture* icon = model->PackageIcon(package)) menu.Image(icon, 1.0f, 200.0f);
  menu.Text("by " + package.owner, Menu::Tone::kDim);
  menu.Text(std::to_string(package.downloads) + " downloads, " + std::to_string(package.likes) + " likes", Menu::Tone::kDim);
  if (ready && !details.version.empty()) menu.Text("Version " + details.version + (details.updated.empty() ? "" : ", updated " + details.updated), Menu::Tone::kDim);
  if (package.deprecated) menu.Text("Deprecated", Menu::Tone::kError);
  menu.Text(package.description);

  const net::Thunderstore::Install* install = store.GetInstall(package);
  const ModInfo* installed = model->mods().Find(package.name);
  RestartRow(menu);
  if (installed) {
    const std::string folder = installed->folder;
    menu.Page("Manage", [folder](Menu& sub) { ModPage(sub, folder); }, installed->enabled ? "Enabled" : "Disabled");
  }
  const bool busy = install && install->status == net::Status::kLoading;
  if (!busy && install && install->status == net::Status::kError) menu.Text(install->error, Menu::Tone::kError);
  // One row through installing and after, under one id, so the selection stays on it.
  const bool outdated = installed && ready && !details.version.empty() && installed->version != details.version;
  std::string action = !installed ? "Install" : outdated ? "Update" : "Reinstall";
  if (busy) action = install->step.empty() ? "Installing" : install->step;
  if (menu.Button(action + "###install", Menu::Look{.detail = outdated && !busy ? "v" + details.version : std::string(), .disabled = busy})) model->Install(package);
  if (menu.Button("View on Thunderstore")) OpenUrl(package.page_url);

  if (!ready || details.dependencies.empty()) return;
  menu.Header("Requires");
  for (const net::Thunderstore::Dependency& dependency : details.dependencies) {
    const bool have = model->IsInstalled(dependency.name);
    menu.Text(dependency.name + " by " + dependency.owner + (have ? ", installed" : ""), have ? Menu::Tone::kDim : Menu::Tone::kNormal);
  }
}

void FilterPage(Menu& menu) {
  net::Query& query = model->query();
  std::vector<std::string> orderings;
  int ordering = 0;
  for (const net::Ordering& option : net::kOrderings) {
    if (query.ordering == option.value) ordering = static_cast<int>(orderings.size());
    orderings.emplace_back(option.label);
  }
  if (menu.Choice("Sort", &ordering, orderings)) {
    query.ordering = net::kOrderings[ordering].value;
    query.page = 1;
  }
  const std::vector<net::Category>& categories = model->thunderstore()->Categories();
  std::vector<std::string> category_names = {"All categories"};
  int category = 0;
  for (const net::Category& option : categories) {
    if (option.id == query.category_id) category = static_cast<int>(category_names.size());
    category_names.push_back(option.name);
  }
  if (menu.Choice("Category", &category, category_names)) {
    query.category_id = category == 0 ? std::string() : categories[static_cast<size_t>(category - 1)].id;
    query.page = 1;
  }
  if (menu.Toggle("Show Deprecated", &query.deprecated)) query.page = 1;
}

// What the filter is set to, for its row.
std::string FilterSummary() {
  const net::Query& query = model->query();
  std::string summary;
  for (const net::Ordering& option : net::kOrderings) {
    if (query.ordering == option.value) summary = option.label;
  }
  for (const net::Category& category : model->thunderstore()->Categories()) {
    if (category.id == query.category_id) summary += ", " + category.name;
  }
  return summary;
}

std::string PackageState(const net::Package& package) {
  const net::Thunderstore::Install* install = model->thunderstore()->GetInstall(package);
  if (install && install->status == net::Status::kLoading) return "Installing";
  if (install && install->status == net::Status::kError) return "Failed";
  return model->IsInstalled(package.name) ? "Installed" : std::string();
}

void BrowsePage(Menu& menu) {
  net::Thunderstore& store = *model->thunderstore();
  net::Query& query = model->query();

  if (menu.TextField("Search", &query.search, 64)) query.page = 1;
  menu.Page("Filter", FilterPage, FilterSummary());

  const net::Thunderstore::Page& page = store.GetPage(query);
  if (page.status == net::Status::kError) {
    menu.Text(page.error.empty() ? "Could not load mods" : page.error, Menu::Tone::kError);
    if (menu.Button("Try Again")) store.Invalidate();
    return;
  }
  if (page.status != net::Status::kReady) {
    menu.Text("Loading", Menu::Tone::kDim);
    return;
  }
  if (page.packages.empty()) {
    menu.Text("No mods found", Menu::Tone::kDim);
    return;
  }
  menu.Header(std::to_string(page.total) + (page.total == 1 ? " mod" : " mods"));
  for (const net::Package& package : page.packages) {
    const Menu::Look look{.detail = PackageState(package), .sub = "by " + package.owner, .icon = [package](bool* loading) { return model->PackageIcon(package, loading); }};
    menu.Page(package.name + "##" + package.owner + "/" + package.name, [package](Menu& sub) { PackagePage(sub, package); }, look);
  }
  const int pages = std::max(1, static_cast<int>((page.total + net::Thunderstore::kPageSize - 1) / net::Thunderstore::kPageSize));
  if (pages == 1) return;
  // Both always there, so paging keeps the selection where it is.
  menu.Header("Page " + std::to_string(query.page) + " of " + std::to_string(pages));
  if (menu.Button("Previous Page", Menu::Look{.disabled = query.page <= 1})) --query.page;
  if (menu.Button("Next Page", Menu::Look{.disabled = query.page >= pages})) ++query.page;
}

void Build(Menu& menu) {
  model->Poll();
  for (const net::Thunderstore::Failure& failure : model->failures()) menu.Text("Couldn't install " + failure.name + ": " + failure.error, Menu::Tone::kError);
  if (!model->failures().empty() && menu.Button("Dismiss")) model->DismissFailures();
  RestartRow(menu);
  if (net::Thunderstore* store = model->thunderstore()) menu.Page("Browse Mods", BrowsePage, store->Installing() > 0 ? "Installing" : "");
  if (menu.Button("Reload Scripts")) model->ReloadScripts();
  if (menu.Button("Open Mods Folder")) model->OpenModsFolder();

  const std::vector<ModInfo>& mods = model->mods().mods();
  const size_t enabled = model->mods().EnabledCount();
  menu.Header("Enabled");
  if (enabled == 0) menu.Text("No mods enabled", Menu::Tone::kDim);
  for (size_t i = 0; i < mods.size(); ++i) {
    if (i == enabled) menu.Header("Available");
    const std::string folder = mods[i].folder;
    menu.Page(mods[i].name + "##" + folder, [folder](Menu& sub) { ModPage(sub, folder); }, ModLook(mods[i]));
  }
}

}  // namespace

void AddMenus(MenuModel& menu_model) {
  model = &menu_model;
  rexglue_guide::AddMenu({.id = std::string(kBladeId), .title = "Mods", .place = rexglue_guide::MenuPlace::kRightBlade, .build = Build});
  rexglue_guide::AddMenu({.id = std::string(kWindowId), .title = "Mods", .place = rexglue_guide::MenuPlace::kDetached, .window = {.width = 640.0f, .height = 760.0f, .hold_input = true}, .build = Build});
}

void Toggle() { rexglue_guide::ToggleMenu(kWindowId); }
void Show(bool show) { rexglue_guide::ShowMenu(kWindowId, show); }
bool Shown() { return rexglue_guide::MenuShown(kWindowId) || rexglue_guide::MenuShown(kBladeId); }

}  // namespace rexfabric::ui::guide
