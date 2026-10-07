#include "soundify/security/MachineId.hpp"

#include "soundify/security/InstallationIdentity.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace soundify::security {
namespace {

using Microsoft::WRL::ComPtr;

// SHA-256 of an empty string: what a build sends when it read nothing.
constexpr std::string_view EmptyInputSha256 =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

bool isJavaWhitespace(char ch) {
    return static_cast<unsigned char>(ch) <= 0x20;
}

std::string_view javaTrim(std::string_view value) {
    while (!value.empty() && isJavaWhitespace(value.front())) value.remove_prefix(1);
    while (!value.empty() && isJavaWhitespace(value.back())) value.remove_suffix(1);
    return value;
}

// Java's hash(): an empty input gives no id rather than the empty-input digest.
std::string javaHash(std::string_view raw) {
    return raw.empty() ? std::string{} : sha256Hex(raw);
}

std::string utf8(const wchar_t* text, std::size_t length) {
    if (!text || length == 0) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), result.data(), size, nullptr, nullptr);
    return result;
}

class ComScope final {
public:
    ComScope() : status_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
        // An STA thread can still use WMI; only an unusable COM runtime is fatal.
        if (FAILED(status_) && status_ != RPC_E_CHANGED_MODE) throw std::runtime_error("COM is unavailable");
    }
    ~ComScope() {
        if (SUCCEEDED(status_)) CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT status_;
};

class Bstr final {
public:
    explicit Bstr(const wchar_t* value) : value_(SysAllocString(value)) {
        if (!value_) throw std::bad_alloc();
    }
    ~Bstr() { SysFreeString(value_); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    [[nodiscard]] BSTR get() const noexcept { return value_; }

private:
    BSTR value_;
};

ComPtr<IWbemServices> connectNamespace(IWbemLocator& locator, const wchar_t* name) {
    ComPtr<IWbemServices> services;
    const Bstr resource(name);
    if (FAILED(locator.ConnectServer(resource.get(), nullptr, nullptr, nullptr,
                                     WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &services))) {
        return nullptr;
    }
    // Per-proxy security; CoInitializeSecurity is process-wide and belongs to the host process.
    if (FAILED(CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_DEFAULT, RPC_C_AUTHZ_NONE,
                                 COLE_DEFAULT_PRINCIPAL, RPC_C_AUTHN_LEVEL_CALL,
                                 RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE))) {
        return nullptr;
    }
    return services;
}

template <typename Visit>
bool forEachInstance(IWbemServices& services, const wchar_t* wql, Visit visit) {
    const Bstr language(L"WQL");
    const Bstr query(wql);
    ComPtr<IEnumWbemClassObject> rows;
    if (FAILED(services.ExecQuery(language.get(), query.get(),
                                  WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &rows))) {
        return false;
    }
    while (true) {
        ComPtr<IWbemClassObject> row;
        ULONG count{};
        const auto status = rows->Next(10000, 1, &row, &count);
        if (FAILED(status) || status == WBEM_S_TIMEDOUT) return false;
        if (count == 0) return true;
        visit(*row.Get());
    }
}

std::optional<std::string> stringProperty(IWbemClassObject& row, const wchar_t* name) {
    VARIANT value;
    VariantInit(&value);
    if (FAILED(row.Get(name, 0, &value, nullptr, nullptr))) return std::nullopt;
    std::optional<std::string> result;
    if (value.vt == VT_BSTR && value.bstrVal) result = utf8(value.bstrVal, SysStringLen(value.bstrVal));
    VariantClear(&value);
    return result;
}

std::optional<std::uint32_t> unsignedProperty(IWbemClassObject& row, const wchar_t* name) {
    VARIANT value;
    VariantInit(&value);
    if (FAILED(row.Get(name, 0, &value, nullptr, nullptr))) return std::nullopt;
    std::optional<std::uint32_t> result;
    switch (value.vt) {
    case VT_I4: result = static_cast<std::uint32_t>(value.lVal); break;
    case VT_UI4: result = value.ulVal; break;
    case VT_I2: result = static_cast<std::uint16_t>(value.iVal); break;
    case VT_UI2: result = value.uiVal; break;
    default: break;
    }
    VariantClear(&value);
    return result;
}

