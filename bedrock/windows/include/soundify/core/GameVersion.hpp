#pragma once

#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace soundify {

// The Minecraft line this DLL build is compiled for, in Latite's pattern syntax.
// It must equal the pinned Latite's supportedMinecraftVersions.
inline constexpr std::string_view NativeGameLine = "1.26.5x";

// Latite's matcher: same length, 'x' matches one digit, everything else literally.
[[nodiscard]] constexpr bool matchesGameLine(std::string_view version, std::string_view line) noexcept {
    if (version.size() != line.size() || line.empty()) return false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if ((line[i] == 'x' || line[i] == 'X') && version[i] >= '0' && version[i] <= '9') continue;
        if (line[i] != version[i]) return false;
    }
    return true;
}

// Store package "1.26.5203.0" -> "1.26.52", the version the game and Latite report
// (Latite reads major.minor.build from the executable; the package adds two hotfix digits).
[[nodiscard]] inline std::optional<std::string> gameVersionFromPackage(std::wstring_view package) {
    unsigned parts[4]{};
    std::size_t count{};
    std::string ascii;
    for (const auto ch : package) {
        if (ch > 127) return std::nullopt;
        ascii.push_back(static_cast<char>(ch));
    }
    std::string_view rest(ascii);
    while (count < 4) {
        const auto dot = rest.find('.');
        const auto field = rest.substr(0, dot);
        if (field.empty()) return std::nullopt;
        const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), parts[count]);
        if (error != std::errc{} || end != field.data() + field.size()) return std::nullopt;
        ++count;
        if (dot == std::string_view::npos) break;
        rest.remove_prefix(dot + 1);
    }
    if (count < 3 || parts[2] < 100) return std::nullopt;
    return std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2] / 100);
}

} // namespace soundify
