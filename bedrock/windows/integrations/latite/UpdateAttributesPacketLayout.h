#pragma once

#include "mc/common/network/Packet.h"
#include "mc/deps/core/StringUtils.h"

#include <cstddef>
#include <cstdint>

namespace soundify::protocol {

// The modifier vector is not inspected. Keeping its three-pointer ABI layout
// avoids depending on the much larger AttributeModifier type.
struct OpaqueVector final {
    void* begin;
    void* end;
    void* capacity;
};

struct AttributeData final {
    float currentValue;
    float minValue;
    float maxValue;
    float defaultValue;
    float defaultMinValue;
    float defaultMaxValue;
    SDK::HashedString name;
    OpaqueVector modifiers;
};

struct AttributeDataVector final {
    AttributeData* begin;
    AttributeData* end;
    AttributeData* capacity;

    [[nodiscard]] std::size_t validatedSize(std::size_t maximum) const {
        const auto first = reinterpret_cast<std::uintptr_t>(begin);
        const auto last = reinterpret_cast<std::uintptr_t>(end);
        const auto limit = reinterpret_cast<std::uintptr_t>(capacity);
        if (first == 0 && last == 0 && limit == 0) return 0;
        if (first == 0 || first > last || last > limit || (last - first) % sizeof(AttributeData) != 0)
            return maximum + 1;
        const auto count = (last - first) / sizeof(AttributeData);
        return count <= maximum ? count : maximum + 1;
    }
};

// Mojang protocols 2169 (26.45) and 2193 (26.52, same schema) / packet 0x1D payload. UpdateAttributesPacket derives
// from PayloadPacket, so its payload begins after Latite's 0x30-byte Packet.
struct UpdateAttributesPayload final {
    std::uint64_t runtimeId;
    AttributeDataVector attributes;
    std::uint64_t tick;
};

constexpr std::uint8_t UpdateAttributesPacketId = 0x1D;

static_assert(sizeof(SDK::Packet) == 0x30);
static_assert(sizeof(SDK::HashedString) == 0x30);
static_assert(sizeof(OpaqueVector) == 0x18);
static_assert(sizeof(AttributeDataVector) == 0x18);
static_assert(offsetof(AttributeData, currentValue) == 0x00);
static_assert(offsetof(AttributeData, minValue) == 0x04);
static_assert(offsetof(AttributeData, maxValue) == 0x08);
static_assert(offsetof(AttributeData, defaultValue) == 0x0C);
static_assert(offsetof(AttributeData, defaultMinValue) == 0x10);
static_assert(offsetof(AttributeData, defaultMaxValue) == 0x14);
static_assert(offsetof(AttributeData, name) == 0x18);
static_assert(offsetof(AttributeData, modifiers) == 0x48);
static_assert(sizeof(AttributeData) == 0x60);
static_assert(offsetof(UpdateAttributesPayload, runtimeId) == 0x00);
static_assert(offsetof(UpdateAttributesPayload, attributes) == 0x08);
static_assert(offsetof(UpdateAttributesPayload, tick) == 0x20);
static_assert(sizeof(UpdateAttributesPayload) == 0x28);

inline UpdateAttributesPayload* updateAttributesPayload(SDK::Packet* packet) {
    return reinterpret_cast<UpdateAttributesPayload*>(
        reinterpret_cast<std::byte*>(packet) + sizeof(SDK::Packet));
}

} // namespace soundify::protocol
