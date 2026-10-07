#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace soundify {

enum class WeaponKind {
    Other,
    WoodenSword,
    Sword,
    Axe,
    Tool,
    Stick,
    Mace,
    Trident,
};

enum class SoundGroup {
    General,
    Combat,
    Item,
    Movement,
    Interface,
    Status,
    Effect,
    BedWars,
};

enum class InventoryIntent {
    Pointer,
    Drop,
    Swap,
};

enum class ProjectileKind {
    Snowball,
    Egg,
    Potion,
    ExperienceBottle,
};

struct InventorySlotState {
    std::string itemId;
    std::uint8_t count{};

    bool operator==(const InventorySlotState&) const = default;
};

struct ItemUseTraits {
    bool bowPower{};
    bool bowFlame{};
    bool crossbowPiercing{};
};

struct SoundPosition {
    float x{};
    float y{};
    float z{};

    bool operator==(const SoundPosition&) const = default;
};

struct SoundCommand {
    std::string id;
    float volume{1.0F};
    float pitch{1.0F};
    SoundGroup group{SoundGroup::General};
    std::optional<SoundPosition> position;
};

class SoundEngine final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void onWorldChanged();
    void onAttack(std::int64_t targetId, WeaponKind weapon, bool critical, TimePoint now);
    std::vector<SoundCommand> onCriticalHitConfirmed(std::int64_t targetId, TimePoint now);
    std::vector<SoundCommand> onProjectileDeflected(std::int64_t targetId, SoundPosition position,
                                                     TimePoint now);
    std::vector<SoundCommand> onMaceSmash(TimePoint now);
    std::vector<SoundCommand> onShieldCooldown(std::string itemCategory, int durationTicks,
                                                TimePoint now);
    std::vector<SoundCommand> onDamageConfirmed(std::int64_t targetId, TimePoint now);
    std::vector<SoundCommand> onDeath(std::int64_t targetId, TimePoint now);
    void onEntityRemoved(std::int64_t targetId);
    std::vector<SoundCommand> onHotbarChanged(std::string itemId, TimePoint now);
    std::vector<SoundCommand> onPrimaryAction(const std::string& itemId, TimePoint now);
    std::vector<SoundCommand> onUseChanged(const std::string& itemId, bool pressed, TimePoint now,
                                            ItemUseTraits traits = {});
    std::vector<SoundCommand> onFireballSpawn(SoundPosition position, float distance) const;
    void onProjectileSpawn(std::int64_t uniqueId, std::uint64_t runtimeId, ProjectileKind kind,
                           SoundPosition position, TimePoint now);
    void onProjectileMoved(std::uint64_t runtimeId, SoundPosition position);
    std::vector<SoundCommand> onProjectileRemoved(std::int64_t uniqueId, SoundPosition playerPosition,
                                                   bool suppressSound, TimePoint now);
    std::vector<SoundCommand> onSneakChanged(bool sneaking, TimePoint now);
    std::vector<SoundCommand> onMovement(bool sprinting, float horizontalSpeed, const std::string& bootsId,
                                         TimePoint now);
    std::vector<SoundCommand> onTraversalState(bool swimming, bool gliding, TimePoint now);
    std::vector<SoundCommand> onJumpMotion(bool jumpPressed, float verticalVelocity, bool solidAbove,
                                            bool swimming, bool gliding, TimePoint now);
    void onMobEffect(std::uint8_t eventId, int effectId, int durationTicks, bool ambient, bool visible,
                     bool suppressSound);
    std::vector<SoundCommand> flushEffectSounds();
    std::vector<SoundCommand> onExperienceLevel(int level, bool suppressSound);
    std::vector<SoundCommand> onHealth(float health, TimePoint now);
    std::vector<SoundCommand> onScreenChanged(const std::string& screen, TimePoint now);
    std::vector<SoundCommand> onTabListChanged(bool open, TimePoint now);
    std::vector<SoundCommand> onCharacterTyped(bool backspace, TimePoint now);
    void armInventoryInteraction(InventoryIntent intent, TimePoint now);
    std::vector<SoundCommand> onInventorySnapshot(std::vector<InventorySlotState> slots,
                                                   bool containerOpen, TimePoint now);
    std::vector<SoundCommand> onLobbyInventory(const std::vector<InventorySlotState>& slots);
    std::vector<SoundCommand> onCompactTowerPlaced(TimePoint now);
    std::vector<SoundCommand> onBedWarsTntSpawn(SoundPosition position, float distance) const;
    std::vector<SoundCommand> onBedWarsBedBroken(SoundPosition playerPosition, TimePoint now);
    std::vector<SoundCommand> onTitleMessage(std::string text, TimePoint now);
    std::vector<SoundCommand> onChatMessage(std::string text, std::string playerName, TimePoint now);
    void expire(TimePoint now);

