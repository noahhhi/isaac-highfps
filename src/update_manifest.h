#pragma once

#include <charconv>
#include <string_view>

namespace highfps::update {

constexpr char kManifestHeader[] = "isaac-highfps-j510-v1";

struct Manifest {
    unsigned revision{};
    std::string_view gameHash, dllHash, helperHash, iniHash;
};

inline bool IsSha256(std::string_view value) {
    if (value.size() != 64) return false;
    for (const char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

inline bool ParseManifest(std::string_view input, Manifest& manifest) {
    std::string_view lines[6];
    for (auto& line : lines) {
        const auto newline = input.find('\n');
        if (newline == std::string_view::npos) return false;
        line = input.substr(0, newline);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        input.remove_prefix(newline + 1);
    }
    if (!input.empty() || lines[0] != kManifestHeader || lines[1].empty() ||
        lines[1].size() > 9 || lines[1].front() == '0') return false;
    unsigned revision = 0;
    const auto parsed = std::from_chars(lines[1].data(), lines[1].data() + lines[1].size(), revision);
    if (parsed.ec != std::errc{} || parsed.ptr != lines[1].data() + lines[1].size()) return false;
    for (unsigned i = 2; i < 6; ++i)
        if (!IsSha256(lines[i])) return false;
    manifest = {revision, lines[2], lines[3], lines[4], lines[5]};
    return true;
}

constexpr unsigned long long kCheckInterval = 24ull * 60 * 60 * 10000000;

inline bool CheckDue(unsigned long long previous, unsigned long long now) {
    return previous == 0 || (now >= previous && now - previous >= kCheckInterval);
}

}  // namespace highfps::update
