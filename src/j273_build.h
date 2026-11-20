#pragma once

#include <cstdint>

namespace highfps::j273 {

constexpr uint16_t kMachine = 0x014c;
constexpr uint32_t kTimestamp = 0x68154d8d;
constexpr uint32_t kImageSize = 0x88d000;
constexpr uintptr_t kMainCalls = 0x0090aeff;
constexpr uintptr_t kSleepClamp = 0x0090af8c;
constexpr uintptr_t kSpinBranch = 0x0090b019;
constexpr uint8_t kMainCallBytes[] = {
    0x32, 0xc9, 0xe8, 0x3a, 0xfd, 0x11, 0x00, 0x6a, 0x01, 0xe8,
    0xd3, 0x34, 0x02, 0x00, 0xe8, 0x2e, 0x3d, 0x02, 0x00,
};
constexpr uint8_t kSleepBytes[] = {0x0f, 0x4f, 0xc8};
constexpr uint8_t kSpinBytes[] = {0x77, 0xe5};

constexpr bool Matches(uint16_t machine, uint32_t timestamp, uint32_t imageSize) {
    return machine == kMachine && timestamp == kTimestamp && imageSize == kImageSize;
}

}  // namespace highfps::j273
