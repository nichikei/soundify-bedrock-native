#pragma once

#include "mc/common/network/Packet.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace soundify::protocol {

struct PacketVec3 final {
    float x;
    float y;
    float z;
};

// Mojang protocols 2169 (26.45) and 2193 (26.52, same schema) / packet 0x0D payload prefix. Only fields before
// velocity are represented because Soundify needs the actor type and spawn
// position. AddActorPacket derives from PayloadPacket, so the payload starts
// after Latite's 0x30-byte Packet base.
struct AddActorPayloadPrefix final {
    std::int64_t uniqueId;
    std::uint64_t runtimeId;
    std::string actorType;
    PacketVec3 position;
};

constexpr std::uint8_t AddActorPacketId = 0x0D;

static_assert(sizeof(SDK::Packet) == 0x30);
static_assert(sizeof(std::string) == 0x20);
static_assert(sizeof(PacketVec3) == 0x0C);
static_assert(offsetof(AddActorPayloadPrefix, uniqueId) == 0x00);
static_assert(offsetof(AddActorPayloadPrefix, runtimeId) == 0x08);
static_assert(offsetof(AddActorPayloadPrefix, actorType) == 0x10);
static_assert(offsetof(AddActorPayloadPrefix, position) == 0x30);

inline AddActorPayloadPrefix* addActorPayload(SDK::Packet* packet) {
    return reinterpret_cast<AddActorPayloadPrefix*>(
        reinterpret_cast<std::byte*>(packet) + sizeof(SDK::Packet));
}

} // namespace soundify::protocol
