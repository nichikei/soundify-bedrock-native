#pragma once

#include "mc/common/network/Packet.h"

#include <cstddef>
#include <cstdint>

namespace soundify::protocol {

struct RemoveActorPayload final {
    std::int64_t uniqueId;
};

constexpr std::uint8_t RemoveActorPacketId = 0x0E;

static_assert(sizeof(SDK::Packet) == 0x30);
static_assert(offsetof(RemoveActorPayload, uniqueId) == 0x00);
static_assert(sizeof(RemoveActorPayload) == 0x08);

inline RemoveActorPayload* removeActorPayload(SDK::Packet* packet) {
    return reinterpret_cast<RemoveActorPayload*>(
        reinterpret_cast<std::byte*>(packet) + sizeof(SDK::Packet));
}

} // namespace soundify::protocol
