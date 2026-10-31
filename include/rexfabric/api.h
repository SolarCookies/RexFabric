// The C interface native mods (.asi, .dll, .so) are built against. Nothing here depends on the SDK, so a
// mod only breaks when this file does. RexFabricApi only ever grows: new members go at the end and bump
// REXFABRIC_API_VERSION, so a mod built against an older version keeps working on newer hosts.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REXFABRIC_API_VERSION 1

#if defined(_WIN32)
#define REXFABRIC_MOD_EXPORT __declspec(dllexport)
#else
#define REXFABRIC_MOD_EXPORT __attribute__((visibility("default")))
#endif

// The mod calling in. Its hooks and log lines belong to it.
typedef struct RexFabricMod RexFabricMod;
// A hooked call in progress, valid until the hook returns.
typedef struct RexFabricCall RexFabricCall;
// Zero means no hook.
typedef uint64_t RexFabricHookId;

// Runs on the guest thread making the call. Call call_next to run the rest of the chain and the original
// function, or skip it to replace the function.
typedef void (*RexFabricHookFn)(RexFabricCall* call, void* user);

typedef enum RexFabricLogLevel { REXFABRIC_LOG_DEBUG, REXFABRIC_LOG_INFO, REXFABRIC_LOG_WARN, REXFABRIC_LOG_ERROR } RexFabricLogLevel;

typedef struct RexFabricApi {
  uint32_t version;  // the host's REXFABRIC_API_VERSION
  uint32_t size;     // the host's sizeof(RexFabricApi)

  void (*log)(RexFabricMod* mod, RexFabricLogLevel level, const char* message);

  // Hooks attach to guest function addresses. Hooks on one function run in the order they were added.
  int (*is_hookable)(uint32_t address);
  RexFabricHookId (*hook)(RexFabricMod* mod, uint32_t address, RexFabricHookFn fn, void* user);
  int (*unhook)(RexFabricHookId id);

  // Only valid inside a hook. Registers follow the PowerPC ABI: r3 to r10 carry integer arguments and r3 the
  // result, f1 to f13 carry float arguments and f1 the result.
  void (*call_next)(RexFabricCall* call);
  uint32_t (*call_address)(const RexFabricCall* call);
  uint64_t (*get_gpr)(const RexFabricCall* call, uint32_t index);
  void (*set_gpr)(RexFabricCall* call, uint32_t index, uint64_t value);
  double (*get_fpr)(const RexFabricCall* call, uint32_t index);
  void (*set_fpr)(RexFabricCall* call, uint32_t index, double value);
  // Calls the guest function at `address` with up to eight integer arguments and returns its r3. The hooked
  // call's registers are left as they were.
  uint64_t (*call_guest)(RexFabricCall* call, uint32_t address, const uint64_t* args, uint32_t count);

  // Guest memory, by guest virtual address. Reads and writes convert from and to the guest's big endian.
  void* (*guest_pointer)(uint32_t address);
  uint8_t (*read_u8)(uint32_t address);
  uint16_t (*read_u16)(uint32_t address);
  uint32_t (*read_u32)(uint32_t address);
  uint64_t (*read_u64)(uint32_t address);
  float (*read_f32)(uint32_t address);
  double (*read_f64)(uint32_t address);
  void (*write_u8)(uint32_t address, uint8_t value);
  void (*write_u16)(uint32_t address, uint16_t value);
  void (*write_u32)(uint32_t address, uint32_t value);
  void (*write_u64)(uint32_t address, uint64_t value);
  void (*write_f32)(uint32_t address, float value);
  void (*write_f64)(uint32_t address, double value);
} RexFabricApi;

// A native mod exports RexFabricModInit and may export RexFabricModShutdown. Init runs once guest memory exists
// and before the game starts; returning zero unloads the mod. A library without RexFabricModInit still loads, so
// plain .asi files can do their own setup in DllMain.
typedef int (*RexFabricModInitFn)(const RexFabricApi* api, RexFabricMod* mod);
typedef void (*RexFabricModShutdownFn)(void);

#define REXFABRIC_MOD_INIT_NAME "RexFabricModInit"
#define REXFABRIC_MOD_SHUTDOWN_NAME "RexFabricModShutdown"

#ifdef __cplusplus
}
#endif
