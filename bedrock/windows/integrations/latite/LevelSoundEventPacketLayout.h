#pragma once

#include "mc/common/network/Packet.h"
#include "util/LMath.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace soundify::protocol {

// Mojang protocols 2169 (26.45) and 2193 (26.52, same schema) / packet 0x7B payload. The payload is the second base of
// LevelSoundEventPacket and starts immediately after Latite's 0x30-byte Packet.
// Keep these assertions: silently accepting a changed STL or packet layout would
// turn a sound replacement into memory corruption.
using SoundEventIdentifier = std::variant<std::monostate, std::uint32_t, std::string>;

struct LevelSoundEventPayload final {
    std::string actorIdentifier;
    std::int64_t actorUniqueId;
    SoundEventIdentifier soundEvent;
    Vec3 position;
    int data;
    bool global;
    bool baby;
    std::optional<Vec3> fireAtPosition;
};

constexpr std::uint8_t LevelSoundEventPacketId = 0x7B;
constexpr std::uint32_t ExplodeEvent = 57;
constexpr std::uint32_t MaceSmashAirEvent = 476;
constexpr std::uint32_t MaceSmashGroundEvent = 477;
constexpr std::uint32_t MaceHeavySmashGroundEvent = 478;

inline bool isMaceSmashEvent(const SoundEventIdentifier& event) {
    if (const auto* numeric = std::get_if<std::uint32_t>(&event)) {
        return *numeric == MaceSmashAirEvent || *numeric == MaceSmashGroundEvent ||
               *numeric == MaceHeavySmashGroundEvent;
    }
    if (const auto* named = std::get_if<std::string>(&event)) {
        return *named == "mace.smash_air" || *named == "mace.smash_ground" ||
               *named == "mace.heavy_smash_ground";
    }
    return false;
}

static_assert(sizeof(SDK::Packet) == 0x30);
static_assert(sizeof(SoundEventIdentifier) == 0x28);
static_assert(offsetof(LevelSoundEventPayload, actorIdentifier) == 0x00);
static_assert(offsetof(LevelSoundEventPayload, actorUniqueId) == 0x20);
static_assert(offsetof(LevelSoundEventPayload, soundEvent) == 0x28);
static_assert(offsetof(LevelSoundEventPayload, position) == 0x50);
static_assert(offsetof(LevelSoundEventPayload, data) == 0x5C);
static_assert(offsetof(LevelSoundEventPayload, global) == 0x60);
static_assert(offsetof(LevelSoundEventPayload, baby) == 0x61);
static_assert(offsetof(LevelSoundEventPayload, fireAtPosition) == 0x64);
static_assert(sizeof(LevelSoundEventPayload) == 0x78);

inline LevelSoundEventPayload* payload(SDK::Packet* packet) {
    return reinterpret_cast<LevelSoundEventPayload*>(
        reinterpret_cast<std::byte*>(packet) + sizeof(SDK::Packet));
}

} // namespace soundify::protocol
