#include "soundify/security/AuthClient.hpp"

#include <windows.h>
#include <winhttp.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

namespace soundify::security {
namespace {

constexpr std::size_t MaximumResponseBytes = 1024 * 1024;
constexpr int SessionFormat = 2;
constexpr auto MinimumAttemptTime = std::chrono::milliseconds(1000);

std::atomic<std::size_t> preferredHost{0};

class UnreachableError final : public std::runtime_error {
public:
    UnreachableError() : std::runtime_error(std::string(UnreachableMessage)) {}
};

void secureClear(std::string& value) noexcept {
    if (!value.empty()) SecureZeroMemory(value.data(), value.size());
    value.clear();
}

class InternetHandle final {
public:
    explicit InternetHandle(HINTERNET value = nullptr) noexcept : value_(value) {}
    ~InternetHandle() { if (value_) WinHttpCloseHandle(value_); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    [[nodiscard]] HINTERNET get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }
private:
    HINTERNET value_{};
};

std::string serverMessage(const nlohmann::json& response, std::string fallback) {
    const auto found = response.find("message");
    if (found != response.end() && found->is_string() && !found->get_ref<const std::string&>().empty()) {
        return found->get<std::string>();
    }
    return fallback;
}

bool truthy(const nlohmann::json& value) {
    if (value.is_boolean()) return value.get<bool>();
    if (!value.is_string()) return false;
    auto text = value.get<std::string>();
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text == "true" || text == "update" || text == "updates" || text == "all" ||
           text == "all_versions" || text == "lifetime";
}

bool hasUpdateEntitlement(const nlohmann::json& object) {
    static constexpr auto fields = std::to_array<std::string_view>({
        "update", "updates", "hasUpdate", "hasUpdates", "updateAccess", "allVersions",
        "allReleases", "canUseAllVersions", "universal",
    });
    if (!object.is_object()) return false;
    for (const auto field : fields) {
        const auto found = object.find(field);
        if (found != object.end() && truthy(*found)) return true;
    }
    return false;
}

bool releaseMatches(std::string_view entitled, std::string_view requested) {
    if (entitled == requested) return true;
    const auto separator = requested.find('+');
    return separator != std::string_view::npos &&
           (entitled == requested.substr(0, separator) || entitled == requested.substr(separator + 1));
}

bool releaseEntitlementMatches(const nlohmann::json& response, std::string_view requested) {
    if (requested.empty() || hasUpdateEntitlement(response)) return true;
    static constexpr auto fields = std::to_array<std::string_view>({
        "release", "entitledRelease", "version", "entitledVersion", "purchasedRelease",
        "purchasedVersion",
    });
    bool scoped{};
    const auto inspect = [&](const nlohmann::json& object) {
        if (!object.is_object()) return false;
        if (hasUpdateEntitlement(object)) return true;
        for (const auto field : fields) {
            const auto found = object.find(field);
            if (found == object.end() || !found->is_primitive() || found->is_null()) continue;
            scoped = true;
            std::string value;
            if (found->is_string()) value = found->get<std::string>();
            else value = found->dump();
            if (releaseMatches(value, requested)) return true;
        }
        return false;
    };
    if (inspect(response)) return true;
    const auto entitlement = response.find("entitlement");
    if (entitlement != response.end() && inspect(*entitlement)) return true;
    return !scoped;
}

nlohmann::json requestPayload(const MachineIdentity& identity, const AuthOptions& options) {
    nlohmann::json payload{
        {"hwid", identity.hwid},
        {"hwidScheme", HwidScheme},
        {"release", AuthRelease},
        {"version", AuthRelease},
        {"modVersion", BedrockClientVersion},
        {"client", AuthClientName},
        {"platform", "bedrock-windows"},
    };
    if (!identity.legacyHwids.empty()) payload["legacyHwids"] = identity.legacyHwids;
    if (!options.minecraftVersion.empty()) payload["minecraftVersion"] = options.minecraftVersion;
    return payload;
}

const std::wstring& userAgent() {
    // The server reads `Soundify-Reborn/<release>` like the Java builds send.
    static const std::wstring value = [] {
        std::wstring agent = L"Soundify-Reborn/";
        for (const char ch : AuthRelease) agent.push_back(static_cast<wchar_t>(ch));
        return agent;
    }();
    return value;
}

std::optional<nlohmann::json> parseResponse(const HttpResponse& http) {
    if (http.body.empty()) return std::nullopt;
    auto response = nlohmann::json::parse(http.body, nullptr, false);
    if (response.is_discarded() || !response.is_object()) return std::nullopt;
    return response;
}

AuthResult failureResult(const HttpResponse& http, const std::optional<nlohmann::json>& response,
                         std::string fallback) {
    auto message = response ? serverMessage(*response, std::move(fallback)) : std::move(fallback);
    if (http.status == 401 || http.status == 403) return {AuthStatus::Rejected, std::move(message)};
    if (http.status == 426) return {AuthStatus::UpdateRequired, std::move(message)};
    if (http.status < 200 || http.status >= 300) {
        if (!response) message = "Login server error (HTTP " + std::to_string(http.status) + ")";
        return {AuthStatus::NetworkError, std::move(message)};
    }
    if (!response) return {AuthStatus::InvalidResponse, "Login server returned an invalid response"};
    return {AuthStatus::Rejected, std::move(message)};
}

std::wstring endpoint(const std::wstring& base, std::wstring_view path) {
    if (base.empty()) throw std::runtime_error("Authentication URL is empty");
    if (base.back() == L'/') return base.substr(0, base.size() - 1) + std::wstring(path);
    return base + std::wstring(path);
}

int milliseconds(std::chrono::milliseconds value) {
    return static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(value.count(), 1, std::numeric_limits<int>::max()));
}

} // namespace

