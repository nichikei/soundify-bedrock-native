#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace soundify::security {

inline constexpr std::wstring_view InstallationKeyName = L"Soundify Reborn Bedrock Installation Identity";

class InstallationIdentity final {
public:
    explicit InstallationIdentity(std::wstring keyName = std::wstring(InstallationKeyName));
    ~InstallationIdentity();

    // Id of a key that already exists, without creating one. Bedrock 0.3.14-0.3.23 sent this
    // id as `hwid`, so it is still useful as a legacy id on PCs that ran those builds.
    [[nodiscard]] static std::optional<std::string> existingId(
        const std::wstring& keyName = std::wstring(InstallationKeyName));

    InstallationIdentity(const InstallationIdentity&) = delete;
    InstallationIdentity& operator=(const InstallationIdentity&) = delete;
    InstallationIdentity(InstallationIdentity&&) = delete;
    InstallationIdentity& operator=(InstallationIdentity&&) = delete;

    [[nodiscard]] std::string id() const;
    [[nodiscard]] std::vector<std::uint8_t> publicKey() const;
    [[nodiscard]] std::vector<std::uint8_t> sign(std::string_view challenge) const;

private:
    std::uintptr_t provider_{};
    std::uintptr_t key_{};
};

[[nodiscard]] std::string toHex(const std::vector<std::uint8_t>& bytes);

} // namespace soundify::security