std::optional<wchar_t> systemDriveLetter() {
    std::array<wchar_t, 16> buffer{};
    const auto length = GetEnvironmentVariableW(L"SystemDrive", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return std::nullopt;
    return static_cast<wchar_t>(std::towupper(buffer[0]));
}

// Get-Partition -DriveLetter; nothing when the storage provider or the partition is missing,
// exactly when the Java query falls back to the first disk.
std::optional<std::uint32_t> systemDiskNumber(IWbemLocator& locator) {
    const auto letter = systemDriveLetter();
    if (!letter) return std::nullopt;
    auto storage = connectNamespace(locator, L"ROOT\\Microsoft\\Windows\\Storage");
    if (!storage) return std::nullopt;
    std::optional<std::uint32_t> found;
    forEachInstance(*storage.Get(), L"SELECT DiskNumber, DriveLetter FROM MSFT_Partition", [&](IWbemClassObject& row) {
        const auto partitionLetter = unsignedProperty(row, L"DriveLetter");
        if (found || !partitionLetter || *partitionLetter == 0) return;
        if (static_cast<wchar_t>(std::towupper(static_cast<wint_t>(*partitionLetter))) == *letter)
            found = unsignedProperty(row, L"DiskNumber");
    });
    return found;
}

std::optional<std::filesystem::path> environmentDirectory(const wchar_t* name) {
    wchar_t* value{};
    std::size_t length{};
    if (_wdupenv_s(&value, &length, name) != 0 || !value || !*value) {
        std::free(value);
        return std::nullopt;
    }
    std::filesystem::path result(value);
    std::free(value);
    return result;
}

std::string readSmallFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::array<char, 4096> buffer{};
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    return std::string(buffer.data(), static_cast<std::size_t>(input.gcount()));
}

// Ids earlier builds left on this PC: the Java `.hwid_cache` files (APPDATA, then user home)
// and the installation key that Bedrock 0.3.14-0.3.23 sent when no Java cache existed.
std::vector<std::string> cachedIds() {
    std::vector<std::string> result;
    for (const auto* variable : {L"APPDATA", L"USERPROFILE"}) {
        if (const auto directory = environmentDirectory(variable))
            result.push_back(readSmallFile(*directory / L".soundify" / L".hwid_cache"));
    }
    try {
        if (auto previous = InstallationIdentity::existingId()) result.push_back(std::move(*previous));
    } catch (const std::exception&) {
        // A broken key store must not block the hardware id.
    }
    return result;
}

} // namespace

bool MachineIdentity::owns(std::string_view id) const {
    return !id.empty() && (id == hwid || std::find(legacyHwids.begin(), legacyHwids.end(), id) != legacyHwids.end());
}

std::string joinTrimmedLines(std::string_view output) {
    std::string result;
    std::size_t start{};
    while (start <= output.size()) {
        const auto end = output.find_first_of("\r\n", start);
        const auto line = output.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        result += javaTrim(line);
        if (end == std::string_view::npos) break;
        // "\r\n" is one line break for Java's BufferedReader.
        start = end + ((output[end] == '\r' && end + 1 < output.size() && output[end + 1] == '\n') ? 2 : 1);
    }
    return result;
}

std::string sha256Hex(std::string_view text) {
    std::array<std::uint8_t, 32> digest{};
    const auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                                   reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),
                                   static_cast<ULONG>(text.size()), digest.data(), static_cast<ULONG>(digest.size()));
    if (!BCRYPT_SUCCESS(status)) throw std::runtime_error("SHA-256 failed");
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 0x0f]);
    }
    return hex;
}

bool isIdentifyingHwid(std::string_view value) {
    return value.size() == 64 && value != EmptyInputSha256 &&
           std::all_of(value.begin(), value.end(), [](char ch) {
               return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
           });
}

std::string mainboardOutput(const HardwareSerials& serials) {
    std::string printed;
    for (const auto& serial : serials.baseboards) {
        if (!serial) continue;
        printed += *serial;
        printed += '\n';
    }
    return joinTrimmedLines(printed);
}

