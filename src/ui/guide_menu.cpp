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

void ModPage(Menu& menu, const std::string& folder) {
  const std::vector<ModInfo>& mods = model->mods().mods();
  const auto it = std::find_if(mods.begin(), mods.end(), [&](const ModInfo& mod) { return SameModName(mod.folder, folder); });
  if (it == mods.end()) {
    menu.Back();
    return;
  }
  const size_t index = static_cast<size_t>(it - mods.begin());
  if (rex::ui::ImmediateTexture* icon = model->ModIcon(*it)) menu.Image(icon, 1.0f, 200.0f);
  if (!Detail(*it).empty()) menu.Text(Detail(*it), Menu::Tone::kDim);
  if (!it->description.empty()) menu.Text(it->description);
  bool enabled = it->enabled;
  if (menu.Toggle("Enabled", &enabled)) {
    model->SetEnabled(index, enabled);
    return;
  }
  if (!enabled) return;
  if (index > 0 && menu.Button("Load Earlier")) model->Move(index, -1);
  else if (index + 1 < model->mods().EnabledCount() && menu.Button("Load Later")) model->Move(index, 1);
}

void PackagePage(Menu& menu, const net::Package& package) {
  net::Thunderstore& store = *model->thunderstore();
  if (rex::ui::ImmediateTexture* icon = model->PackageIcon(package)) menu.Image(icon, 1.0f, 200.0f);
  menu.Header(package.name);
  menu.Text("by " + package.owner, Menu::Tone::kDim);
  menu.Text(std::to_string(package.downloads) + " downloads, " + std::to_string(package.likes) + " likes", Menu::Tone::kDim);
  if (package.deprecated) menu.Text("Deprecated", Menu::Tone::kError);
  if (!package.description.empty()) menu.Text(package.description);
  const net::Thunderstore::Install* install = store.GetInstall(package);
  if (install && install->status == net::Status::kLoading) {
    menu.Text("Installing", Menu::Tone::kDim);
  } else {
    if (install && install->status == net::Status::kError) menu.Text(install->error, Menu::Tone::kError);
    if (menu.Button(model->IsInstalled(package.name) ? "Reinstall" : "Install")) model->Install(package);
  }
  if (menu.Button("View on Thunderstore")) OpenUrl(package.page_url);
}

void BrowsePage(Menu& menu) {
  net::Thunderstore& store = *model->thunderstore();
  net::Query& query = model->query();

  if (menu.TextField("Search", &query.search, 64)) query.page = 1;
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
  const std::vector<net::Category>& categories = store.Categories();
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
  if (menu.Toggle("Deprecated", &query.deprecated)) query.page = 1;

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
  const int pages = std::max(1, static_cast<int>((page.total + net::Thunderstore::kPageSize - 1) / net::Thunderstore::kPageSize));
  menu.Header("Page " + std::to_string(query.page) + " of " + std::to_string(pages));
  for (const net::Package& package : page.packages) {
    const std::string detail = model->IsInstalled(package.name) ? "Installed" : "by " + package.owner;
    menu.Page(package.name + "##" + package.owner + "/" + package.name, [package](Menu& sub) { PackagePage(sub, package); }, detail);
  }
  if (query.page < pages && menu.Button("Next Page")) ++query.page;
  if (query.page > 1 && menu.Button("Previous Page")) --query.page;
}

void Build(Menu& menu) {
  model->Poll();
  if (model->restart_needed() && menu.Button("Restart to Apply Changes")) model->Restart();
  if (model->thunderstore()) menu.Page("Browse Mods", BrowsePage);
  const std::vector<ModInfo>& mods = model->mods().mods();
  const size_t enabled = model->mods().EnabledCount();
  menu.Header("Enabled");
  if (enabled == 0) menu.Text("No mods enabled", Menu::Tone::kDim);
  for (size_t i = 0; i < mods.size(); ++i) {
    if (i == enabled) menu.Header("Available");
    const std::string folder = mods[i].folder;
    menu.Page(mods[i].name + "##" + folder, [folder](Menu& sub) { ModPage(sub, folder); }, Detail(mods[i]));
  }
  if (menu.Button("Reload Scripts")) model->ReloadScripts();
  if (menu.Button("Open Mods Folder")) model->OpenModsFolder();
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
