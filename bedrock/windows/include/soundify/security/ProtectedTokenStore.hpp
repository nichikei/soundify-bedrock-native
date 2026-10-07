#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace soundify::security {

class ProtectedTokenStore final {
public:
    explicit ProtectedTokenStore(std::filesystem::path path);

    void save(std::string_view token) const;
    [[nodiscard]] std::optional<std::string> load() const;
    void clear() const;

private:
    std::filesystem::path path_;
};

} // namespace soundify::security
