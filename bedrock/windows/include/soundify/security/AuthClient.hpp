#pragma once

#include "soundify/security/MachineId.hpp"
#include "soundify/security/ProtectedTokenStore.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace soundify::security {

inline constexpr std::string_view BedrockClientVersion = "0.4.2";
// Store catalog entry for Bedrock: game_version "bedrock", product_version = client version.
inline constexpr std::string_view AuthRelease = "0.4.2+bedrock";
inline constexpr std::string_view AuthClientName = "soundify-reborn-bedrock";
// Same device id as the Java builds of 2026-10 (see MachineId.hpp).
inline constexpr int HwidScheme = 2;
inline constexpr std::string_view UnreachableMessage =
    "Can't reach the login server. Check your internet or set DNS to 1.1.1.1";

struct HttpResponse {
    unsigned status{};
    std::string body;
};

struct RequestTimeouts {
    std::chrono::milliseconds resolve{5000};
    std::chrono::milliseconds connect{5000};
    std::chrono::milliseconds send{5000};
    std::chrono::milliseconds receive{8000};
};

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    // Throws when the host cannot be reached (DNS, TCP, TLS or timeout).
    virtual HttpResponse postJson(std::wstring_view url, std::string_view body,
                                  std::wstring_view userAgent, const RequestTimeouts& timeouts) = 0;
};

class WinHttpTransport final : public HttpTransport {
public:
    HttpResponse postJson(std::wstring_view url, std::string_view body,
                          std::wstring_view userAgent, const RequestTimeouts& timeouts) override;
};

// Our own domain first: some ISPs (XL Axiata, 2026-10) DNS-block every *.workers.dev name.
[[nodiscard]] std::vector<std::wstring> defaultAuthHosts();

// The host index that answered last in this process, tried first by every later request
// (auth now, resource downloads later), so a blocked host costs one timeout per session.
[[nodiscard]] std::size_t preferredHostIndex() noexcept;
void preferHostIndex(std::size_t index) noexcept;

// POSTs to `path` on each host in turn, starting with the preferred one: the next host is tried
// on a connection error or a 5xx, all within `budget`. Throws when no host answers.
[[nodiscard]] HttpResponse postWithFallback(HttpTransport& transport, const std::vector<std::wstring>& hosts,
                                            std::wstring_view path, std::string_view body,
                                            std::chrono::milliseconds budget);

enum class AuthStatus {
    Authenticated,
    LoginRequired,
    Rejected,
    VersionDenied,
    UpdateRequired,
    NetworkError,
    InvalidResponse,
    StorageError,
};

struct AuthResult {
    AuthStatus status{AuthStatus::InvalidResponse};
    std::string message;

    [[nodiscard]] bool authenticated() const noexcept {
        return status == AuthStatus::Authenticated;
    }
};

struct AuthOptions {
    std::vector<std::wstring> hosts = defaultAuthHosts();
    // Sent next to the release, never inside it, so a game update needs no new store rule.
    std::string minecraftVersion;
    // Upper bound for one request across every host.
    std::chrono::milliseconds budget{18000};
};

// Reads this PC's device id; throws when none can be read.
using IdentityProvider = std::function<MachineIdentity()>;

class AuthClient final {
public:
    // Without `identity` (the in-game DLL) the id saved with the session by the launcher or
    // login app is used, so the game process never queries hardware.
    AuthClient(ProtectedTokenStore& tokenStore, HttpTransport& transport,
               IdentityProvider identity = {}, AuthOptions options = {});

    [[nodiscard]] AuthResult login(std::string username, std::string password);
    [[nodiscard]] AuthResult verifyStored();
    void logout() const;

private:
    struct Session {
        MachineIdentity identity;
        std::string token;
        bool legacyFormat{};
    };

    ProtectedTokenStore& tokenStore_;
    HttpTransport& transport_;
    IdentityProvider identity_;
    AuthOptions options_;

    [[nodiscard]] HttpResponse post(std::wstring_view path, std::string_view body);
    [[nodiscard]] AuthResult verifyToken(const MachineIdentity& identity, std::string_view token);
    [[nodiscard]] std::optional<Session> loadSession() const;
    void saveSession(const MachineIdentity& identity, std::string_view token) const;
};

[[nodiscard]] std::filesystem::path defaultTokenPath();

} // namespace soundify::security