private:
    struct Attack {
        WeaponKind weapon{WeaponKind::Other};
        bool critical{};
        bool impactPlayed{};
        bool criticalPlayed{};
        bool damageConfirmed{};
        TimePoint attackedAt{};
        TimePoint confirmedAt{};
    };

    struct Projectile {
        std::uint64_t runtimeId{};
        ProjectileKind kind{ProjectileKind::Snowball};
        SoundPosition position;
        TimePoint spawnedAt{};
    };

    static constexpr auto HitWindow = std::chrono::milliseconds(650);
    static constexpr auto DeathWindow = std::chrono::milliseconds(1600);
    static constexpr auto HotbarCooldown = std::chrono::milliseconds(45);
    static constexpr auto ActionCooldown = std::chrono::milliseconds(35);
    static constexpr auto RunInterval = std::chrono::milliseconds(330);
    static constexpr auto TypeCooldown = std::chrono::milliseconds(8);
    static constexpr auto ChatCooldown = std::chrono::milliseconds(80);
    static constexpr auto InventoryWindow = std::chrono::milliseconds(300);
    static constexpr auto ProjectileMinimumLifetime = std::chrono::milliseconds(100);
    static constexpr auto ProjectileMaximumLifetime = std::chrono::seconds(30);
    static constexpr auto DeflectCooldown = std::chrono::seconds(1);
    static constexpr auto ShieldCooldownDedupe = std::chrono::milliseconds(250);

    std::unordered_map<std::int64_t, Attack> attacks_;
    std::unordered_map<std::int64_t, Projectile> projectiles_;
    std::unordered_map<std::uint64_t, std::int64_t> projectileUniqueByRuntime_;
    std::unordered_map<std::int64_t, TimePoint> deflectedProjectiles_;
    std::optional<TimePoint> lastHotbarSound_;
    std::optional<TimePoint> lastActionSound_;
    std::optional<TimePoint> lastMovementSound_;
    std::optional<TimePoint> nextHeartbeat_;
    std::optional<TimePoint> lastTypeSound_;
    std::optional<TimePoint> lastChatSound_;
    std::optional<TimePoint> lastServerMessage_;
    std::vector<InventorySlotState> inventorySlots_;
    std::optional<std::pair<InventoryIntent, TimePoint>> pendingInventoryInteraction_;
    float inventoryDropPitch_{0.5F};
    std::optional<TimePoint> lastInventoryDrop_;
    std::string lastServerText_;
    std::optional<float> health_;
    bool awaitingRespawnSound_{};
    std::optional<TimePoint> lastPlayerDeathSound_;
    std::optional<TimePoint> lastPlayerRespawnSound_;
    bool tabListOpen_{};
    std::optional<bool> sneaking_;
    std::optional<bool> swimming_;
    std::optional<bool> gliding_;
    std::optional<TimePoint> recentJump_;
    std::optional<TimePoint> lastHeadHit_;
    bool roseAfterJump_{};
    std::optional<float> lastVerticalVelocity_;
    std::unordered_set<int> activeEffects_;
    std::vector<int> pendingEffectSounds_;
    std::uint32_t effectPitchState_{0x6D2B79F5U};
    std::optional<int> experienceLevel_;
    std::optional<std::pair<std::string, TimePoint>> activeUse_;
    std::string screen_;
    unsigned killCombo_{1};
    std::optional<TimePoint> lastChatKill_;
    float voidPitch_{1.0F};
    std::optional<TimePoint> lastVoidSound_;
    std::optional<TimePoint> lastMaceSmash_;
    std::optional<TimePoint> lastShieldCooldown_;
    std::optional<TimePoint> lastCompactTower_;
    std::optional<TimePoint> lastBedWarsBedBreak_;
    int lobbyPattern_{};

    static std::optional<std::string> impactSound(WeaponKind weapon);
    static std::optional<std::string> selectionSound(const std::string& itemId);
    static std::optional<std::string> primarySound(const std::string& itemId);
    static std::optional<std::string> effectSound(int effectId);
    float nextPitch(float minimum, float maximum);
};

} // namespace soundify