std::string systemDiskOutput(const HardwareSerials& serials) {
    const DiskDrive* drive{};
    if (serials.systemDiskNumber) {
        const auto found = std::find_if(serials.diskDrives.begin(), serials.diskDrives.end(),
                                        [&](const DiskDrive& candidate) { return candidate.index == *serials.systemDiskNumber; });
        if (found != serials.diskDrives.end()) drive = &*found;
    } else if (!serials.diskDrives.empty()) {
        drive = &serials.diskDrives.front();
    }
    return drive && drive->serial ? joinTrimmedLines(*drive->serial) : std::string{};
}

MachineIdentity computeMachineIdentity(std::string_view mainboard, std::string_view disk,
                                       const std::vector<std::string>& cached) {
    std::string primary = javaHash(std::string(mainboard) + std::string(disk));
    // Insertion-ordered set, as Java's LinkedHashSet.
    std::vector<std::string> candidates;
    const auto add = [&](std::string id) {
        if (std::find(candidates.begin(), candidates.end(), id) == candidates.end()) candidates.push_back(std::move(id));
    };
    // Soundify 1.3 / 1.3.1 for 1.21.11, 26.1 1.3 and 26.2 1.3 hashed the mainboard serial alone.
    if (auto boardOnly = javaHash(javaTrim(mainboard)); isIdentifyingHwid(boardOnly)) add(std::move(boardOnly));
    for (const auto& raw : cached) {
        const auto value = javaTrim(raw);
        if (value.size() == 64 && std::all_of(value.begin(), value.end(), [](char ch) {
                return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            })) add(std::string(value));
    }

    MachineIdentity identity;
    for (auto& candidate : candidates) {
        if (isIdentifyingHwid(candidate) && candidate != primary && identity.legacyHwids.size() < MaxLegacyHwids)
            identity.legacyHwids.push_back(std::move(candidate));
    }
    if (!isIdentifyingHwid(primary)) {
        primary.clear();
        if (!identity.legacyHwids.empty()) {
            primary = std::move(identity.legacyHwids.front());
            identity.legacyHwids.erase(identity.legacyHwids.begin());
        }
    }
    identity.hwid = std::move(primary);
    return identity;
}

HardwareSerials readHardwareSerials() {
    const ComScope com;
    ComPtr<IWbemLocator> locator;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator))))
        throw std::runtime_error("WMI is unavailable");
    auto cimv2 = connectNamespace(*locator.Get(), L"ROOT\\CIMV2");
    if (!cimv2) throw std::runtime_error("WMI is unavailable");

    // A failed query prints nothing to Java's stdout, so it reads as an empty value here too.
    HardwareSerials serials;
    forEachInstance(*cimv2.Get(), L"SELECT SerialNumber FROM Win32_BaseBoard", [&](IWbemClassObject& row) {
        serials.baseboards.push_back(stringProperty(row, L"SerialNumber"));
    });
    forEachInstance(*cimv2.Get(), L"SELECT Index, SerialNumber FROM Win32_DiskDrive", [&](IWbemClassObject& row) {
        if (const auto index = unsignedProperty(row, L"Index"))
            serials.diskDrives.push_back({*index, stringProperty(row, L"SerialNumber")});
    });
    serials.systemDiskNumber = systemDiskNumber(*locator.Get());
    return serials;
}

MachineIdentity currentMachineIdentity() {
    static std::mutex mutex;
    static std::optional<MachineIdentity> cached;
    std::lock_guard lock(mutex);
    if (cached) return *cached;

    std::string mainboard;
    std::string disk;
    try {
        const auto serials = readHardwareSerials();
        mainboard = mainboardOutput(serials);
        disk = systemDiskOutput(serials);
    } catch (const std::exception&) {
        // Fall back to an id an earlier build cached on this PC, as the Java mod does.
    }
    auto identity = computeMachineIdentity(mainboard, disk, cachedIds());
    if (identity.hwid.empty()) throw std::runtime_error("Could not read this device's hardware ID");
    cached = identity;
    return identity;
}

} // namespace soundify::security
