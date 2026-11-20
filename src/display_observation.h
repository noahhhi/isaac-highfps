#pragma once

#include "presentation_refresh.h"

namespace highfps::refresh {

struct HostSnapshot {
    Observation display{};
    std::uint32_t outputId = 0;
    bool valid = false;
};

constexpr bool ReadUnsigned(const char*& cursor, std::uint64_t& value) {
    if (*cursor < '0' || *cursor > '9') return false;
    value = 0;
    do {
        const auto digit = unsigned(*cursor++ - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    } while (*cursor >= '0' && *cursor <= '9');
    return true;
}

constexpr HostSnapshot ParseHostSnapshot(const char* cursor, std::uint64_t nowMs) {
    if (!cursor) return {};
    constexpr char prefix[] = "HFDS1 ";
    for (unsigned index = 0; index + 1 < sizeof(prefix); ++index)
        if (*cursor++ != prefix[index]) return {};
    std::uint64_t values[8] = {};
    for (unsigned index = 0; index < 8; ++index) {
        if (index && *cursor++ != ' ') return {};
        if (!ReadUnsigned(cursor, values[index])) return {};
    }
    if (*cursor == '\n') ++cursor;
    if (*cursor || values[0] > nowMs || nowMs - values[0] > 3000 ||
        !values[1] || values[1] > UINT32_MAX ||
        values[2] > UINT32_MAX || values[3] > UINT32_MAX ||
        values[4] > 2 || values[5] > 2 || values[6] > 2 || values[7] > 1) return {};
    const Rate mode{std::uint32_t(values[2]), std::uint32_t(values[3])};
    if (!mode.Valid() || mode.Hz() <= 1) return {};
    const Observation display{mode, {}, Support(values[4]), Support(values[5]),
                              Support(values[6]), values[7] != 0};
    return {display, std::uint32_t(values[1]), true};
}

}
