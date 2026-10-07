#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace soundify::security {

// The device id shared with the Java mod (hwidScheme 2). On one PC both must send the same
// `hwid`, otherwise the account's single device slot is taken twice.
//
// Java (HWIDUtils, 2026-10): hwid = SHA-256(mainboard serial + system-disk serial), each read
// with a PowerShell Get-CimInstance query; SHA-256(mainboard serial) and any `.hwid_cache`
// value are sent as legacyHwids. Java runs PowerShell without -NoProfile, so a profile that
// prints text changes its id. Here the same WMI values are read directly, which gives the id
// Java computes on a PC whose PowerShell profile prints nothing.
inline constexpr std::size_t MaxLegacyHwids = 8;

struct MachineIdentity {
    std::string hwid;
    std::vector<std::string> legacyHwids;

    [[nodiscard]] bool owns(std::string_view id) const;
};

struct DiskDrive {
    std::uint32_t index{};
    std::optional<std::string> serial;
};

// WMI values the Java queries print. Serials are UTF-8; a null SerialNumber prints nothing.
struct HardwareSerials {
    std::vector<std::optional<std::string>> baseboards;   // Win32_BaseBoard, WMI order
    std::optional<std::uint32_t> systemDiskNumber;         // MSFT_Partition of %SystemDrive%
    std::vector<DiskDrive> diskDrives;                     // Win32_DiskDrive, WMI order
};

// Java's command reader: non-blank lines, each trimmed of characters <= U+0020, joined.
[[nodiscard]] std::string joinTrimmedLines(std::string_view output);
[[nodiscard]] std::string sha256Hex(std::string_view text);
// Lowercase SHA-256 hex that is not the hash of an empty input.
[[nodiscard]] bool isIdentifyingHwid(std::string_view value);

// What each Java PowerShell query yields for these serials, after its line reader.
[[nodiscard]] std::string mainboardOutput(const HardwareSerials& serials);
[[nodiscard]] std::string systemDiskOutput(const HardwareSerials& serials);

// Mirrors HWIDUtils.compute on Windows. `cachedIds` are raw contents of files that may
// hold an earlier id (Java `.hwid_cache`, the 0.3.x Bedrock installation id); only trimmed
// lowercase SHA-256 values count. When no hardware id is readable the first cached id is used.
[[nodiscard]] MachineIdentity computeMachineIdentity(std::string_view mainboard, std::string_view disk,
                                                     const std::vector<std::string>& cachedIds);

// Reads the serials through WMI (no PowerShell). Throws when WMI cannot be reached.
[[nodiscard]] HardwareSerials readHardwareSerials();

// This PC's identity, computed once per process. Throws when no id can be read at all.
[[nodiscard]] MachineIdentity currentMachineIdentity();

} // namespace soundify::security
