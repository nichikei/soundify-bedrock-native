#pragma once

#include "soundify/client/ClientAdapter.hpp"

#include <cstdint>
#include <string>

namespace soundify {

class SoundifyClient final {
public:
    explicit SoundifyClient(ClientAdapter& adapter);

    [[nodiscard]] bool active() const;
    void onWorldChanged();
    void onAttack(std::int64_t targetId, WeaponKind weapon, bool critical,
                  SoundEngine::TimePoint now);
    void onCriticalHitConfirmed(std::int64_t targetId, SoundEngine::TimePoint now);
    void onProjectileDeflected(std::int64_t targetId, SoundPosition position,
                               SoundEngine::TimePoint now);
    void onMaceSmash(SoundEngine::TimePoint now);
    void onShieldCooldown(std::string itemCategory, int durationTicks, SoundEngine::TimePoint now);
    void onDamageConfirmed(std::int64_t targetId, SoundEngine::TimePoint now);
    void onDeath(std::int64_t targetId, SoundEngine::TimePoint now);
    void onEntityRemoved(std::int64_t targetId);
    void onHotbarChanged(std::string itemId, SoundEngine::TimePoint now);
    void onPrimaryAction(const std::string& itemId, SoundEngine::TimePoint now);
    void onUseChanged(const std::string& itemId, bool pressed, SoundEngine::TimePoint now,
                      ItemUseTraits traits = {});
    void onFireballSpawn(SoundPosition position, float distance);
    void onProjectileSpawn(std::int64_t uniqueId, std::uint64_t runtimeId, ProjectileKind kind,
                           SoundPosition position, SoundEngine::TimePoint now);
    void onProjectileMoved(std::uint64_t runtimeId, SoundPosition position);
    void onProjectileRemoved(std::int64_t uniqueId, SoundPosition playerPosition, bool suppressSound,
                             SoundEngine::TimePoint now);
    void onSneakChanged(bool sneaking, SoundEngine::TimePoint now);
    void onMovement(bool sprinting, float horizontalSpeed, const std::string& bootsId, SoundEngine::TimePoint now);
    void onTraversalState(bool swimming, bool gliding, SoundEngine::TimePoint now);
    void onJumpMotion(bool jumpPressed, float verticalVelocity, bool solidAbove, bool swimming, bool gliding,
                      SoundEngine::TimePoint now);
    void onMobEffect(std::uint8_t eventId, int effectId, int durationTicks, bool ambient, bool visible,
                     bool suppressSound);
    void onExperienceLevel(int level, bool suppressSound);
    void onHealth(float health, SoundEngine::TimePoint now);
    void onScreenChanged(const std::string& screen, SoundEngine::TimePoint now);
    void onTabListChanged(bool open, SoundEngine::TimePoint now);
    void onCharacterTyped(bool backspace, SoundEngine::TimePoint now);
    void armInventoryInteraction(InventoryIntent intent, SoundEngine::TimePoint now);
    void onInventorySnapshot(std::vector<InventorySlotState> slots, bool containerOpen,
                             SoundEngine::TimePoint now);
    void onLobbyInventory(const std::vector<InventorySlotState>& slots);
    void onCompactTowerPlaced(SoundEngine::TimePoint now);
    void onBedWarsTntSpawn(SoundPosition position, float distance);
    void onBedWarsBedBroken(SoundPosition playerPosition, SoundEngine::TimePoint now);
    void onTitleMessage(std::string text, SoundEngine::TimePoint now);
    void onChatMessage(std::string text, std::string playerName, SoundEngine::TimePoint now);
    void flushPendingSounds();
    void tick(SoundEngine::TimePoint now);

private:
    ClientAdapter& adapter_;
    SoundEngine engine_;

    void dispatch(std::vector<SoundCommand> commands);
};

} // namespace soundify
