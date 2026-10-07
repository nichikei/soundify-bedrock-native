#include "soundify/security/ProtectedTokenStore.hpp"

#include <windows.h>
#include <dpapi.h>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace soundify::security {
namespace {

DATA_BLOB entropy() {
    static char value[] = "Soundify-Reborn-Bedrock-v1";
    return {static_cast<DWORD>(sizeof(value) - 1),
            reinterpret_cast<BYTE*>(value)};
}

} // namespace

ProtectedTokenStore::ProtectedTokenStore(std::filesystem::path path) : path_(std::move(path)) {}

void ProtectedTokenStore::save(std::string_view token) const {
    DATA_BLOB input{static_cast<DWORD>(token.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(token.data()))};
    auto extra = entropy();
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&input, L"Soundify session token", &extra, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) {
        throw std::runtime_error("CryptProtectData failed");
    }

    if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path());
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(encrypted.pbData), encrypted.cbData);
    LocalFree(encrypted.pbData);
    if (!output) throw std::runtime_error("Could not write protected token");
}

std::optional<std::string> ProtectedTokenStore::load() const {
    std::ifstream input(path_, std::ios::binary);
    if (!input) return std::nullopt;
    std::vector<BYTE> bytes((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    if (bytes.empty()) return std::nullopt;

    DATA_BLOB encrypted{static_cast<DWORD>(bytes.size()), bytes.data()};
    auto extra = entropy();
    DATA_BLOB plain{};
    if (!CryptUnprotectData(&encrypted, nullptr, &extra, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &plain)) {
        throw std::runtime_error("CryptUnprotectData failed");
    }
    std::string result(reinterpret_cast<const char*>(plain.pbData), plain.cbData);
    SecureZeroMemory(plain.pbData, plain.cbData);
    LocalFree(plain.pbData);
    return result;
}

void ProtectedTokenStore::clear() const {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
}

} // namespace soundify::security
