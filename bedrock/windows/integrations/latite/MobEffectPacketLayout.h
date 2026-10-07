#pragma once

#include "mc/common/network/Packet.h"

#include <cstddef>
#include <cstdint>

namespace soundify::protocol {

// Mojang protocols 2169 (26.45) and 2193 (26.52, same schema) / packet 0x1C payload. This mirrors the generated
// MobEffectPacketPayload for 26.40-26.52 and begins after Latite's Packet base.
struct MobEffectPayload final {
    std::uint64_t runtimeId;
    std::int32_t durationTicks;
    std::uint8_t eventId;
    std::byte eventPadding[3];
    std::int32_t effectId;
    std::int32_t amplifier;
    bool showParticles;
    bool ambient;
    std::byte tickPadding[6];
    std::uint64_t tick;
};

constexpr std::uint8_t MobEffectPacketId = 0x1C;

static_assert(sizeof(SDK::Packet) == 0x30);
static_assert(offsetof(MobEffectPayload, runtimeId) == 0x00);
static_assert(offsetof(MobEffectPayload, durationTicks) == 0x08);
static_assert(offsetof(MobEffectPayload, eventId) == 0x0C);
static_assert(offsetof(MobEffectPayload, effectId) == 0x10);
static_assert(offsetof(MobEffectPayload, amplifier) == 0x14);
static_assert(offsetof(MobEffectPayload, showParticles) == 0x18);
static_assert(offsetof(MobEffectPayload, ambient) == 0x19);
static_assert(offsetof(MobEffectPayload, tick) == 0x20);
static_assert(sizeof(MobEffectPayload) == 0x28);

inline MobEffectPayload* mobEffectPayload(SDK::Packet* packet) {
    return reinterpret_cast<MobEffectPayload*>(reinterpret_cast<std::byte*>(packet) + sizeof(SDK::Packet));
}

} // namespace soundify::protocol
