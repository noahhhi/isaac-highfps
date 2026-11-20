#pragma once

#include <cstdint>

namespace highfps::j510 {

inline constexpr std::uint32_t kTimestamp = 0x6AFE6324;
inline constexpr std::uint32_t kImageSize = 0x953000;
inline constexpr std::uintptr_t kManager = 0xC9A0D4;
inline constexpr std::uintptr_t kManagerCounter = 0x4AE94;
inline constexpr std::uintptr_t kMainCalls = 0x9465FC;
inline constexpr std::uint8_t kMainCallBytes[] = {
    0xe8, 0x4f, 0xc3, 0x07, 0x00, 0xe8, 0x2a, 0x49, 0x02, 0x00,
};

constexpr bool Matches(std::uint32_t stamp, std::uint32_t imageSize) {
    return stamp == kTimestamp && imageSize == kImageSize;
}

constexpr bool CanMeasure(bool enabled, bool nativeTimingProbe) {
    return !enabled && nativeTimingProbe;
}

}
