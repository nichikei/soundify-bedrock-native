#pragma once

#include "mc/common/network/Packet.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace soundify::protocol {

// Mojang protocols 2169 (26.45) and 2193 (26.52, same schema) / packet 0xB0 payload. This adapter is intentionally
// checked for 26.45 and 26.52; the assertions prevent a changed ABI from being accepted.
struct PlayerStartItemCooldownPayload final {
    std::string itemCategory;
    std::int32_t durationTicks;
};

constexpr std::uint8_t PlayerStartItemCooldownPacketId = 0xB0;

static_assert(sizeof(SDK::Packet) == 0x30);
static_assert(offsetof(PlayerStartItemCooldownPayload, itemCategory) == 0x00);
static_assert(offsetof(PlayerStartItemCooldownPayload, durationTicks) == 0x20);
static_assert(sizeof(PlayerStartItemCooldownPayload) == 0x28);

inline PlayerStartItemCooldownPayload* playerStartItemCooldownPayload(SDK::Packet* packet) {
    return reinterpret_cast<PlayerStartItemCooldownPayload*>(
        reinterpret_cast<std::byte*>(packet) + sizeof(SDK::Packet));
}

} // namespace soundify::protocol
