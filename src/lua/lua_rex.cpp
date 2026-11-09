// The `rex` table: hooks, the PPC context, guest memory, calling guest functions, cvars, events and logging.
// The names and behaviour match the Lua mods written for the earlier script runtime, so they run unchanged.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/ppc/func.h>
#include <rex/system/thread_state.h>
#include <rex/version.h>

#include "../events.h"
#include "../guest.h"
#include "../hooks.h"
#include "../log.h"
#include "../ui/screen_text.h"
#include "lua_vm.h"

namespace rexfabric::lua {
namespace {

constexpr const char* kContextMeta = "rex.ppc.context";
constexpr const char* kHookTable = "rexfabric.hooks";       // key -> function
constexpr const char* kEventTable = "rexfabric.events";     // event name -> {functions}
constexpr const char* kWatchTable = "rexfabric.cvarwatch";  // cvar name -> {functions}

// Registers ----------------------------------------------------------------------------------------------

// "r13" or "f7" as an index, or -1.
int RegisterIndex(std::string_view name, char prefix) {
  if (name.size() < 2 || name.size() > 3 || name[0] != prefix) return -1;
  int index = 0;
  const auto [end, error] = std::from_chars(name.data() + 1, name.data() + name.size(), index);
  return error == std::errc() && end == name.data() + name.size() && index <= 31 ? index : -1;
}

PPCContext& CheckContext(lua_State* L, int index) {
  luaL_checkudata(L, index, kContextMeta);
  if (!Vm().ctx) luaL_error(L, "PPC context used outside the hook that produced it (contexts are only valid for the duration of the callback)");
  return *Vm().ctx;
}

int ContextIndex(lua_State* L) {
  PPCContext& ctx = CheckContext(L, 1);
  const std::string_view key = luaL_checkstring(L, 2);
  if (int i = RegisterIndex(key, 'r'); i >= 0) lua_pushinteger(L, (ctx.*guest::kGprs[i]).u32);
  else if (int i = RegisterIndex(key, 'f'); i >= 0) lua_pushnumber(L, (ctx.*guest::kFprs[i]).f64);
  else if (key == "lr") lua_pushinteger(L, static_cast<lua_Integer>(ctx.lr));
  else if (key == "ctr") lua_pushinteger(L, static_cast<lua_Integer>(ctx.ctr.u64));
  else if (key == "address") lua_pushinteger(L, Vm().ctx_address);
  else {
    lua_pushvalue(L, 2);
    lua_rawget(L, lua_upvalueindex(1));
  }
  return 1;
}

int ContextNewIndex(lua_State* L) {
  PPCContext& ctx = CheckContext(L, 1);
  const std::string_view key = luaL_checkstring(L, 2);
  // Writing an integer register zero extends, as 32 bit guest code leaves one.
  if (int i = RegisterIndex(key, 'r'); i >= 0) (ctx.*guest::kGprs[i]).u64 = static_cast<uint32_t>(luaL_checkinteger(L, 3));
  else if (int i = RegisterIndex(key, 'f'); i >= 0) (ctx.*guest::kFprs[i]).f64 = luaL_checknumber(L, 3);
  else if (key == "lr") ctx.lr = static_cast<uint64_t>(luaL_checkinteger(L, 3));
  else if (key == "ctr") ctx.ctr.u64 = static_cast<uint64_t>(luaL_checkinteger(L, 3));
  else return luaL_error(L, "unknown PPC context field '%s'", lua_tostring(L, 2));
  return 0;
}

int CheckRegister(lua_State* L, int arg) {
  const lua_Integer index = luaL_checkinteger(L, arg);
  if (index < 0 || index > 31) luaL_error(L, "register index %d out of range [0, 31]", static_cast<int>(index));
  return static_cast<int>(index);
}

int ContextU64(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>((CheckContext(L, 1).*guest::kGprs[CheckRegister(L, 2)]).u64));
  return 1;
}

int ContextSetU64(lua_State* L) {
  (CheckContext(L, 1).*guest::kGprs[CheckRegister(L, 2)]).u64 = static_cast<uint64_t>(luaL_checkinteger(L, 3));
  return 0;
}

int ContextS32(lua_State* L) {
  lua_pushinteger(L, (CheckContext(L, 1).*guest::kGprs[CheckRegister(L, 2)]).s32);
  return 1;
}

int ContextS64(lua_State* L) {
  lua_pushinteger(L, (CheckContext(L, 1).*guest::kGprs[CheckRegister(L, 2)]).s64);
  return 1;
}

int ContextF32(lua_State* L) {
  lua_pushnumber(L, (CheckContext(L, 1).*guest::kFprs[CheckRegister(L, 2)]).f32);
  return 1;
}

constexpr luaL_Reg kContextMethods[] = {{"u64", ContextU64}, {"set_u64", ContextSetU64}, {"s32", ContextS32}, {"s64", ContextS64}, {"f32", ContextF32}, {nullptr, nullptr}};

// Guest memory -------------------------------------------------------------------------------------------

uint8_t* CheckMemory(lua_State* L, uint32_t address) {
  uint8_t* host = guest::Translate(address);
  if (!host) luaL_error(L, "guest memory is not mapped yet");
  return host;
}

uint32_t CheckAddress(lua_State* L, int arg) {
  const lua_Integer address = luaL_checkinteger(L, arg);
  if (address < 0 || address > 0xFFFFFFFFll) luaL_error(L, "guest address %I out of range", address);
  return static_cast<uint32_t>(address);
}

template <typename T>
int MemRead(lua_State* L) {
  const uint32_t address = CheckAddress(L, 1);
  CheckMemory(L, address);
  if constexpr (std::is_floating_point_v<T>) lua_pushnumber(L, guest::Read<T>(address));
  else lua_pushinteger(L, static_cast<lua_Integer>(guest::Read<T>(address)));
  return 1;
}

template <typename T>
int MemWrite(lua_State* L) {
  const uint32_t address = CheckAddress(L, 1);
  CheckMemory(L, address);
  if constexpr (std::is_floating_point_v<T>) guest::Write<T>(address, static_cast<T>(luaL_checknumber(L, 2)));
  else guest::Write<T>(address, static_cast<T>(luaL_checkinteger(L, 2)));
  return 0;
}

int MemString(lua_State* L) {
  const uint32_t address = CheckAddress(L, 1);
  const lua_Integer max = luaL_optinteger(L, 2, 4096);
  const char* text = reinterpret_cast<const char*>(CheckMemory(L, address));
  lua_Integer length = 0;
  while (length < max && text[length] != '\0') ++length;
  lua_pushlstring(L, text, static_cast<size_t>(length));
  return 1;
}

int MemSetString(lua_State* L) {
  const uint32_t address = CheckAddress(L, 1);
  size_t length = 0;
  const char* text = luaL_checklstring(L, 2, &length);
  std::memcpy(CheckMemory(L, address), text, length + 1);
  return 0;
}

// A NUL terminated UTF-16BE string, as UTF-8. Only the basic plane: guest UI strings stay inside it.
int MemWideString(lua_State* L) {
  const uint32_t address = CheckAddress(L, 1);
  const lua_Integer max = luaL_optinteger(L, 2, 4096);
  CheckMemory(L, address);
  std::string out;
  for (lua_Integer i = 0; i < max; ++i) {
    const uint16_t c = guest::Read<uint16_t>(address + static_cast<uint32_t>(i * 2));
    if (c == 0) break;
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  lua_pushlstring(L, out.data(), out.size());
  return 1;
}

int MemBytes(lua_State* L) {
  const uint32_t address = CheckAddress(L, 1);
  const lua_Integer count = luaL_checkinteger(L, 2);
  if (count < 0 || count > (1 << 24)) return luaL_error(L, "byte count %I out of range", count);
  lua_pushlstring(L, reinterpret_cast<const char*>(CheckMemory(L, address)), static_cast<size_t>(count));
  return 1;
}

int MemSetBytes(lua_State* L) {
  const uint32_t address = CheckAddress(L, 1);
  size_t length = 0;
  const char* bytes = luaL_checklstring(L, 2, &length);
  std::memcpy(CheckMemory(L, address), bytes, length);
  return 0;
}

constexpr luaL_Reg kMemFuncs[] = {
    {"u8", MemRead<uint8_t>}, {"u16", MemRead<uint16_t>}, {"u32", MemRead<uint32_t>}, {"u64", MemRead<uint64_t>},
    {"s8", MemRead<int8_t>}, {"s16", MemRead<int16_t>}, {"s32", MemRead<int32_t>}, {"s64", MemRead<int64_t>},
    {"f32", MemRead<float>}, {"f64", MemRead<double>},
    {"set_u8", MemWrite<uint8_t>}, {"set_u16", MemWrite<uint16_t>}, {"set_u32", MemWrite<uint32_t>}, {"set_u64", MemWrite<uint64_t>},
    {"set_s8", MemWrite<int8_t>}, {"set_s16", MemWrite<int16_t>}, {"set_s32", MemWrite<int32_t>}, {"set_s64", MemWrite<int64_t>},
    {"set_f32", MemWrite<float>}, {"set_f64", MemWrite<double>},
    {"string", MemString}, {"set_string", MemSetString}, {"wstring", MemWideString}, {"bytes", MemBytes}, {"set_bytes", MemSetBytes},
    {nullptr, nullptr},
};

// Cvars --------------------------------------------------------------------------------------------------
// Cvars a script defines stay registered for the life of the process, so a reload keeps their values and a
// mod reading another mod's cvar never sees it vanish.

struct ScriptCvar {
  bool b = false;
  int64_t i = 0;
  double d = 0.0;
  std::string s;
};

std::unordered_map<std::string, std::shared_ptr<ScriptCvar>>& ScriptCvars() {
  static std::unordered_map<std::string, std::shared_ptr<ScriptCvar>> cvars;
  return cvars;
}

// Cvars whose watchers owe a call, drained on the frame event. The cvar registry calls change callbacks
// holding its own lock, which rex.cvar.set takes while holding the VM's, so Lua can't run from there.
struct PendingChanges {
  std::mutex mutex;
  std::vector<std::string> names;
  std::set<std::string> bridged;  // cvars with a change callback in the registry, kept across reloads
};

PendingChanges& Pending() {
  static PendingChanges pending;
  return pending;
}

void PushCvar(lua_State* L, const rex::cvar::FlagEntry& info) {
  using rex::cvar::FlagType;
  switch (info.type) {
    case FlagType::Boolean: lua_pushboolean(L, rex::cvar::Query<bool>(info.name)); return;
    case FlagType::Int32: lua_pushinteger(L, rex::cvar::Query<int32_t>(info.name)); return;
    case FlagType::Int64: lua_pushinteger(L, rex::cvar::Query<int64_t>(info.name)); return;
    case FlagType::Uint32: lua_pushinteger(L, rex::cvar::Query<uint32_t>(info.name)); return;
    case FlagType::Uint64: lua_pushinteger(L, static_cast<lua_Integer>(rex::cvar::Query<uint64_t>(info.name))); return;
    case FlagType::Double: lua_pushnumber(L, rex::cvar::Query<double>(info.name)); return;
    case FlagType::String: lua_pushstring(L, rex::cvar::Query<std::string>(info.name).c_str()); return;
    case FlagType::Command: break;
  }
  lua_pushnil(L);
}

int CvarGet(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(name);
  if (info) {
    PushCvar(L, *info);
    return 1;
  }
  if (lua_gettop(L) < 2) return luaL_error(L, "unknown cvar '%s'", name);
  lua_pushvalue(L, 2);
  return 1;
}

std::string CvarText(lua_State* L, int index) {
  if (lua_isboolean(L, index)) return lua_toboolean(L, index) ? "true" : "false";
  if (lua_isinteger(L, index)) return std::to_string(static_cast<long long>(lua_tointeger(L, index)));
  if (lua_isnumber(L, index)) {
    char text[40];
    std::snprintf(text, sizeof(text), "%.17g", static_cast<double>(lua_tonumber(L, index)));
    return text;
  }
  return luaL_checkstring(L, index);
}

int CvarSet(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  const std::string value = CvarText(L, 2);
  if (!rex::cvar::SetFlagByName(name, value)) return luaL_error(L, "could not set cvar '%s' to '%s'", name, value.c_str());
  return 0;
}

int CvarExists(lua_State* L) {
  lua_pushboolean(L, rex::cvar::GetFlagInfo(luaL_checkstring(L, 1)) != nullptr);
  return 1;
}

int CvarList(lua_State* L) {
  const char* category = luaL_optstring(L, 1, nullptr);
  const std::vector<std::string> names = category ? rex::cvar::ListFlagsByCategory(category) : rex::cvar::ListFlags();
  lua_createtable(L, static_cast<int>(names.size()), 0);
  for (size_t i = 0; i < names.size(); ++i) {
    lua_pushstring(L, names[i].c_str());
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}

// rex.cvar.boolean, int32, int64, uint32, uint64, number and string: (name, default, category, description).
template <rex::cvar::FlagType kType>
int CvarDefine(lua_State* L) {
  using rex::cvar::FlagType;
  const char* name = luaL_checkstring(L, 1);
  // Already registered: a native cvar of the same name, or this one again after a reload. Its value stands.
  if (rex::cvar::GetFlagInfo(name)) {
    lua_settop(L, 1);
    return CvarGet(L);
  }
  auto& storage = ScriptCvars()[name];
  if (!storage) storage = std::make_shared<ScriptCvar>();
  std::shared_ptr<ScriptCvar> cvar = storage;

  rex::cvar::FlagEntry entry;
  entry.name = name;
  entry.type = kType;
  entry.category = luaL_optstring(L, 3, "Scripts");
  entry.description = luaL_optstring(L, 4, "");
  entry.lifecycle = rex::cvar::Lifecycle::kHotReload;
  entry.command_callback = [](std::string_view) {};
  if constexpr (kType == FlagType::Boolean) {
    cvar->b = lua_toboolean(L, 2) != 0;
    entry.default_value = cvar->b ? "true" : "false";
    entry.setter = [cvar](std::string_view v) { cvar->b = v == "true" || v == "1" || v == "yes"; return true; };
    entry.getter = [cvar] { return std::string(cvar->b ? "true" : "false"); };
  } else if constexpr (kType == FlagType::String) {
    cvar->s = luaL_optstring(L, 2, "");
    entry.default_value = cvar->s;
    entry.setter = [cvar](std::string_view v) { cvar->s.assign(v); return true; };
    entry.getter = [cvar] { return cvar->s; };
  } else if constexpr (kType == FlagType::Double) {
    cvar->d = luaL_optnumber(L, 2, 0.0);
    entry.default_value = std::to_string(cvar->d);
    entry.setter = [cvar](std::string_view v) { return rex::cvar::ParseDouble(v, cvar->d); };
    entry.getter = [cvar] { return std::to_string(cvar->d); };
  } else {
    cvar->i = luaL_optinteger(L, 2, 0);
    entry.default_value = std::to_string(cvar->i);
    entry.setter = [cvar](std::string_view v) {
      int64_t parsed = 0;
      const auto [end, error] = std::from_chars(v.data(), v.data() + v.size(), parsed);
      if (error != std::errc()) return false;
      cvar->i = parsed;
      return true;
    };
    entry.getter = [cvar] { return std::to_string(cvar->i); };
  }
  if (!rex::cvar::RegisterFlag(std::move(entry))) return luaL_error(L, "could not register cvar '%s'", name);
  lua_settop(L, 1);
  return CvarGet(L);
}

int CvarOnChange(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  RegistryTable(L, kWatchTable);
  lua_getfield(L, -1, name);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name);
  }
  lua_pushvalue(L, 2);
  lua_rawseti(L, -2, static_cast<lua_Integer>(lua_rawlen(L, -2)) + 1);
  lua_pop(L, 2);

  PendingChanges& pending = Pending();
  std::lock_guard lock(pending.mutex);
  if (pending.bridged.insert(name).second) {
    rex::cvar::RegisterChangeCallback(name, [owned = std::string(name)](std::string_view, std::string_view) {
      PendingChanges& pending = Pending();
      std::lock_guard lock(pending.mutex);
      if (std::find(pending.names.begin(), pending.names.end(), owned) == pending.names.end()) pending.names.push_back(owned);
    });
  }
  return 0;
}

constexpr luaL_Reg kCvarFuncs[] = {
    {"get", CvarGet}, {"set", CvarSet}, {"exists", CvarExists}, {"list", CvarList}, {"on_change", CvarOnChange},
    {"boolean", CvarDefine<rex::cvar::FlagType::Boolean>}, {"int32", CvarDefine<rex::cvar::FlagType::Int32>}, {"int64", CvarDefine<rex::cvar::FlagType::Int64>},
    {"uint32", CvarDefine<rex::cvar::FlagType::Uint32>}, {"uint64", CvarDefine<rex::cvar::FlagType::Uint64>}, {"number", CvarDefine<rex::cvar::FlagType::Double>},
    {"string", CvarDefine<rex::cvar::FlagType::String>},
    {nullptr, nullptr},
};

// Hooks --------------------------------------------------------------------------------------------------
// Lua hooks are before and after, not around: a hook runs before the original and may stop it by returning
// false, and rex.hook_after runs once it returns. One core hook per address carries all of them, so every
// before hook runs even when an earlier one stops the original, as mods expect.

struct HookEntry {
  uint32_t address;
  bool after;
  std::string owner;
};

struct Site {
  RexFabricHookId core = 0;
  std::vector<uint64_t> before;
  std::vector<uint64_t> after;
};

struct HookState {
  std::unordered_map<uint32_t, Site> sites;
  std::map<uint64_t, HookEntry> entries;
  uint64_t next_key = 1;
};

HookState& LuaHooks() {
  static HookState state;
  return state;
}

// True when the hook returned exactly false.
bool RunHook(VM& vm, RexFabricCall& call, uint64_t key) {
  lua_State* L = vm.L;
  RegistryTable(L, kHookTable);
  lua_rawgeti(L, -1, static_cast<lua_Integer>(key));
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return false;
  }
  PPCContext* ctx = vm.ctx;
  uint8_t* base = vm.base;
  const uint32_t address = vm.ctx_address;
  vm.ctx = call.ctx;
  vm.base = call.base;
  vm.ctx_address = call.address;
  lua_rawgeti(L, LUA_REGISTRYINDEX, vm.ctx_ref);
  bool skip = false;
  if (CallProtected(L, 1, 1, "hook")) {
    skip = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  vm.ctx = ctx;
  vm.base = base;
  vm.ctx_address = address;
  return skip;
}

// Runs an address's before or after hooks; true when a before hook stopped the original.
bool RunHooks(VM& vm, RexFabricCall& call, bool after) {
  const auto it = LuaHooks().sites.find(call.address);
  if (it == LuaHooks().sites.end()) return false;
  const std::vector<uint64_t> keys = after ? it->second.after : it->second.before;
  bool skip = false;
  for (uint64_t key : keys) skip |= RunHook(vm, call, key);
  return skip && !after;
}

void Dispatch(RexFabricCall* call, void*) {
  VM& vm = Vm();
  bool skip = false;
  {
    std::lock_guard lock(vm.mutex);
    if (vm.L) skip = RunHooks(vm, *call, false);
  }
  if (!skip) CallNext(*call);
  std::lock_guard lock(vm.mutex);
  if (vm.L) RunHooks(vm, *call, true);
}

int Attach(lua_State* L, bool after) {
  const uint32_t address = CheckAddress(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (!Hooks::Get().IsHookable(address)) return luaL_error(L, "cannot hook 0x%08X: no recompiled function at this address", address);
  HookState& state = LuaHooks();
  Site& site = state.sites[address];
  if (!site.core) site.core = Hooks::Get().Add(Vm().owner, address, Dispatch, nullptr);
  const uint64_t key = state.next_key++;
  RegistryTable(L, kHookTable);
  lua_pushvalue(L, 2);
  lua_rawseti(L, -2, static_cast<lua_Integer>(key));
  lua_pop(L, 1);
  (after ? site.after : site.before).push_back(key);
  state.entries[key] = {address, after, CallerMod(L)};
  lua_pushinteger(L, static_cast<lua_Integer>(key));
  return 1;
}

int Hook(lua_State* L) { return Attach(L, false); }
int HookAfter(lua_State* L) { return Attach(L, true); }

int Unhook(lua_State* L) {
  const auto key = static_cast<uint64_t>(luaL_checkinteger(L, 1));
  HookState& state = LuaHooks();
  const auto entry = state.entries.find(key);
  if (entry == state.entries.end()) {
    lua_pushboolean(L, false);
    return 1;
  }
  Site& site = state.sites[entry->second.address];
  std::erase(entry->second.after ? site.after : site.before, key);
  if (site.before.empty() && site.after.empty()) {
    Hooks::Get().Remove(site.core);
    state.sites.erase(entry->second.address);
  }
  state.entries.erase(entry);
  RegistryTable(L, kHookTable);
  lua_pushnil(L);
  lua_rawseti(L, -2, static_cast<lua_Integer>(key));
  lua_pop(L, 1);
  lua_pushboolean(L, true);
  return 1;
}

int ListHooks(lua_State* L) {
  std::vector<std::pair<uint64_t, const HookEntry*>> hooks;
  for (const auto& [key, entry] : LuaHooks().entries) hooks.emplace_back(key, &entry);
  std::sort(hooks.begin(), hooks.end(), [](const auto& a, const auto& b) { return a.second->address != b.second->address ? a.second->address < b.second->address : a.first < b.first; });
  lua_createtable(L, static_cast<int>(hooks.size()), 0);
  for (size_t i = 0; i < hooks.size(); ++i) {
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, static_cast<lua_Integer>(hooks[i].first));
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, hooks[i].second->address);
    lua_setfield(L, -2, "address");
    lua_pushstring(L, hooks[i].second->after ? "post" : "pre");
    lua_setfield(L, -2, "phase");
    lua_pushstring(L, hooks[i].second->owner.c_str());
    lua_setfield(L, -2, "owner");
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}

int Hookable(lua_State* L) {
  lua_pushboolean(L, Hooks::Get().IsHookable(CheckAddress(L, 1)));
  return 1;
}

// Calling guest functions --------------------------------------------------------------------------------

// rex.call(address, ...): up to eight integer arguments in r3 to r10, returns r3. It runs on the calling
// guest thread, below its stack, so it has to be called from a hook or an event a guest thread fires.
int Call(lua_State* L) {
  const uint32_t address = CheckAddress(L, 1);
  rex::runtime::ThreadState* thread = rex::runtime::ThreadState::Get();
  if (!thread) return luaL_error(L, "rex.call must run on a guest thread (call it from inside a hook, not from a frame event)");
  PPCFunc* function = rex::runtime::ResolveIndirectFunction(address);
  if (!function) return luaL_error(L, "no guest function at 0x%08X", address);
  const int argc = lua_gettop(L) - 1;
  if (argc > 8) return luaL_error(L, "rex.call supports at most 8 register arguments (got %d)", argc);

  PPCContext* parent = Vm().ctx ? Vm().ctx : thread->context();
  PPCContext ctx{};
  ctx.r1 = parent->r1;
  ctx.r1.u32 -= 0x70;  // the PowerPC minimum frame: linkage area and parameter save area
  ctx.r13 = parent->r13;
  ctx.fpscr = parent->fpscr;
  for (int i = 0; i < argc; ++i) (ctx.*guest::kGprs[3 + i]).u64 = static_cast<uint64_t>(luaL_checkinteger(L, 2 + i));
  function(ctx, Vm().base ? Vm().base : CheckMemory(L, 0));
  parent->fpscr = ctx.fpscr;
  lua_pushinteger(L, ctx.r3.u32);
  return 1;
}

// Events -------------------------------------------------------------------------------------------------

std::set<std::string>& Listened() {
  static std::set<std::string> names;
  return names;
}

void DispatchEvent(const std::string& name, std::span<const Value> args) {
  VM& vm = Vm();
  std::lock_guard lock(vm.mutex);
  if (!vm.L) return;
  lua_State* L = vm.L;
  RegistryTable(L, kEventTable);
  lua_getfield(L, -1, name.c_str());
  if (lua_istable(L, -1)) {
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, -1));
    for (lua_Integer i = 1; i <= count; ++i) {
      lua_rawgeti(L, -1, i);
      for (const Value& arg : args) PushValue(L, arg);
      CallProtected(L, static_cast<int>(args.size()), 0, name.c_str());
    }
  }
  lua_pop(L, 2);
}

