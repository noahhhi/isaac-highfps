#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace highfps::j510 {

inline constexpr std::uint32_t kPreferredBase = 0x00400000;
inline constexpr std::uint32_t kUpdateSite = 0x9465FC;
inline constexpr std::uint32_t kRenderSite = 0x946601;
inline constexpr std::uint32_t kGcGuard = 0x946606;
inline constexpr std::uint32_t kGcSite = 0x94660F;
inline constexpr std::uint32_t kLimitTest = 0x94665D;
inline constexpr std::uint32_t kLimitGuard = 0x946661;
inline constexpr std::uint32_t kLoopBranch = 0x94666B;
inline constexpr std::uint32_t kLoopHead = 0x946541;
inline constexpr std::uint32_t kUpdateWrapper = 0x9C2950;
inline constexpr std::uint32_t kRender = 0x96AF30;
inline constexpr std::uint32_t kGc = 0x86F2D0;
inline constexpr std::uint32_t kGame = 0xC99538;
inline constexpr std::uint32_t kWindowFlags = 0xC9E894;

struct LoopSites {
    std::array<std::uint8_t, 5> update{}, render{}, gc{};
    std::array<std::uint8_t, 9> gcGuard{};
    std::array<std::uint8_t, 4> limitTest{};
    std::array<std::uint8_t, 10> limitGuard{};
    std::array<std::uint8_t, 6> loopBranch{};
    std::array<std::uint8_t, 40> wrapper{};
};

struct LoopTargets {
    std::uint32_t update{}, render{}, gc{};
};

struct LoopPlan {
    std::uint32_t originalUpdate{}, originalRender{}, originalGc{};
    std::array<std::uint8_t, 5> update{}, render{}, gc{};
    std::array<std::uint8_t, 4> limitTest{};
    std::array<std::uint8_t, 6> loopBranch{};
};

constexpr std::uint32_t Rebase(std::uint32_t address, std::uint32_t imageBase) {
    return imageBase + (address - kPreferredBase);
}

template <std::size_t Size>
constexpr void WriteU32(std::array<std::uint8_t, Size>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index)
        bytes[offset + index] = std::uint8_t(value >> (8 * index));
}

constexpr std::array<std::uint8_t, 5> Relative(std::uint8_t opcode, std::uint32_t site, std::uint32_t target) {
    std::array<std::uint8_t, 5> bytes{opcode};
    WriteU32(bytes, 1, target - (site + 5));
    return bytes;
}

inline LoopSites ExpectedLoop(std::uint32_t imageBase) {
    LoopSites sites;
    sites.update = Relative(0xe8, kUpdateSite, kUpdateWrapper);
    sites.render = Relative(0xe8, kRenderSite, kRender);
    sites.gc = Relative(0xe8, kGcSite, kGc);
    sites.gcGuard = {0x83, 0x3d, 0, 0, 0, 0, 0, 0x74, 0x05};
    WriteU32(sites.gcGuard, 2, Rebase(kGame, imageBase));
    sites.limitTest = {0x84, 0xdb, 0x75, 0x10};
    sites.limitGuard = {0xf7, 0x05, 0, 0, 0, 0, 0x00, 0x02, 0x00, 0x00};
    WriteU32(sites.limitGuard, 2, Rebase(kWindowFlags, imageBase));
    sites.loopBranch = {0x0f, 0x85, 0, 0, 0, 0};
    WriteU32(sites.loopBranch, 2, kLoopHead - (kLoopBranch + 6));
    sites.wrapper = {
        0x68, 0x34, 0x40, 0xc8, 0x00, 0xff, 0x15, 0x98, 0xaa, 0xb3, 0x00, 0x83, 0xc4, 0x04,
        0x83, 0x38, 0x00, 0x74, 0x06, 0xff, 0x15, 0x80, 0xaa, 0xb3, 0x00, 0x32, 0xc9,
        0xe8, 0xe0, 0xf0, 0x0c, 0x00, 0x6a, 0x01, 0xe8, 0xd9, 0x7c, 0xfa, 0xff, 0xc3,
    };
    WriteU32(sites.wrapper, 1, Rebase(0xC84034, imageBase));
    WriteU32(sites.wrapper, 7, Rebase(0xB3AA98, imageBase));
    WriteU32(sites.wrapper, 21, Rebase(0xB3AA80, imageBase));
    return sites;
}

inline bool PrepareLoopPlan(const LoopSites& live, std::uint32_t imageBase,
                            const LoopTargets& targets, LoopPlan& result) {
    const auto expected = ExpectedLoop(imageBase);
    if (!targets.update || !targets.render || !targets.gc ||
        live.update != expected.update || live.render != expected.render || live.gc != expected.gc ||
        live.gcGuard != expected.gcGuard || live.limitTest != expected.limitTest ||
        live.limitGuard != expected.limitGuard || live.loopBranch != expected.loopBranch ||
        live.wrapper != expected.wrapper)
        return false;

    LoopPlan pending;
    pending.originalUpdate = Rebase(kUpdateWrapper, imageBase);
    pending.originalRender = Rebase(kRender, imageBase);
    pending.originalGc = Rebase(kGc, imageBase);
    pending.update = Relative(0xe8, Rebase(kUpdateSite, imageBase), targets.update);
    pending.render = Relative(0xe8, Rebase(kRenderSite, imageBase), targets.render);
    pending.gc = Relative(0xe8, Rebase(kGcSite, imageBase), targets.gc);
    pending.limitTest.fill(0x90);
    pending.loopBranch = {0xe9, 0, 0, 0, 0, 0x90};
    WriteU32(pending.loopBranch, 1, kLoopHead - (kLoopBranch + 5));
    result = pending;
    return true;
}

}
