#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace soundify {

struct ExplosionPosition final {
    float x{};
    float y{};
    float z{};
};

class ExplosionRouter final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void onWorldChanged();
    void onCreeperPrimed(std::uint64_t runtimeId, ExplosionPosition position, TimePoint now);
    void updatePrimedCreeper(std::uint64_t runtimeId, ExplosionPosition position);
    void observeEndCrystal(std::uint64_t runtimeId, ExplosionPosition position, TimePoint now);
    void onRespawnAnchorUsed(ExplosionPosition position, TimePoint now);

    // Returns a Bedrock sound definition only when the generic explosion should
    // be replaced. An empty result intentionally keeps TNT and other explosions.
    [[nodiscard]] std::optional<std::string> route(
        std::string_view actorIdentifier,
        ExplosionPosition position,
        TimePoint now);

private:
    struct Observation final {
        ExplosionPosition position;
        TimePoint observedAt;
    };

    std::unordered_map<std::uint64_t, Observation> primedCreepers_;
    std::unordered_map<std::uint64_t, Observation> endCrystals_;
    std::optional<Observation> respawnAnchor_;

    void expire(TimePoint now);
};

} // namespace soundify