// rex.on(name, fn): "frame" with the seconds since the last frame, or any event the game declares.
int On(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  RegistryTable(L, kEventTable);
  lua_getfield(L, -1, name);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name);
  }
  lua_pushvalue(L, 2);
  lua_rawseti(L, -2, static_cast<lua_Integer>(lua_rawlen(L, -2)) + 1);
  lua_pop(L, 2);
  if (Listened().insert(name).second) events::Listen(name, Vm().owner, [event = std::string(name)](std::span<const Value> args) { DispatchEvent(event, args); });
  return 0;
}

// Logging and time ---------------------------------------------------------------------------------------

std::string Concat(lua_State* L) {
  std::string text;
  for (int i = 1; i <= lua_gettop(L); ++i) {
    if (i > 1) text += ' ';
    text += luaL_tolstring(L, i, nullptr);
    lua_pop(L, 1);
  }
  return text;
}

int LogInfo(lua_State* L) {
  RF_INFO("[{}] {}", CallerMod(L), Concat(L));
  return 0;
}

int LogWarn(lua_State* L) {
  RF_WARN("[{}] {}", CallerMod(L), Concat(L));
  return 0;
}

int LogError(lua_State* L) {
  RF_ERROR("[{}] {}", CallerMod(L), Concat(L));
  return 0;
}

