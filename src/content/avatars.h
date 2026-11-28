// Avatars of mods' classes in the world (docs/content_and_classes.md, Runtime). Spawning one makes its backing where
// it's asked for, then attaches its components in tree order, its ancestors' first; every tick its components tick,
// each where the avatar and the components above it put it; removing it detaches them in reverse order and removes its
// backing. One whose backing is gone from the world (its garden unloaded) is detached and forgotten on the next tick.
//
// Each avatar fires RexFabric's avatar events (content.h, kAvatarSpawned), which its class graph runs: spawned once its
// components are attached, tick each frame after they ticked, and removed before they detach, whether it's removed or
// goes with its world. A reload of the mods' scripts removes the avatars it spawned without the event, as the scripts
// whose graphs would run it are gone; the saved ones it keeps are spawned again for the new scripts (AnnounceAvatars).
//
// An avatar keeps the class as it was when it was spawned: a class saved since changes the next one spawned. Its
// property values start as its class's and can be changed on it alone, for as long as it's there.
//
// Spawning, moving, removing and ticking are for the thread the game runs its backings on; reading what's there may
// be done from any thread.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <rexfabric/content.h>

namespace rexfabric::content {

// An avatar as it is now.
struct AvatarView {
  uint32_t id = 0;
  std::string path;  // its class
  Transform at;
  Object object;  // the game's object its backing is, or none
  bool saved = false;  // saved with its world (SetAvatarSaved)
};

// Spawns an avatar of the class at `at`; its id, or 0 with `why`: a class that isn't an avatar class, or a backing or
// component that refused.
uint32_t SpawnAvatar(std::string_view path, const Transform& at, std::string* why);
// Removes it; false for one that isn't there.
bool RemoveAvatar(uint32_t id);
// Removes every avatar but the saved ones, as a reload of the mods' scripts does: they're gone at once, and detached and
// their backings removed on the next tick, on the game's thread. A saved avatar belongs to its world rather than to the
// script that spawned it, so it stays until it's removed or its world goes. Any thread.
void RemoveAllAvatars();
bool MoveAvatar(uint32_t id, const Transform& to, std::string* why);

std::optional<AvatarView> FindAvatar(uint32_t id);
// The class of an avatar that's there (removed or gone with its world, it isn't), or empty: cheaper than FindAvatar,
// as it asks the game nothing.
std::string ClassOfAvatar(uint32_t id);
// Every avatar of the class or a class below it, every one for an empty path, oldest first.
std::vector<uint32_t> AvatarsOf(std::string_view path);
// The avatar whose backing is the game's object, or 0.
uint32_t AvatarOfObject(const Object& object);

// A property's value on the avatar, or none for an avatar or property it doesn't have.
std::optional<nlohmann::json> AvatarValue(uint32_t id, std::string_view property);
// Sets it on the avatar alone, fitted to the property's type, and has its backing show it; false with `why` when it
// doesn't fit. On the thread the avatars tick on.
bool SetAvatarValue(uint32_t id, std::string_view property, const nlohmann::json& value, std::string* why);

// Whether the avatar went because its backing went with its world (the game left it), rather than being removed.
bool WentWithItsWorld(uint32_t id);

// Whether the avatar is saved with its world (content.h, WorldSaveData): spawned again where it was when the world it's
// in loads from the game's save. False for one that isn't there.
bool SetAvatarSaved(uint32_t id, bool saved);

// A saved avatar as its world's save keeps it: its class, where it is, and the values it has that its class doesn't
// give it, by property, so a class whose defaults change gives the new ones to the avatars that kept its own.
struct SavedAvatar {
  std::string path;
  Transform at;
  nlohmann::json values = nlohmann::json::object();
};

// Every saved avatar, oldest first.
std::vector<SavedAvatar> SavedAvatars();

// Whoever keeps something of its own for each avatar (the Lua VM, the tables of its class graphs) is told here once an
// avatar is gone for good, after its removed event: once per avatar, on the thread it went on. One listener, which a new
// one replaces; an empty one stops it.
void OnAvatarGone(std::function<void(uint32_t id)> listener);

// The avatars there are now are new to the scripts about to load (the scripts are loaded again): each still there on the
// next tick fires its spawned event again then, so the new scripts' class graphs start each one's own. An avatar the
// scripts spawn as they load fires its own. Any thread.
void AnnounceAvatars();

// Ticks every avatar's components, after forgetting the ones whose backing is gone, and fires each one's tick event.
// Each frame, before the frame event.
void TickAvatars(double seconds);

}  // namespace rexfabric::content
