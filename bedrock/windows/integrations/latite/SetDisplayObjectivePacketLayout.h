#pragma once

#include "mc/common/network/Packet.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace soundify::protocol {

// Mojang protocols 2169 (26.45) and 2193 (26.52, same schema) / packet 0x6B payload.
struct SetDisplayObjectivePayload final {
    std::string displaySlotName;
    std::string objectiveName;
    std::string objectiveDisplayName;
    std::string criteriaName;
    std::int32_t sortOrder;
};

constexpr std::uint8_t SetDisplayObjectivePacketId = 0x6B;

static_assert(sizeof(SDK::Packet) == 0x30);
static_assert(offsetof(SetDisplayObjectivePayload, displaySlotName) == 0x00);
static_assert(offsetof(SetDisplayObjectivePayload, objectiveName) == 0x20);
static_assert(offsetof(SetDisplayObjectivePayload, objectiveDisplayName) == 0x40);
static_assert(offsetof(SetDisplayObjectivePayload, criteriaName) == 0x60);
static_assert(offsetof(SetDisplayObjectivePayload, sortOrder) == 0x80);
static_assert(sizeof(SetDisplayObjectivePayload) == 0x88);

inline SetDisplayObjectivePayload* setDisplayObjectivePayload(SDK::Packet* packet) {
    return reinterpret_cast<SetDisplayObjectivePayload*>(
        reinterpret_cast<std::byte*>(packet) + sizeof(SDK::Packet));
}

} // namespace soundify::protocol
