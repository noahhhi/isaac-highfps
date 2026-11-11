// Repentance+ 1.9.7.15.J374, PE32 x86, preferred image base 0x400000.
// Globals/functions below are virtual addresses; Addr() applies the ASLR delta.
// Member fields and vtable indices are offsets. See docs/port-1.9.7.15.md.
#pragma once
#include <cstdint>
namespace isaac {
inline constexpr uint32_t kPeTimestamp = 0x68D45723;
inline constexpr uint32_t kPeImageSize = 0x8B6000;

// Executable identity and simulation
inline constexpr uintptr_t kImageBase = 0x00400000;
inline constexpr uintptr_t kGGamePtr = 0x00C0C074;
inline constexpr uintptr_t kGManagerPtr   = 0x00C0C09C;
inline constexpr uintptr_t kMgrFrameCount        = 0x4ABA8;
inline constexpr uintptr_t kPhaseMachine     = 0x0094CAD0;

// Entity state (shared layout verified in J374)
inline constexpr uintptr_t kEntType        = 0x028;
inline constexpr uintptr_t kEntFlags       = 0x168;
inline constexpr uintptr_t kEntPreviewFlag = 0x175;
inline constexpr uintptr_t kEntPosition    = 0x33C;
inline constexpr uintptr_t kEntPosBackup   = 0x344;
inline constexpr uintptr_t kEntVelocity    = 0x360;
inline constexpr uintptr_t kEntFriction    = 0x36C;
inline constexpr uintptr_t kEntTimeScale   = 0x39C;
inline constexpr uint64_t kFlagNoInterpolate      = 1ull << 1;

// Room and render projection
inline constexpr uintptr_t kRoomInGame   = 0x18300;
inline constexpr uintptr_t kRoomScrollOfs = 0x1204;
inline constexpr uintptr_t kOrthoWidth        = 0x00C0E71C;
inline constexpr uintptr_t kOrthoHeight       = 0x00C0E92C;
inline constexpr uintptr_t kSnapScaleA = 0x00BAF5E4;
inline constexpr uintptr_t kSnapScaleB = 0x00BAF7A0;

// J374 KAGE graphics ABI (not interchangeable with J460)
inline constexpr uintptr_t kGraphicsManager   = 0x00BD56B0;
inline constexpr uintptr_t kCurrentShader     = 0x00BD56C8;
inline constexpr uintptr_t kScreenWidth       = 0x00C0E944;
inline constexpr uintptr_t kScreenHeight      = 0x00C0E928;
inline constexpr uintptr_t kMgrBlendState     = 0x04;
inline constexpr uintptr_t kMgrCurRenderTgt   = 0x1C;
inline constexpr uintptr_t kPushRenderTarget  = 0x00682000;
inline constexpr uintptr_t kPopRenderTarget   = 0x006820C0;
inline constexpr uintptr_t kSetRenderTarget   = 0x00A0C6E0;
inline constexpr uintptr_t kClearTarget       = 0x00A0C880;
inline constexpr uintptr_t kFlushBatches      = 0x00A09720;
inline constexpr uintptr_t kCreateRenderTgt   = 0x00A13B40;
inline constexpr uintptr_t kLuaPostRender     = 0x008633A0;
inline constexpr int kVtImageRender    = 3;
inline constexpr int kVtImageSetFilter = 16;
inline constexpr uintptr_t kImgFilterCacheMin = 0x24;
inline constexpr uintptr_t kImgFilterCacheMag = 0x28;
} // namespace isaac
