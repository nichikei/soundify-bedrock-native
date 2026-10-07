#include "soundify/client/SoundifyClient.hpp"

namespace soundify {

SoundifyClient::SoundifyClient(ClientAdapter& adapter) : adapter_(adapter) {}

bool SoundifyClient::active() const {
    return adapter_.supported();
}

void SoundifyClient::onWorldChanged() {
    engine_.onWorldChanged();
}

void SoundifyClient::onAttack(std::int64_t targetId, WeaponKind weapon, bool critical,
                               SoundEngine::TimePoint now) {
    if (active()) engine_.onAttack(targetId, weapon, critical, now);
}

void SoundifyClient::onCriticalHitConfirmed(std::int64_t targetId, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onCriticalHitConfirmed(targetId, now));
}

void SoundifyClient::onProjectileDeflected(std::int64_t targetId, SoundPosition position,
                                            SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onProjectileDeflected(targetId, position, now));
}

void SoundifyClient::onMaceSmash(SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onMaceSmash(now));
}

void SoundifyClient::onShieldCooldown(std::string itemCategory, int durationTicks,
                                       SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onShieldCooldown(std::move(itemCategory), durationTicks, now));
}

void SoundifyClient::onDamageConfirmed(std::int64_t targetId, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onDamageConfirmed(targetId, now));
}

void SoundifyClient::onDeath(std::int64_t targetId, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onDeath(targetId, now));
}

void SoundifyClient::onEntityRemoved(std::int64_t targetId) {
    engine_.onEntityRemoved(targetId);
}

void SoundifyClient::onHotbarChanged(std::string itemId, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onHotbarChanged(std::move(itemId), now));
}

void SoundifyClient::onPrimaryAction(const std::string& itemId, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onPrimaryAction(itemId, now));
}

void SoundifyClient::onUseChanged(const std::string& itemId, bool pressed, SoundEngine::TimePoint now,
                                   ItemUseTraits traits) {
    if (active()) dispatch(engine_.onUseChanged(itemId, pressed, now, traits));
}

void SoundifyClient::onFireballSpawn(SoundPosition position, float distance) {
    if (active()) dispatch(engine_.onFireballSpawn(position, distance));
}

void SoundifyClient::onProjectileSpawn(std::int64_t uniqueId, std::uint64_t runtimeId, ProjectileKind kind,
                                        SoundPosition position, SoundEngine::TimePoint now) {
    if (active()) engine_.onProjectileSpawn(uniqueId, runtimeId, kind, position, now);
}

void SoundifyClient::onProjectileMoved(std::uint64_t runtimeId, SoundPosition position) {
    if (active()) engine_.onProjectileMoved(runtimeId, position);
}

void SoundifyClient::onProjectileRemoved(std::int64_t uniqueId, SoundPosition playerPosition, bool suppressSound,
                                          SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onProjectileRemoved(uniqueId, playerPosition, suppressSound, now));
}

void SoundifyClient::onSneakChanged(bool sneaking, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onSneakChanged(sneaking, now));
}

void SoundifyClient::onMovement(bool sprinting, float horizontalSpeed, const std::string& bootsId,
                                SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onMovement(sprinting, horizontalSpeed, bootsId, now));
}

void SoundifyClient::onTraversalState(bool swimming, bool gliding, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onTraversalState(swimming, gliding, now));
}

void SoundifyClient::onJumpMotion(bool jumpPressed, float verticalVelocity, bool solidAbove, bool swimming,
                                   bool gliding, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onJumpMotion(jumpPressed, verticalVelocity, solidAbove, swimming, gliding, now));
}

void SoundifyClient::onMobEffect(std::uint8_t eventId, int effectId, int durationTicks, bool ambient, bool visible,
                                  bool suppressSound) {
    if (active()) engine_.onMobEffect(eventId, effectId, durationTicks, ambient, visible, suppressSound);
}

void SoundifyClient::onExperienceLevel(int level, bool suppressSound) {
    if (active()) dispatch(engine_.onExperienceLevel(level, suppressSound));
}

void SoundifyClient::onHealth(float health, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onHealth(health, now));
}

void SoundifyClient::onScreenChanged(const std::string& screen, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onScreenChanged(screen, now));
}

void SoundifyClient::onTabListChanged(bool open, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onTabListChanged(open, now));
}

void SoundifyClient::onCharacterTyped(bool backspace, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onCharacterTyped(backspace, now));
}

void SoundifyClient::armInventoryInteraction(InventoryIntent intent, SoundEngine::TimePoint now) {
    if (active()) engine_.armInventoryInteraction(intent, now);
}

void SoundifyClient::onInventorySnapshot(std::vector<InventorySlotState> slots, bool containerOpen,
                                          SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onInventorySnapshot(std::move(slots), containerOpen, now));
}

void SoundifyClient::onLobbyInventory(const std::vector<InventorySlotState>& slots) {
    if (active()) dispatch(engine_.onLobbyInventory(slots));
}

void SoundifyClient::onCompactTowerPlaced(SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onCompactTowerPlaced(now));
}

void SoundifyClient::onBedWarsTntSpawn(SoundPosition position, float distance) {
    if (active()) dispatch(engine_.onBedWarsTntSpawn(position, distance));
}

void SoundifyClient::onBedWarsBedBroken(SoundPosition playerPosition, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onBedWarsBedBroken(playerPosition, now));
}

void SoundifyClient::onTitleMessage(std::string text, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onTitleMessage(std::move(text), now));
}

void SoundifyClient::onChatMessage(std::string text, std::string playerName, SoundEngine::TimePoint now) {
    if (active()) dispatch(engine_.onChatMessage(std::move(text), std::move(playerName), now));
}

void SoundifyClient::flushPendingSounds() {
    if (active()) dispatch(engine_.flushEffectSounds());
}

void SoundifyClient::tick(SoundEngine::TimePoint now) {
    flushPendingSounds();
    engine_.expire(now);
}

void SoundifyClient::dispatch(std::vector<SoundCommand> commands) {
    for (const auto& command : commands) adapter_.play(command);
}

} // namespace soundify
