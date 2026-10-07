#pragma once
#include <cstdint>
#include <string_view>

namespace soundify {
// LevelSoundEvent enum, unchanged between Mojang's 1.26.45 (2169) and 1.26.52 (2193) schemas.
inline constexpr std::uint32_t GlassImpactEvent = 166;
inline bool isExperienceBottle(std::string_view actor) {
    return actor == "minecraft:xp_bottle" || actor == "xp_bottle" ||
           actor == "minecraft:experience_bottle" || actor == "experience_bottle";
}
inline bool isThrownBottle(std::string_view actor) {
    return isExperienceBottle(actor) || actor == "minecraft:splash_potion" ||
           actor == "splash_potion" || actor == "minecraft:lingering_potion" ||
           actor == "lingering_potion";
}
inline bool isThrownBottleImpact(std::string_view actor, std::uint32_t event) {
    return isThrownBottle(actor) && event == GlassImpactEvent;
}
inline bool isThrownBottleImpact(std::string_view actor, std::string_view event) {
    return isThrownBottle(actor) && event == "glass";
}
inline bool isExperienceBottleImpact(std::string_view actor, std::uint32_t event) {
    return isExperienceBottle(actor) && event == GlassImpactEvent;
}
inline bool isExperienceBottleImpact(std::string_view actor, std::string_view event) {
    return isExperienceBottle(actor) && event == "glass";
}
} // namespace soundify
