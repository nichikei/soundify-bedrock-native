#include "soundify/security/InstallationIdentity.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <ncrypt.h>

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace soundify::security {
namespace {

void checkSecurityStatus(SECURITY_STATUS status, const char* operation) {
    if (status != ERROR_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with status " +
                                 std::to_string(static_cast<unsigned long>(status)));
    }
}

std::vector<std::uint8_t> sha256(const std::uint8_t* data, std::size_t size) {
    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }

    std::vector<std::uint8_t> digest(32);
    const auto status = BCryptHash(algorithm, nullptr, 0,
                                   const_cast<PUCHAR>(data), static_cast<ULONG>(size),
                                   digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        throw std::runtime_error("BCryptHash failed");
    }
    return digest;
}

std::pair<NCRYPT_PROV_HANDLE, NCRYPT_KEY_HANDLE> openExisting(
    const wchar_t* providerName, const std::wstring& keyName) {
    NCRYPT_PROV_HANDLE provider{};
    if (NCryptOpenStorageProvider(&provider, providerName, 0) != ERROR_SUCCESS) {
        return {};
    }

    NCRYPT_KEY_HANDLE key{};
    auto status = NCryptOpenKey(provider, &key, keyName.c_str(), 0, NCRYPT_SILENT_FLAG);
    if (status == ERROR_SUCCESS) {
        return {provider, key};
    }

    NCryptFreeObject(provider);
    return {};
}

std::pair<NCRYPT_PROV_HANDLE, NCRYPT_KEY_HANDLE> create(
    const wchar_t* providerName, const std::wstring& keyName) {
    NCRYPT_PROV_HANDLE provider{};
    if (NCryptOpenStorageProvider(&provider, providerName, 0) != ERROR_SUCCESS) {
        return {};
    }

    NCRYPT_KEY_HANDLE key{};
    auto status = NCryptCreatePersistedKey(provider, &key, NCRYPT_ECDSA_P256_ALGORITHM,
                                           keyName.c_str(), 0, NCRYPT_SILENT_FLAG);
    if (status == ERROR_SUCCESS) {
        status = NCryptFinalizeKey(key, NCRYPT_SILENT_FLAG);
    }
    if (status != ERROR_SUCCESS) {
        if (key != 0) NCryptFreeObject(key);
        NCryptFreeObject(provider);
        return {};
    }
    return {provider, key};
}

} // namespace

InstallationIdentity::InstallationIdentity(std::wstring keyName) {
    // Reopen either existing identity before creating one in another provider.
    // This prevents the installation ID changing if TPM availability changes.
    auto handles = openExisting(MS_PLATFORM_CRYPTO_PROVIDER, keyName);
    if (handles.second == 0) {
        handles = openExisting(MS_KEY_STORAGE_PROVIDER, keyName);
    }
    if (handles.second == 0) {
        handles = create(MS_PLATFORM_CRYPTO_PROVIDER, keyName);
    }
    if (handles.second == 0) {
        handles = create(MS_KEY_STORAGE_PROVIDER, keyName);
    }
    if (handles.second == 0) {
        throw std::runtime_error("Could not open or create the Soundify installation key");
    }
    provider_ = static_cast<std::uintptr_t>(handles.first);
    key_ = static_cast<std::uintptr_t>(handles.second);
}

std::optional<std::string> InstallationIdentity::existingId(const std::wstring& keyName) {
    auto handles = openExisting(MS_PLATFORM_CRYPTO_PROVIDER, keyName);
    if (handles.second == 0) handles = openExisting(MS_KEY_STORAGE_PROVIDER, keyName);
    if (handles.second == 0) return std::nullopt;
    DWORD size{};
    std::vector<std::uint8_t> key;
    auto status = NCryptExportKey(handles.second, 0, BCRYPT_ECCPUBLIC_BLOB, nullptr, nullptr, 0, &size, 0);
    if (status == ERROR_SUCCESS) {
        key.resize(size);
        status = NCryptExportKey(handles.second, 0, BCRYPT_ECCPUBLIC_BLOB, nullptr, key.data(), size, &size, 0);
        key.resize(size);
    }
    NCryptFreeObject(handles.second);
    NCryptFreeObject(handles.first);
    if (status != ERROR_SUCCESS) return std::nullopt;
    return toHex(sha256(key.data(), key.size()));
}

InstallationIdentity::~InstallationIdentity() {
    if (key_ != 0) NCryptFreeObject(static_cast<NCRYPT_KEY_HANDLE>(key_));
    if (provider_ != 0) NCryptFreeObject(static_cast<NCRYPT_PROV_HANDLE>(provider_));
}

std::vector<std::uint8_t> InstallationIdentity::publicKey() const {
    DWORD size{};
    auto key = static_cast<NCRYPT_KEY_HANDLE>(key_);
    checkSecurityStatus(NCryptExportKey(key, 0, BCRYPT_ECCPUBLIC_BLOB, nullptr,
                                       nullptr, 0, &size, 0),
                        "NCryptExportKey(size)");
    std::vector<std::uint8_t> result(size);
    checkSecurityStatus(NCryptExportKey(key, 0, BCRYPT_ECCPUBLIC_BLOB, nullptr,
                                       result.data(), size, &size, 0),
                        "NCryptExportKey(data)");
    result.resize(size);
    return result;
}

std::string InstallationIdentity::id() const {
    const auto key = publicKey();
    return toHex(sha256(key.data(), key.size()));
}

std::vector<std::uint8_t> InstallationIdentity::sign(std::string_view challenge) const {
    const auto digest = sha256(reinterpret_cast<const std::uint8_t*>(challenge.data()),
                               challenge.size());
    DWORD size{};
    auto key = static_cast<NCRYPT_KEY_HANDLE>(key_);
    checkSecurityStatus(NCryptSignHash(key, nullptr,
                                      const_cast<PBYTE>(digest.data()),
                                      static_cast<DWORD>(digest.size()),
                                      nullptr, 0, &size, NCRYPT_SILENT_FLAG),
                        "NCryptSignHash(size)");
    std::vector<std::uint8_t> signature(size);
    checkSecurityStatus(NCryptSignHash(key, nullptr,
                                      const_cast<PBYTE>(digest.data()),
                                      static_cast<DWORD>(digest.size()),
                                      signature.data(), size, &size, NCRYPT_SILENT_FLAG),
                        "NCryptSignHash(data)");
    signature.resize(size);
    return signature;
}

std::string toHex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes) output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}

} // namespace soundify::security