int LogDebug(lua_State* L) {
  RF_DEBUG("[{}] {}", CallerMod(L), Concat(L));
  return 0;
}

// rex.screen_text(text, seconds, rgb, size, key): text in the screen's top right corner for a while, for debugging.
// Two seconds, cyan and the default size unless given; a key replaces the shown line with the same key.
int ScreenText(lua_State* L) {
  std::string text = luaL_tolstring(L, 1, nullptr);
  lua_pop(L, 1);
  const double seconds = luaL_optnumber(L, 2, 2.0);
  const lua_Integer rgb = luaL_optinteger(L, 3, 0x00A8FF);
  const double size = luaL_optnumber(L, 4, 0.0);
  std::string key;
  if (!lua_isnoneornil(L, 5)) {
    key = luaL_tolstring(L, 5, nullptr);
    lua_pop(L, 1);
  }
  ui::PrintToScreen(std::move(text), seconds, static_cast<uint32_t>(rgb), static_cast<float>(size), std::move(key));
  return 0;
}

// rex.draw: shapes anywhere on the screen, positions and sizes as fractions of it (0-1), options in a table.

float OptionNumber(lua_State* L, int options, const char* name, float fallback) {
  if (!lua_istable(L, options)) return fallback;
  lua_getfield(L, options, name);
  const float value = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : fallback;
  lua_pop(L, 1);
  return std::isfinite(value) ? value : fallback;
}

