#include "soundify/core/ExplosionRouter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace soundify {
namespace {
using namespace std::chrono_literals;

constexpr auto CreeperLifetime = 5s;
constexpr auto CrystalLifetime = 750ms;
constexpr auto AnchorLifetime = 1500ms;
constexpr float CreeperRadius = 1.25F;
constexpr float CrystalRadius = 3.0F;
constexpr float AnchorRadius = 1.75F;

float distanceSquared(ExplosionPosition left, ExplosionPosition right) {
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return x * x + y * y + z * z;
}

std::string normalizeIdentifier(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

bool isIdentifier(const std::string& value, std::string_view name) {
    if (value == name || value == "minecraft:" + std::string(name)) return true;
    const auto separator = value.rfind(':');
    return separator != std::string::npos && value.substr(separator + 1) == name;
}
}

void ExplosionRouter::onWorldChanged() {
    primedCreepers_.clear();
    endCrystals_.clear();
    respawnAnchor_.reset();
}

void ExplosionRouter::onCreeperPrimed(std::uint64_t runtimeId, ExplosionPosition position, TimePoint now) {
    primedCreepers_[runtimeId] = {position, now};
}

void ExplosionRouter::updatePrimedCreeper(std::uint64_t runtimeId, ExplosionPosition position) {
    const auto found = primedCreepers_.find(runtimeId);
    if (found != primedCreepers_.end()) found->second.position = position;
}

void ExplosionRouter::observeEndCrystal(std::uint64_t runtimeId, ExplosionPosition position, TimePoint now) {
    endCrystals_[runtimeId] = {position, now};
}

void ExplosionRouter::onRespawnAnchorUsed(ExplosionPosition position, TimePoint now) {
    respawnAnchor_ = Observation{position, now};
}

std::optional<std::string> ExplosionRouter::route(
    std::string_view actorIdentifier,
    ExplosionPosition position,
    TimePoint now) {
    expire(now);
    const auto identifier = normalizeIdentifier(actorIdentifier);

    if (isIdentifier(identifier, "creeper")) return "soundify.creeper_boom";
    if (isIdentifier(identifier, "ender_crystal") || isIdentifier(identifier, "end_crystal")) {
        return "soundify.crystal_boom";
    }

    if (respawnAnchor_ && distanceSquared(respawnAnchor_->position, position) <= AnchorRadius * AnchorRadius) {
        respawnAnchor_.reset();
        return "soundify.anchor_boom";
    }

    for (auto it = primedCreepers_.begin(); it != primedCreepers_.end(); ++it) {
        if (distanceSquared(it->second.position, position) <= CreeperRadius * CreeperRadius) {
            primedCreepers_.erase(it);
            return "soundify.creeper_boom";
        }
    }

    for (auto it = endCrystals_.begin(); it != endCrystals_.end(); ++it) {
        if (distanceSquared(it->second.position, position) <= CrystalRadius * CrystalRadius) {
            endCrystals_.erase(it);
            return "soundify.crystal_boom";
        }
    }

    return std::nullopt;
}

void ExplosionRouter::expire(TimePoint now) {
    std::erase_if(primedCreepers_, [now](const auto& entry) {
        return now - entry.second.observedAt > CreeperLifetime;
    });
    std::erase_if(endCrystals_, [now](const auto& entry) {
        return now - entry.second.observedAt > CrystalLifetime;
    });
    if (respawnAnchor_ && now - respawnAnchor_->observedAt > AnchorLifetime) respawnAnchor_.reset();
}

} // namespace soundify