std::vector<std::wstring> defaultAuthHosts() {
    return {L"https://mods.gduck.site/soundify-auth", L"https://soundifyauth.gduckmc.workers.dev"};
}

std::size_t preferredHostIndex() noexcept {
    return preferredHost.load();
}

void preferHostIndex(std::size_t index) noexcept {
    preferredHost.store(index);
}

HttpResponse WinHttpTransport::postJson(std::wstring_view url, std::string_view body,
                                        std::wstring_view agent, const RequestTimeouts& timeouts) {
    if (url.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()) ||
        body.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
        throw std::runtime_error("Authentication request is too large");
    }
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    std::wstring mutableUrl(url);
    if (!WinHttpCrackUrl(mutableUrl.data(), static_cast<DWORD>(mutableUrl.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS) {
        throw std::runtime_error("Authentication endpoint must use HTTPS");
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    InternetHandle session(WinHttpOpen(std::wstring(agent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) throw std::runtime_error("Could not initialize HTTPS");
    WinHttpSetTimeouts(session.get(), milliseconds(timeouts.resolve), milliseconds(timeouts.connect),
                       milliseconds(timeouts.send), milliseconds(timeouts.receive));
    InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
    if (!connection) throw std::runtime_error("Could not connect to authentication service");
    InternetHandle request(WinHttpOpenRequest(connection.get(), L"POST", path.c_str(), nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE));
    if (!request) throw std::runtime_error("Could not create authentication request");
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    constexpr wchar_t headers[] = L"Content-Type: application/json; charset=utf-8\r\nAccept: application/json\r\n";
    const BOOL sent = WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1),
                                         const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                                         static_cast<DWORD>(body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(request.get(), nullptr))
        throw std::runtime_error("Authentication request failed (" + std::to_string(GetLastError()) + ")");
    DWORD status{};
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        throw std::runtime_error("Authentication response has no status");
    }
    std::string response;
    while (true) {
        DWORD available{};
        if (!WinHttpQueryDataAvailable(request.get(), &available))
            throw std::runtime_error("Could not read authentication response");
        if (available == 0) break;
        if (response.size() + available > MaximumResponseBytes) {
            throw std::runtime_error("Authentication response is too large");
        }
        const auto offset = response.size();
        response.resize(offset + available);
        DWORD read{};
        if (!WinHttpReadData(request.get(), response.data() + offset, available, &read))
            throw std::runtime_error("Could not read authentication response");
        response.resize(offset + read);
    }
    return {status, std::move(response)};
}

AuthClient::AuthClient(ProtectedTokenStore& tokenStore, HttpTransport& transport,
                       IdentityProvider identity, AuthOptions options)
    : tokenStore_(tokenStore), transport_(transport), identity_(std::move(identity)), options_(std::move(options)) {}

HttpResponse AuthClient::post(std::wstring_view path, std::string_view body) {
    return postWithFallback(transport_, options_.hosts, path, body, options_.budget);
}

HttpResponse postWithFallback(HttpTransport& transport, const std::vector<std::wstring>& hosts,
                              std::wstring_view path, std::string_view body, std::chrono::milliseconds budget) {
    if (hosts.empty()) throw UnreachableError();
    const auto deadline = std::chrono::steady_clock::now() + budget;
    const auto first = preferredHostIndex() % hosts.size();
    for (std::size_t attempt = 0; attempt < hosts.size(); ++attempt) {
        const auto index = (first + attempt) % hosts.size();
        const bool lastHost = attempt + 1 == hosts.size();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining < MinimumAttemptTime) break;
        RequestTimeouts timeouts;
        timeouts.resolve = std::min(timeouts.resolve, remaining);
        timeouts.connect = std::min(timeouts.connect, remaining);
        timeouts.send = std::min(timeouts.send, remaining);
        // A host that accepts but never answers must leave time for the next one.
        timeouts.receive = lastHost ? remaining : std::min(timeouts.receive, remaining);
        try {
            auto response = transport.postJson(endpoint(hosts[index], path), body, userAgent(), timeouts);
            if (response.status >= 500 && !lastHost) continue;
            preferHostIndex(index);
            return response;
        } catch (const std::exception&) {
            // Unreachable or blocked: try the next host.
        }
    }
    throw UnreachableError();
}

AuthResult AuthClient::login(std::string username, std::string password) {
    if (username.empty() || password.empty()) {
        secureClear(password);
        return {AuthStatus::Rejected, "Enter username and password"};
    }
    if (username.size() > 64 || password.size() > 128) {
        secureClear(password);
        return {AuthStatus::Rejected, "Username or password is too long"};
    }
    if (!identity_) {
        secureClear(password);
        return {AuthStatus::StorageError, "Sign in from Soundify Launcher"};
    }
    MachineIdentity identity;
    try {
        identity = identity_();
    } catch (const std::exception&) {
        secureClear(password);
        return {AuthStatus::StorageError, "Could not read this device's hardware ID"};
    }
    try {
        auto payload = requestPayload(identity, options_);
        payload["username"] = std::move(username);
        payload["password"] = password;
        std::string request;
        try {
            request = payload.dump();
        } catch (...) {
            secureClear(payload["password"].get_ref<std::string&>());
            secureClear(password);
            throw;
        }
        secureClear(payload["password"].get_ref<std::string&>());
        payload.erase("password");
        secureClear(password);
        HttpResponse http;
        try {
            http = post(L"/auth", request);
        } catch (...) {
            secureClear(request);
            throw;
        }
        secureClear(request);
        auto parsed = parseResponse(http);
        secureClear(http.body);
        const bool success = parsed && parsed->contains("success") && (*parsed)["success"].is_boolean() &&
                             (*parsed)["success"].get<bool>();
        if (!success) return failureResult(http, parsed, "Login failed");
        if (!releaseEntitlementMatches(*parsed, AuthRelease)) {
            return {AuthStatus::VersionDenied, "This account does not include Soundify for Bedrock"};
        }
        const auto tokenValue = parsed->find("token");
        if (tokenValue == parsed->end() || !tokenValue->is_string() || tokenValue->get_ref<const std::string&>().empty()) {
            return {AuthStatus::InvalidResponse, "Login server did not return a session"};
        }
        auto token = tokenValue->get<std::string>();
        secureClear(tokenValue->get_ref<std::string&>());
        auto verified = verifyToken(identity, token);
        if (verified.authenticated()) saveSession(identity, token);
        secureClear(token);
        return verified;
    } catch (const UnreachableError& error) {
        secureClear(password);
        return {AuthStatus::NetworkError, error.what()};
    } catch (const std::exception& error) {
        secureClear(password);
        return {AuthStatus::StorageError, error.what()};
    }
}

AuthResult AuthClient::verifyStored() {
    std::optional<Session> session;
    try {
        session = loadSession();
    } catch (const std::exception&) {
        return {AuthStatus::StorageError, "Saved login could not be read. Sign in again."};
    }
    if (!session) return {AuthStatus::LoginRequired, "Sign in to activate Soundify"};

    MachineIdentity identity = session->identity;
    if (identity_) {
        try {
            identity = identity_();
        } catch (const std::exception&) {
            secureClear(session->token);
            return {AuthStatus::StorageError, "Could not read this device's hardware ID"};
        }
        // A session saved under an id an earlier build used on this PC moves to the current id.
        if (!identity.owns(session->identity.hwid)) {
            secureClear(session->token);
            tokenStore_.clear();
            return {AuthStatus::LoginRequired, "Saved login belongs to another device. Sign in again."};
        }
    } else if (session->legacyFormat) {
        // Saved by 0.3.x under its installation key; the launcher moves it to the device id.
        secureClear(session->token);
        return {AuthStatus::LoginRequired, "Open Soundify Launcher once to refresh your login"};
    }

    auto result = verifyToken(identity, session->token);
    if (result.authenticated() &&
        (session->legacyFormat || identity.hwid != session->identity.hwid ||
         identity.legacyHwids != session->identity.legacyHwids)) {
        try {
            saveSession(identity, session->token);
        } catch (const std::exception&) {
            // The verified session still works for this run.
        }
    }
    secureClear(session->token);
    if (result.status == AuthStatus::Rejected || result.status == AuthStatus::VersionDenied ||
        result.status == AuthStatus::InvalidResponse) {
        tokenStore_.clear();
    }
    return result;
}

AuthResult AuthClient::verifyToken(const MachineIdentity& identity, std::string_view token) {
    if (token.empty() || token == "UNKNOWN") return {AuthStatus::LoginRequired, "Session token is missing"};
    try {
        auto payload = requestPayload(identity, options_);
        payload["token"] = token;
        auto request = payload.dump();
        secureClear(payload["token"].get_ref<std::string&>());
        payload.erase("token");
        HttpResponse http;
        try {
            http = post(L"/verify", request);
        } catch (...) {
            secureClear(request);
            throw;
        }
        secureClear(request);
        auto parsed = parseResponse(http);
        secureClear(http.body);
        const bool success = parsed && parsed->contains("success") && (*parsed)["success"].is_boolean() &&
                             (*parsed)["success"].get<bool>();
        const bool active = parsed && parsed->contains("active") && (*parsed)["active"].is_boolean() &&
                            (*parsed)["active"].get<bool>();
        if (!success || !active) return failureResult(http, parsed, "Session is no longer active");
        if (!releaseEntitlementMatches(*parsed, AuthRelease)) {
            return {AuthStatus::VersionDenied, "This account does not include Soundify for Bedrock"};
        }
        return {AuthStatus::Authenticated, "Soundify license is active"};
    } catch (const UnreachableError& error) {
        return {AuthStatus::NetworkError, error.what()};
    } catch (const std::exception& error) {
        return {AuthStatus::NetworkError, error.what()};
    }
}

std::optional<AuthClient::Session> AuthClient::loadSession() const {
    auto stored = tokenStore_.load();
    if (!stored) return std::nullopt;
    Session session;
    if (!stored->empty() && stored->front() == '{') {
        auto json = nlohmann::json::parse(*stored, nullptr, false);
        secureClear(*stored);
        if (json.is_discarded() || !json.is_object() || json.value("format", 0) != SessionFormat) return std::nullopt;
        session.identity.hwid = json.value("hwid", std::string{});
        const auto legacy = json.find("legacyHwids");
        if (legacy != json.end() && legacy->is_array()) {
            for (const auto& id : *legacy) {
                if (id.is_string() && session.identity.legacyHwids.size() < MaxLegacyHwids)
                    session.identity.legacyHwids.push_back(id.get<std::string>());
            }
        }
        const auto token = json.find("token");
        if (token != json.end() && token->is_string()) {
            session.token = token->get<std::string>();
            secureClear(token->get_ref<std::string&>());
        }
    } else {
        // 0.3.x: "<installation id>\n<token>".
        const auto separator = stored->find('\n');
        if (separator != std::string::npos) {
            session.identity.hwid = stored->substr(0, separator);
            session.token = stored->substr(separator + 1);
            session.legacyFormat = true;
        }
        secureClear(*stored);
    }
    if (session.identity.hwid.empty() || session.token.empty()) {
        secureClear(session.token);
        return std::nullopt;
    }
    return session;
}

void AuthClient::saveSession(const MachineIdentity& identity, std::string_view token) const {
    nlohmann::json value{
        {"format", SessionFormat},
        {"hwid", identity.hwid},
        {"legacyHwids", identity.legacyHwids},
        {"token", token},
    };
    auto serialized = value.dump();
    secureClear(value["token"].get_ref<std::string&>());
    try {
        tokenStore_.save(serialized);
    } catch (...) {
        secureClear(serialized);
        throw;
    }
    secureClear(serialized);
}

void AuthClient::logout() const {
    tokenStore_.clear();
}

std::filesystem::path defaultTokenPath() {
    wchar_t* localAppData{};
    std::size_t length{};
    if (_wdupenv_s(&localAppData, &length, L"LOCALAPPDATA") == 0 && localAppData && *localAppData) {
        const auto result = std::filesystem::path(localAppData) / L"Soundify Reborn Bedrock" / L"auth.bin";
        std::free(localAppData);
        return result;
    }
    std::free(localAppData);
    throw std::runtime_error("LOCALAPPDATA is unavailable");
}

} // namespace soundify::security