bool OptionBool(lua_State* L, int options, const char* name, bool fallback) {
  if (!lua_istable(L, options)) return fallback;
  lua_getfield(L, options, name);
  const bool value = lua_isnil(L, -1) ? fallback : lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return value;
}

std::string OptionString(lua_State* L, int options, const char* name) {
  if (!lua_istable(L, options)) return {};
  lua_getfield(L, options, name);
  std::string value;
  if (!lua_isnil(L, -1)) {
    value = luaL_tolstring(L, -1, nullptr);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return value;
}

uint32_t OptionRgb(lua_State* L, int options) {
  if (!lua_istable(L, options)) return 0xFFFFFF;
  lua_getfield(L, options, "rgb");
  const uint32_t rgb = lua_isinteger(L, -1) ? static_cast<uint32_t>(lua_tointeger(L, -1)) & 0xFFFFFF : 0xFFFFFF;
  lua_pop(L, 1);
  return rgb;
}

float CheckFraction(lua_State* L, int index) {
  const double value = luaL_checknumber(L, index);
  if (!std::isfinite(value)) luaL_argerror(L, index, "not a finite number");
  return static_cast<float>(value);
}

// The options every shape takes: key, rgb (0xRRGGBB), alpha (0-1), seconds, thickness (pixels), filled.
ui::ScreenShape ShapeOf(lua_State* L, int options, ui::ScreenShape::Kind kind) {
  if (!lua_isnoneornil(L, options)) luaL_checktype(L, options, LUA_TTABLE);
  ui::ScreenShape shape;
  shape.kind = kind;
  shape.key = OptionString(L, options, "key");
  shape.rgb = OptionRgb(L, options);
  shape.alpha = OptionNumber(L, options, "alpha", 1.0f);
  shape.seconds = OptionNumber(L, options, "seconds", 0.0f);
  shape.thickness = OptionNumber(L, options, "thickness", 2.0f);
  shape.filled = OptionBool(L, options, "filled", true);
  return shape;
}

// rex.draw.text(text, x, y, options): size (of the screen's height, 0.03), align ("left", "center", "right"), background.
int ShapeText(lua_State* L) {
  ui::ScreenShape shape = ShapeOf(L, 4, ui::ScreenShape::Kind::kText);
  shape.text = luaL_tolstring(L, 1, nullptr);
  lua_pop(L, 1);
  shape.x = CheckFraction(L, 2);
  shape.y = CheckFraction(L, 3);
  shape.size = OptionNumber(L, 4, "size", 0.03f);
  const std::string align = OptionString(L, 4, "align");
  shape.align = align == "left" ? -1 : align == "right" ? 1 : 0;
  shape.background = OptionBool(L, 4, "background", false);
  ui::DrawOnScreen(std::move(shape));
  return 0;
}

// rex.draw.rect(x, y, width, height, options): filled unless filled = false; rounding (of the screen's height).
int ShapeRect(lua_State* L) {
  ui::ScreenShape shape = ShapeOf(L, 5, ui::ScreenShape::Kind::kRect);
  shape.x = CheckFraction(L, 1);
  shape.y = CheckFraction(L, 2);
  shape.x2 = CheckFraction(L, 3);
  shape.y2 = CheckFraction(L, 4);
  shape.rounding = OptionNumber(L, 5, "rounding", 0.0f);
  ui::DrawOnScreen(std::move(shape));
  return 0;
}

// rex.draw.line(x1, y1, x2, y2, options).
int ShapeLine(lua_State* L) {
  ui::ScreenShape shape = ShapeOf(L, 5, ui::ScreenShape::Kind::kLine);
  shape.x = CheckFraction(L, 1);
  shape.y = CheckFraction(L, 2);
  shape.x2 = CheckFraction(L, 3);
  shape.y2 = CheckFraction(L, 4);
  ui::DrawOnScreen(std::move(shape));
  return 0;
}

// rex.draw.circle(x, y, radius, options): the radius a fraction of the screen's height.
int ShapeCircle(lua_State* L) {
  ui::ScreenShape shape = ShapeOf(L, 4, ui::ScreenShape::Kind::kCircle);
  shape.x = CheckFraction(L, 1);
  shape.y = CheckFraction(L, 2);
  shape.size = CheckFraction(L, 3);
  ui::DrawOnScreen(std::move(shape));
  return 0;
}

int ShapeRemove(lua_State* L) {
  ui::RemoveFromScreen(luaL_checkstring(L, 1));
  return 0;
}

int ShapeClear(lua_State* L) {
  ui::ClearScreen(luaL_optstring(L, 1, ""));
  return 0;
}

constexpr luaL_Reg kDrawFuncs[] = {
    {"text", ShapeText}, {"rect", ShapeRect}, {"line", ShapeLine}, {"circle", ShapeCircle}, {"remove", ShapeRemove}, {"clear", ShapeClear},
    {nullptr, nullptr},
};

// Monotonic seconds. Only differences mean anything.
int Now(lua_State* L) {
  lua_pushnumber(L, std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
  return 1;
}

constexpr luaL_Reg kRexFuncs[] = {
    {"hook", Hook}, {"hook_after", HookAfter}, {"unhook", Unhook}, {"hooks", ListHooks}, {"hookable", Hookable}, {"call", Call},
    {"on", On}, {"now", Now}, {"screen_text", ScreenText}, {"log", LogInfo}, {"info", LogInfo}, {"warn", LogWarn}, {"error", LogError}, {"debug", LogDebug},
    {nullptr, nullptr},
};

}  // namespace

void OpenRex(lua_State* L) {
  luaL_setfuncs(L, kRexFuncs, 0);
  lua_pushstring(L, REXGLUE_VERSION_STRING);
  lua_setfield(L, -2, "version");
  luaL_newlib(L, kMemFuncs);
  lua_setfield(L, -2, "mem");
  luaL_newlib(L, kCvarFuncs);
  lua_setfield(L, -2, "cvar");
  luaL_newlib(L, kDrawFuncs);
  lua_setfield(L, -2, "draw");

  luaL_newmetatable(L, kContextMeta);
  luaL_newlib(L, kContextMethods);
  lua_pushcclosure(L, ContextIndex, 1);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, ContextNewIndex);
  lua_setfield(L, -2, "__newindex");
  lua_pushstring(L, kContextMeta);
  lua_setfield(L, -2, "__metatable");
  lua_pop(L, 1);
  lua_newuserdatauv(L, 1, 0);
  luaL_setmetatable(L, kContextMeta);
  Vm().ctx_ref = luaL_ref(L, LUA_REGISTRYINDEX);
}

void ResetRex() {
  LuaHooks() = {};
  Listened().clear();
  std::lock_guard lock(Pending().mutex);
  Pending().names.clear();
}

void DrainCvarChanges(lua_State* L) {
  std::vector<std::string> names;
  {
    std::lock_guard lock(Pending().mutex);
    names.swap(Pending().names);
  }
  if (names.empty()) return;
  RegistryTable(L, kWatchTable);
  for (const std::string& name : names) {
    lua_getfield(L, -1, name.c_str());
    if (lua_istable(L, -1)) {
      const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(name);
      const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, -1));
      for (lua_Integer i = 1; i <= count; ++i) {
        lua_rawgeti(L, -1, i);
        lua_pushstring(L, name.c_str());
        if (info) PushCvar(L, *info);
        else lua_pushnil(L);
        CallProtected(L, 2, 0, "cvar watcher");
      }
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}

}  // namespace rexfabric::lua
