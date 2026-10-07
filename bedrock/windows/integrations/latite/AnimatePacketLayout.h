#pragma once

#include "mc/common/network/Packet.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace soundify::protocol {

// Mojang protocols 2169 (26.45) and 2193 (26.52, same schema) / packet 0x2C payload. CriticalHit is action 4.
// Generated 26.45 symbols place the runtime ID before the action in memory,
// although the wire schema serializes the action first.
struct AnimatePayload final {
    std::uint64_t targetRuntimeId;
    std::uint8_t action;
    float data;
    std::optional<std::uint8_t> swingSource;
};

constexpr std::uint8_t AnimatePacketId = 0x2C;
constexpr std::uint8_t CriticalHitAction = 4;

static_assert(sizeof(SDK::Packet) == 0x30);
static_assert(offsetof(AnimatePayload, targetRuntimeId) == 0x00);
static_assert(offsetof(AnimatePayload, action) == 0x08);
static_assert(offsetof(AnimatePayload, data) == 0x0C);
static_assert(offsetof(AnimatePayload, swingSource) == 0x10);
static_assert(sizeof(AnimatePayload) == 0x18);

inline AnimatePayload* animatePayload(SDK::Packet* packet) {
    return reinterpret_cast<AnimatePayload*>(reinterpret_cast<std::byte*>(packet) + sizeof(SDK::Packet));
}

} // namespace soundify::protocol
