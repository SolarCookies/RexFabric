// The RexFabricApi table handed to native mods.

#include <bit>
#include <cstring>

#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rexfabric/rexfabric.h>

#include "hooks.h"
#include "log.h"
#include "mod.h"

namespace rexfabric {
namespace {

using Register = PPCRegister PPCContext::*;

constexpr Register kGprs[32] = {&PPCContext::r0, &PPCContext::r1, &PPCContext::r2, &PPCContext::r3, &PPCContext::r4, &PPCContext::r5, &PPCContext::r6, &PPCContext::r7, &PPCContext::r8, &PPCContext::r9, &PPCContext::r10, &PPCContext::r11, &PPCContext::r12, &PPCContext::r13, &PPCContext::r14, &PPCContext::r15, &PPCContext::r16, &PPCContext::r17, &PPCContext::r18, &PPCContext::r19, &PPCContext::r20, &PPCContext::r21, &PPCContext::r22, &PPCContext::r23, &PPCContext::r24, &PPCContext::r25, &PPCContext::r26, &PPCContext::r27, &PPCContext::r28, &PPCContext::r29, &PPCContext::r30, &PPCContext::r31};
constexpr Register kFprs[32] = {&PPCContext::f0, &PPCContext::f1, &PPCContext::f2, &PPCContext::f3, &PPCContext::f4, &PPCContext::f5, &PPCContext::f6, &PPCContext::f7, &PPCContext::f8, &PPCContext::f9, &PPCContext::f10, &PPCContext::f11, &PPCContext::f12, &PPCContext::f13, &PPCContext::f14, &PPCContext::f15, &PPCContext::f16, &PPCContext::f17, &PPCContext::f18, &PPCContext::f19, &PPCContext::f20, &PPCContext::f21, &PPCContext::f22, &PPCContext::f23, &PPCContext::f24, &PPCContext::f25, &PPCContext::f26, &PPCContext::f27, &PPCContext::f28, &PPCContext::f29, &PPCContext::f30, &PPCContext::f31};

RexFabricMod host_mod{"host"};

uint8_t* Translate(uint32_t address) { return REX_KERNEL_MEMORY()->TranslateVirtual(address); }

template <typename T>
T Read(uint32_t address) {
  T value;
  std::memcpy(&value, Translate(address), sizeof(T));
  if constexpr (sizeof(T) > 1) value = std::byteswap(value);
  return value;
}

template <typename T>
void Write(uint32_t address, T value) {
  if constexpr (sizeof(T) > 1) value = std::byteswap(value);
  std::memcpy(Translate(address), &value, sizeof(T));
}

template <typename Float, typename Bits>
Float ReadFloat(uint32_t address) { return std::bit_cast<Float>(Read<Bits>(address)); }

template <typename Float, typename Bits>
void WriteFloat(uint32_t address, Float value) { Write<Bits>(address, std::bit_cast<Bits>(value)); }

void Log(RexFabricMod* mod, RexFabricLogLevel level, const char* message) {
  const char* name = mod ? mod->name.c_str() : "?";
  switch (level) {
    case REXFABRIC_LOG_DEBUG: RF_DEBUG("[{}] {}", name, message); break;
    case REXFABRIC_LOG_INFO: RF_INFO("[{}] {}", name, message); break;
    case REXFABRIC_LOG_WARN: RF_WARN("[{}] {}", name, message); break;
    case REXFABRIC_LOG_ERROR: RF_ERROR("[{}] {}", name, message); break;
  }
}

int IsHookable(uint32_t address) { return Hooks::Get().IsHookable(address); }

RexFabricHookId Hook(RexFabricMod* mod, uint32_t address, RexFabricHookFn fn, void* user) {
  const RexFabricHookId id = Hooks::Get().Add(mod, address, fn, user);
  if (!id) RF_WARN("[{}] can't hook {:08X}: not a recompiled function", mod ? mod->name : "?", address);
  return id;
}

int Unhook(RexFabricHookId id) { return Hooks::Get().Remove(id); }

void ApiCallNext(RexFabricCall* call) { CallNext(*call); }
uint32_t CallAddress(const RexFabricCall* call) { return call->address; }
uint64_t GetGpr(const RexFabricCall* call, uint32_t index) { return index < 32 ? (call->ctx->*kGprs[index]).u64 : 0; }
void SetGpr(RexFabricCall* call, uint32_t index, uint64_t value) { if (index < 32) (call->ctx->*kGprs[index]).u64 = value; }
double GetFpr(const RexFabricCall* call, uint32_t index) { return index < 32 ? (call->ctx->*kFprs[index]).f64 : 0.0; }
void SetFpr(RexFabricCall* call, uint32_t index, double value) { if (index < 32) (call->ctx->*kFprs[index]).f64 = value; }

uint64_t CallGuest(RexFabricCall* call, uint32_t address, const uint64_t* args, uint32_t count) {
  PPCFunc* function = rex::runtime::ResolveIndirectFunction(address);
  if (!function) {
    RF_WARN("call_guest: no function at {:08X}", address);
    return 0;
  }
  // A copy, so the hooked call's registers survive. The stack pointer is the hooked call's, whose own frame
  // isn't live before call_next or after it returns.
  PPCContext ctx = *call->ctx;
  for (uint32_t i = 0; i < count && i < 8; ++i) (ctx.*kGprs[3 + i]).u64 = args[i];
  function(ctx, call->base);
  return ctx.r3.u64;
}

void* GuestPointer(uint32_t address) { return Translate(address); }

constexpr RexFabricApi kApi = {
    .version = REXFABRIC_API_VERSION,
    .size = sizeof(RexFabricApi),
    .log = Log,
    .is_hookable = IsHookable,
    .hook = Hook,
    .unhook = Unhook,
    .call_next = ApiCallNext,
    .call_address = CallAddress,
    .get_gpr = GetGpr,
    .set_gpr = SetGpr,
    .get_fpr = GetFpr,
    .set_fpr = SetFpr,
    .call_guest = CallGuest,
    .guest_pointer = GuestPointer,
    .read_u8 = Read<uint8_t>,
    .read_u16 = Read<uint16_t>,
    .read_u32 = Read<uint32_t>,
    .read_u64 = Read<uint64_t>,
    .read_f32 = ReadFloat<float, uint32_t>,
    .read_f64 = ReadFloat<double, uint64_t>,
    .write_u8 = Write<uint8_t>,
    .write_u16 = Write<uint16_t>,
    .write_u32 = Write<uint32_t>,
    .write_u64 = Write<uint64_t>,
    .write_f32 = WriteFloat<float, uint32_t>,
    .write_f64 = WriteFloat<double, uint64_t>,
};

}  // namespace

const RexFabricApi& Api() { return kApi; }
RexFabricMod* Host() { return &host_mod; }

}  // namespace rexfabric
