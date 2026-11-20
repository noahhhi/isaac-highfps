#include "../src/native_loop_plan_j510.h"
#include <cassert>
#include <cstdio>
#include <fstream>

using namespace highfps::j510;

template <std::size_t Size>
static std::uint32_t Target(const std::array<std::uint8_t, Size>& bytes, std::uint32_t site) {
    std::uint32_t displacement = 0;
    for (unsigned index = 0; index < 4; ++index)
        displacement |= std::uint32_t(bytes[index + 1]) << (8 * index);
    return site + 5 + displacement;
}

static void Verify(const LoopSites& original, std::uint32_t imageBase) {
    const LoopTargets hooks{0x12345200, 0x12345600, 0x12345800};
    LoopPlan planned;
    assert(PrepareLoopPlan(original, imageBase, hooks, planned));
    assert(planned.originalUpdate == Rebase(0x9c2950, imageBase));
    assert(planned.originalRender == Rebase(0x96af30, imageBase));
    assert(planned.originalGc == Rebase(0x86f2d0, imageBase));
    assert(planned.update[0] == 0xe8 && Target(planned.update, Rebase(kUpdateSite, imageBase)) == hooks.update);
    assert(planned.render[0] == 0xe8 && Target(planned.render, Rebase(kRenderSite, imageBase)) == hooks.render);
    assert(planned.gc[0] == 0xe8 && Target(planned.gc, Rebase(kGcSite, imageBase)) == hooks.gc);
    assert(planned.loopBranch[0] == 0xe9 && planned.loopBranch.back() == 0x90);
    assert(Target(planned.loopBranch, Rebase(kLoopBranch, imageBase)) == Rebase(kLoopHead, imageBase));
    for (std::uint8_t byte : planned.limitTest) assert(byte == 0x90);

    const auto unchanged = planned.update;
    auto altered = original;
    altered.wrapper[21] ^= 1;
    assert(!PrepareLoopPlan(altered, imageBase, hooks, planned) && unchanged == planned.update);
    altered = original;
    altered.gcGuard[2] ^= 1;
    assert(!PrepareLoopPlan(altered, imageBase, hooks, planned) && unchanged == planned.update);
    altered = original;
    altered.loopBranch[2] ^= 1;
    assert(!PrepareLoopPlan(altered, imageBase, hooks, planned) && unchanged == planned.update);
    assert(!PrepareLoopPlan(original, imageBase, {hooks.update, 0, hooks.gc}, planned));
}

int main(int argc, char** argv) {
    Verify(ExpectedLoop(kPreferredBase), kPreferredBase);
    constexpr std::uint32_t relocated = 0x10200000;
    Verify(ExpectedLoop(relocated), relocated);
    LoopPlan rejected;
    assert(!PrepareLoopPlan(ExpectedLoop(kPreferredBase), relocated, {1, 2, 3}, rejected));
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary);
        LoopSites sites;
        auto read = [&file](auto& bytes) {
            file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
            assert(file);
        };
        read(sites.update);
        read(sites.render);
        read(sites.gc);
        read(sites.gcGuard);
        read(sites.limitTest);
        read(sites.limitGuard);
        read(sites.loopBranch);
        read(sites.wrapper);
        assert(file.peek() == std::char_traits<char>::eof());
        Verify(sites, kPreferredBase);
        std::puts("J510 exact Steam image loop bytes: passed");
    }
    std::puts("J510 loop plan: wrapper chain, branch targets, relocation and rejection passed");
}
