#include "soundify/core/SoundEngine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string_view>
#include <utility>

namespace {
bool endsWith(const std::string& value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(const std::string& value, std::string_view part) {
    return value.find(part) != std::string::npos;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string lowerServerText(std::string value) {
    value = lowerAscii(std::move(value));
    static constexpr auto vietnameseCaseMap = std::to_array<std::pair<std::string_view, std::string_view>>({
        {"À", "à"}, {"Á", "á"}, {"Ả", "ả"}, {"Ã", "ã"}, {"Ạ", "ạ"}, {"Ă", "ă"},
        {"Ằ", "ằ"}, {"Ắ", "ắ"}, {"Ẳ", "ẳ"}, {"Ẵ", "ẵ"}, {"Ặ", "ặ"}, {"Â", "â"},
        {"Ầ", "ầ"}, {"Ấ", "ấ"}, {"Ẩ", "ẩ"}, {"Ẫ", "ẫ"}, {"Ậ", "ậ"}, {"Đ", "đ"},
        {"È", "è"}, {"É", "é"}, {"Ẻ", "ẻ"}, {"Ẽ", "ẽ"}, {"Ẹ", "ẹ"}, {"Ê", "ê"},
        {"Ề", "ề"}, {"Ế", "ế"}, {"Ể", "ể"}, {"Ễ", "ễ"}, {"Ệ", "ệ"}, {"Ì", "ì"},
        {"Í", "í"}, {"Ỉ", "ỉ"}, {"Ĩ", "ĩ"}, {"Ị", "ị"}, {"Ò", "ò"}, {"Ó", "ó"},
        {"Ỏ", "ỏ"}, {"Õ", "õ"}, {"Ọ", "ọ"}, {"Ô", "ô"}, {"Ồ", "ồ"}, {"Ố", "ố"},
        {"Ổ", "ổ"}, {"Ỗ", "ỗ"}, {"Ộ", "ộ"}, {"Ơ", "ơ"}, {"Ờ", "ờ"}, {"Ớ", "ớ"},
        {"Ở", "ở"}, {"Ỡ", "ỡ"}, {"Ợ", "ợ"}, {"Ù", "ù"}, {"Ú", "ú"}, {"Ủ", "ủ"},
        {"Ũ", "ũ"}, {"Ụ", "ụ"}, {"Ư", "ư"}, {"Ừ", "ừ"}, {"Ứ", "ứ"}, {"Ử", "ử"},
        {"Ữ", "ữ"}, {"Ự", "ự"}, {"Ỳ", "ỳ"}, {"Ý", "ý"}, {"Ỷ", "ỷ"}, {"Ỹ", "ỹ"},
        {"Ỵ", "ỵ"},
    });
    for (const auto& [upper, lower] : vietnameseCaseMap) {
        for (std::size_t pos = value.find(upper); pos != std::string::npos; pos = value.find(upper, pos + lower.size())) {
            value.replace(pos, upper.size(), lower);
        }
    }
    return value;
}

std::string normalizeServerText(std::string value) {
    // Bedrock formatting codes use UTF-8 section-sign + one format character.
    // Strip them before matching chat/title text so colored server messages do
    // not require duplicate rules.
    constexpr std::string_view marker{"\xC2\xA7"};
    for (std::size_t pos = value.find(marker); pos != std::string::npos; pos = value.find(marker, pos)) {
        const auto count = std::min<std::size_t>(marker.size() + 1, value.size() - pos);
        value.erase(pos, count);
    }
    return lowerServerText(std::move(value));
}

bool containsAny(const std::string& text, std::initializer_list<std::string_view> values) {
    return std::any_of(values.begin(), values.end(), [&](std::string_view value) { return contains(text, value); });
}

soundify::SoundCommand sound(std::string id, float volume = 1.0F, float pitch = 1.0F,
                             soundify::SoundGroup group = soundify::SoundGroup::General) {
    return {"soundify." + std::move(id), volume, pitch, group};
}
}

namespace soundify {

void SoundEngine::onWorldChanged() {
    attacks_.clear();
    projectiles_.clear();
    projectileUniqueByRuntime_.clear();
    deflectedProjectiles_.clear();
    lastHotbarSound_.reset();
    lastActionSound_.reset();
    lastMovementSound_.reset();
    nextHeartbeat_.reset();
    lastTypeSound_.reset();
    lastChatSound_.reset();
    lastServerMessage_.reset();
    lastServerText_.clear();
    inventorySlots_.clear();
    pendingInventoryInteraction_.reset();
    inventoryDropPitch_ = 0.5F;
    lastInventoryDrop_.reset();
    health_.reset();
    awaitingRespawnSound_ = false;
    lastPlayerDeathSound_.reset();
    lastPlayerRespawnSound_.reset();
    tabListOpen_ = false;
    sneaking_.reset();
    swimming_.reset();
    gliding_.reset();
    recentJump_.reset();
    lastHeadHit_.reset();
    roseAfterJump_ = false;
    lastVerticalVelocity_.reset();
    activeEffects_.clear();
    pendingEffectSounds_.clear();
    experienceLevel_.reset();
    activeUse_.reset();
    screen_.clear();
    killCombo_ = 1;
    lastChatKill_.reset();
    voidPitch_ = 1.0F;
    lastVoidSound_.reset();
    lastMaceSmash_.reset();
    lastShieldCooldown_.reset();
    lastCompactTower_.reset();
    lastBedWarsBedBreak_.reset();
    lobbyPattern_ = 0;
}

void SoundEngine::onAttack(std::int64_t targetId, WeaponKind weapon, bool critical, TimePoint now) {
    attacks_.insert_or_assign(targetId, Attack{weapon, critical, false, false, false, now, {}});
}

std::vector<SoundCommand> SoundEngine::onCriticalHitConfirmed(std::int64_t targetId, TimePoint now) {
    auto found = attacks_.find(targetId);
    if (found == attacks_.end() || now - found->second.attackedAt > HitWindow) return {};
    auto& attack = found->second;
    attack.critical = true;
    if (!attack.impactPlayed || attack.criticalPlayed) return {};
    attack.criticalPlayed = true;
    return {sound("extra_crit", 1.0F, nextPitch(0.9F, 1.1F), SoundGroup::Combat)};
}

std::vector<SoundCommand> SoundEngine::onProjectileDeflected(std::int64_t targetId,
                                                              SoundPosition position, TimePoint now) {
    const auto previous = deflectedProjectiles_.find(targetId);
    if (previous != deflectedProjectiles_.end() && now - previous->second <= DeflectCooldown) return {};
    deflectedProjectiles_.insert_or_assign(targetId, now);
    auto command = sound("blockhit", 0.7F, 1.0F, SoundGroup::Combat);
    command.position = position;
    return {std::move(command)};
}

std::vector<SoundCommand> SoundEngine::onMaceSmash(TimePoint now) {
    if (lastMaceSmash_ && now - *lastMaceSmash_ < std::chrono::milliseconds(100)) return {};
    lastMaceSmash_ = now;
    return {sound("mace_attack", 0.65F, nextPitch(0.9F, 1.1F), SoundGroup::Combat)};
}

std::vector<SoundCommand> SoundEngine::onShieldCooldown(std::string itemCategory, int durationTicks,
                                                         TimePoint now) {
    itemCategory = lowerAscii(std::move(itemCategory));
    if (durationTicks <= 0 || (itemCategory != "shield" && itemCategory != "minecraft:shield")) return {};
    if (lastShieldCooldown_ && now - *lastShieldCooldown_ < ShieldCooldownDedupe) return {};
    lastShieldCooldown_ = now;
    return {sound("shield_disable", 0.65F, 1.0F, SoundGroup::Combat)};
}

std::vector<SoundCommand> SoundEngine::onDamageConfirmed(std::int64_t targetId, TimePoint now) {
    auto found = attacks_.find(targetId);
    if (found == attacks_.end() || now - found->second.attackedAt > HitWindow) {
        return {};
    }

    Attack& attack = found->second;
    if (attack.impactPlayed) {
        return {};
    }
    attack.damageConfirmed = true;
    attack.confirmedAt = now;
    attack.impactPlayed = true;

    std::vector<SoundCommand> result;
    if (const auto sound = impactSound(attack.weapon)) {
        result.push_back({*sound, 1.0F, 1.0F, SoundGroup::Combat});
    }
    if (attack.critical && !attack.criticalPlayed) {
        result.push_back(sound("extra_crit", 1.0F, nextPitch(0.9F, 1.1F), SoundGroup::Combat));
        attack.criticalPlayed = true;
    }
    return result;
}

std::vector<SoundCommand> SoundEngine::onDeath(std::int64_t targetId, TimePoint now) {
    auto found = attacks_.find(targetId);
    if (found == attacks_.end()) {
        return {};
    }

    const Attack attack = found->second;
    attacks_.erase(found);
    if (!attack.damageConfirmed || now - attack.confirmedAt > DeathWindow) {
        return {};
    }
    return {{"soundify.kill", 1.0F, 1.0F, SoundGroup::Combat}};
}

void SoundEngine::onEntityRemoved(std::int64_t targetId) {
    // An unload, teleport or despawn is not proof that the entity died.
    attacks_.erase(targetId);
}

std::vector<SoundCommand> SoundEngine::onHotbarChanged(std::string itemId, TimePoint now) {
    if (lastHotbarSound_ && now - *lastHotbarSound_ < HotbarCooldown) {
        return {};
    }
    const auto sound = selectionSound(itemId);
    if (!sound) {
        return {};
    }
    lastHotbarSound_ = now;
    return {{*sound, 0.8F, 1.0F, SoundGroup::Item}};
}

std::vector<SoundCommand> SoundEngine::onPrimaryAction(const std::string& itemId, TimePoint now) {
    if (lastActionSound_ && now - *lastActionSound_ < ActionCooldown) return {};
    const auto id = primarySound(itemId);
    if (!id) return {};
    lastActionSound_ = now;
    return {{*id, 0.8F, 1.0F, SoundGroup::Combat}};
}

std::vector<SoundCommand> SoundEngine::onUseChanged(const std::string& itemId, bool pressed, TimePoint now,
                                                     ItemUseTraits traits) {
    const auto itemSound = [](std::string id, float volume = 1.0F, float pitch = 1.0F) {
        return sound(std::move(id), volume, pitch, SoundGroup::Item);
    };
    if (!itemId.starts_with("minecraft:")) return {};
    if (!pressed) {
        if (!activeUse_) return {};
        const auto [activeItem, started] = *activeUse_;
        activeUse_.reset();
        if (activeItem != itemId || now - started < std::chrono::milliseconds(90)) return {};
        if (itemId == "minecraft:bow") {
            std::vector<SoundCommand> result;
            if (traits.bowPower) result.push_back(itemSound("bow_power", 0.65F));
            if (traits.bowFlame) result.push_back(itemSound("bow_flame", 0.65F));
            if (result.empty()) result.push_back(itemSound("bow_shoot", 0.65F));
            return result;
        }
        if (itemId == "minecraft:trident") return {itemSound("trident_throw")};
        if (itemId == "minecraft:crossbow") {
            std::vector<SoundCommand> result{itemSound("firework_ready", 0.75F)};
            if (traits.crossbowPiercing) result.push_back(itemSound("crossbow_piercing", 0.65F));
            return result;
        }
        return {};
    }

    if (lastActionSound_ && now - *lastActionSound_ < ActionCooldown) return {};
    std::optional<SoundCommand> result;
    if (itemId == "minecraft:bow") { activeUse_ = {{itemId, now}}; result = itemSound("bow_pull", 0.8F); }
    else if (itemId == "minecraft:trident") { activeUse_ = {{itemId, now}}; result = itemSound("trident_use", 0.8F); }
    else if (itemId == "minecraft:crossbow") { activeUse_ = {{itemId, now}}; result = itemSound("crossbow_quick_charge", 0.8F); }
    else if (itemId == "minecraft:shield") result = itemSound("shield_use", 0.8F);
    else if (itemId == "minecraft:egg") result = itemSound("egg_throw");
    else if (itemId == "minecraft:snowball") result = itemSound("snowball_throw");
    else if (itemId == "minecraft:experience_bottle") result = itemSound("xp_bottle_throw");
    else if (itemId == "minecraft:splash_potion" || itemId == "minecraft:lingering_potion") result = itemSound("potion_throw");
    else if (itemId == "minecraft:wind_charge") result = itemSound("wind_charge_throw");
    else if (itemId == "minecraft:firework_rocket") result = itemSound("firework_use");
    else if (itemId == "minecraft:end_crystal") result = itemSound("end_crystal_place");
    else if (itemId == "minecraft:compass" || itemId == "minecraft:recovery_compass") result = itemSound("compass_use", 0.8F);
    else if (itemId == "minecraft:potion" || itemId == "minecraft:honey_bottle" ||
             itemId == "minecraft:ominous_bottle") result = itemSound("potion_open", 0.8F);
    if (!result) return {};
    lastActionSound_ = now;
    return {*result};
}

std::vector<SoundCommand> SoundEngine::onFireballSpawn(SoundPosition position, float distance) const {
    if (!std::isfinite(distance) || distance < 0.0F) return {};
    const float volume = std::max(0.1F, 1.0F - distance / 16.0F);
    auto command = sound("fireball_spawn", volume, 1.0F, SoundGroup::Item);
    command.position = position;
    return {std::move(command)};
}

void SoundEngine::onProjectileSpawn(std::int64_t uniqueId, std::uint64_t runtimeId, ProjectileKind kind,
                                     SoundPosition position, TimePoint now) {
    if (const auto existing = projectiles_.find(uniqueId); existing != projectiles_.end()) {
        projectileUniqueByRuntime_.erase(existing->second.runtimeId);
    }
    if (const auto reused = projectileUniqueByRuntime_.find(runtimeId);
        reused != projectileUniqueByRuntime_.end() && reused->second != uniqueId) {
        projectiles_.erase(reused->second);
    }
    projectiles_.insert_or_assign(uniqueId, Projectile{runtimeId, kind, position, now});
    projectileUniqueByRuntime_.insert_or_assign(runtimeId, uniqueId);
}

void SoundEngine::onProjectileMoved(std::uint64_t runtimeId, SoundPosition position) {
    const auto unique = projectileUniqueByRuntime_.find(runtimeId);
    if (unique == projectileUniqueByRuntime_.end()) return;
    const auto projectile = projectiles_.find(unique->second);
    if (projectile != projectiles_.end()) projectile->second.position = position;
}

std::vector<SoundCommand> SoundEngine::onProjectileRemoved(std::int64_t uniqueId, SoundPosition playerPosition,
                                                            bool suppressSound, TimePoint now) {
    const auto found = projectiles_.find(uniqueId);
    if (found == projectiles_.end()) return {};
    const auto projectile = found->second;
    projectiles_.erase(found);
    if (const auto runtime = projectileUniqueByRuntime_.find(projectile.runtimeId);
        runtime != projectileUniqueByRuntime_.end() && runtime->second == uniqueId) {
        projectileUniqueByRuntime_.erase(runtime);
    }
    if (suppressSound || now < projectile.spawnedAt ||
        (projectile.kind != ProjectileKind::ExperienceBottle &&
         now - projectile.spawnedAt < ProjectileMinimumLifetime)) return {};
    const float dx = projectile.position.x - playerPosition.x;
    const float dy = projectile.position.y - playerPosition.y;
    const float dz = projectile.position.z - playerPosition.z;
    if (dx * dx + dy * dy + dz * dz > 64.0F * 64.0F) return {};
    if (projectile.kind == ProjectileKind::Snowball) {
        return {{"step.snow", 1.0F, 1.0F, SoundGroup::Item, projectile.position}};
    }
    if (projectile.kind == ProjectileKind::Egg) {
        return {{"block.turtle_egg.break", 0.5F, 2.0F, SoundGroup::Item, projectile.position}};
    }
    return {{"soundify.splash_potion_break", 1.0F, 1.0F, SoundGroup::Item, projectile.position}};
}

std::vector<SoundCommand> SoundEngine::onSneakChanged(bool sneaking, TimePoint) {
    if (!sneaking_) { sneaking_ = sneaking; return {}; }
    if (*sneaking_ == sneaking) return {};
    sneaking_ = sneaking;
    return {sound(sneaking ? "sneak" : "stand", 0.3F, 1.0F, SoundGroup::Movement)};
}

std::vector<SoundCommand> SoundEngine::onMovement(bool sprinting, float horizontalSpeed, const std::string& bootsId,
                                                  TimePoint now) {
    if (horizontalSpeed < 0.035F) return {};
    if (lastMovementSound_ && now - *lastMovementSound_ < RunInterval) return {};
    std::optional<std::string> armor;
    if (endsWith(bootsId, "leather_boots")) armor = "armor.equip_leather";
    else if (endsWith(bootsId, "chainmail_boots")) armor = "armor.equip_chain";
    else if (endsWith(bootsId, "golden_boots")) armor = "armor.equip_gold";
    else if (endsWith(bootsId, "iron_boots")) armor = "armor.equip_iron";
    else if (endsWith(bootsId, "copper_boots")) armor = "armor.equip_copper";
    else if (endsWith(bootsId, "diamond_boots")) armor = "armor.equip_diamond";
    else if (endsWith(bootsId, "netherite_boots")) armor = "armor.equip_netherite";
    if (!armor && !sprinting) return {};
    lastMovementSound_ = now;
    return armor ? std::vector<SoundCommand>{{*armor, 0.25F, sprinting ? 1.2F : 0.9F, SoundGroup::Movement}}
                 : std::vector<SoundCommand>{sound("run", 0.1F, 1.0F, SoundGroup::Movement)};
}

std::vector<SoundCommand> SoundEngine::onTraversalState(bool swimming, bool gliding, TimePoint) {
    std::vector<SoundCommand> result;
    if (swimming_ && !*swimming_ && swimming)
        result.push_back(sound("water_crawl", 0.8F, 1.0F, SoundGroup::Movement));
    if (gliding_ && !*gliding_ && gliding)
        result.push_back(sound("elytra_use", 0.9F, 1.0F, SoundGroup::Movement));
    swimming_ = swimming;
    gliding_ = gliding;
    return result;
}

std::vector<SoundCommand> SoundEngine::onJumpMotion(bool jumpPressed, float verticalVelocity, bool solidAbove,
                                                     bool swimming, bool gliding, TimePoint now) {
    std::vector<SoundCommand> result;
    const bool acceptedJump = jumpPressed && verticalVelocity > 0.02F &&
                              (!lastVerticalVelocity_ || *lastVerticalVelocity_ <= 0.02F);
    lastVerticalVelocity_ = verticalVelocity;
    if (acceptedJump) {
        recentJump_ = now;
        roseAfterJump_ = true;
        if (activeEffects_.contains(8))
            result.push_back(sound("player_jumpboost", 0.4F, nextPitch(0.8F, 1.0F), SoundGroup::Movement));
    }
    if (!recentJump_) return result;
    if (now - *recentJump_ > std::chrono::milliseconds(600) || swimming || gliding) {
        recentJump_.reset();
        roseAfterJump_ = false;
        return result;
    }
    if (verticalVelocity > 0.02F) roseAfterJump_ = true;
    if (!roseAfterJump_ || !solidAbove || verticalVelocity > 0.0F) return result;
    if (lastHeadHit_ && now - *lastHeadHit_ < std::chrono::milliseconds(200)) return result;
    recentJump_.reset();
    roseAfterJump_ = false;
    lastHeadHit_ = now;
    result.push_back(sound("headhit", 0.8F, 1.0F, SoundGroup::Movement));
    return result;
}

void SoundEngine::onMobEffect(std::uint8_t eventId, int effectId, int durationTicks, bool ambient, bool visible,
                              bool suppressSound) {
    constexpr std::uint8_t Add = 1;
    constexpr std::uint8_t Update = 2;
    constexpr std::uint8_t Remove = 3;
    if (eventId == Remove) {
        activeEffects_.erase(effectId);
        std::erase(pendingEffectSounds_, effectId);
        return;
    }
    if (eventId == Update) {
        activeEffects_.insert(effectId);
        return;
    }
    if (eventId != Add || !activeEffects_.insert(effectId).second || suppressSound || !effectSound(effectId)) return;

    // Match the Java mod's anti-noise filters: long command effects and short
    // ambient beacon refreshes should not sound like a newly consumed potion.
    if (durationTicks > 12000 || (ambient && visible && durationTicks <= 400)) return;
    if (durationTicks > 0 && durationTicks % 20 != 0 && effectId != 19) return;
    if (std::find(pendingEffectSounds_.begin(), pendingEffectSounds_.end(), effectId) == pendingEffectSounds_.end())
        pendingEffectSounds_.push_back(effectId);
}

std::vector<SoundCommand> SoundEngine::flushEffectSounds() {
    if (pendingEffectSounds_.empty()) return {};
    const auto priority = [](int effectId) {
        if (effectId == 11) return 0; // Resistance before Absorption, as in Java.
        if (effectId == 22) return 1;
        return 2;
    };
    const int chosen = *std::min_element(pendingEffectSounds_.begin(), pendingEffectSounds_.end(),
        [&](int left, int right) { return priority(left) < priority(right); });
    pendingEffectSounds_.clear();
    const auto id = effectSound(chosen);
    return id ? std::vector<SoundCommand>{sound(*id, 1.0F, nextPitch(0.9F, 1.1F), SoundGroup::Effect)}
              : std::vector<SoundCommand>{};
}

std::vector<SoundCommand> SoundEngine::onExperienceLevel(int level, bool suppressSound) {
    if (level < 0) return {};
    if (!experienceLevel_ || suppressSound) {
        experienceLevel_ = level;
        return {};
    }
    const int previous = *experienceLevel_;
    experienceLevel_ = level;
    if (level <= previous || level % 5 == 0) return {};
    return {sound("levelup", 0.3F, 1.0F, SoundGroup::Status)};
}

std::vector<SoundCommand> SoundEngine::onHealth(float health, TimePoint now) {
    std::vector<SoundCommand> result;
    if (health_ && *health_ > 0.0F && health <= 0.0F) {
        if (!lastPlayerDeathSound_ || now - *lastPlayerDeathSound_ >= std::chrono::milliseconds(750)) {
            result.push_back(sound("player_died", 1.0F, 1.0F, SoundGroup::Status));
        }
        lastPlayerDeathSound_ = now;
        awaitingRespawnSound_ = true;
    } else if (health_ && *health_ <= 0.0F && health > 0.0F && awaitingRespawnSound_) {
        if (!lastPlayerRespawnSound_ || now - *lastPlayerRespawnSound_ >= std::chrono::milliseconds(750)) {
            result.push_back(sound("revive", 1.0F, 1.0F, SoundGroup::Status));
        }
        lastPlayerRespawnSound_ = now;
        awaitingRespawnSound_ = false;
    }
    health_ = health;

    if (!(health > 0.0F && health <= 4.0F)) {
        nextHeartbeat_.reset();
        return result;
    }
    if (!nextHeartbeat_) {
        nextHeartbeat_ = now + std::chrono::milliseconds(350);
        return result;
    }
    if (now < *nextHeartbeat_) return result;
    const auto interval = std::chrono::milliseconds(static_cast<int>((380.0F + health * 210.0F) * 1.15F));
    nextHeartbeat_ = now + interval;
    result.push_back(sound("heartbeat", 0.75F, 0.8F + (4.0F - health) * 0.1F, SoundGroup::Status));
    return result;
}

std::vector<SoundCommand> SoundEngine::onScreenChanged(const std::string& screen, TimePoint) {
    if (screen_ == screen) return {};
    if (screen_.empty()) { screen_ = screen; return {}; }
    const std::string old = screen_;
    const bool oldHud = old == "hud_screen";
    const bool newHud = screen == "hud_screen";
    const bool oldChat = contains(old, "chat");
    const bool newChat = contains(screen, "chat");
    // Multiple non-HUD ScreenViews can be rendered for one real screen. Keep the first
    // logical screen until the game returns to HUD so transient layers cannot retrigger it.
    if (!oldHud && !newHud) return {};
    screen_ = screen;
    const auto interfaceSound = [](std::string id) { return sound(std::move(id), 1.0F, 1.0F, SoundGroup::Interface); };
    if (newChat && oldHud) return {sound("chatopen", 1.0F, nextPitch(0.9F, 1.1F), SoundGroup::Interface)};
    if (oldChat && newHud) return {interfaceSound("chatclose")};
    if (newHud && !oldHud) {
        if (old == "inventory_screen" || old == "creative_inventory_screen")
            return {interfaceSound("inventory_close")};
        return {};
    }
    if (oldHud && !newHud) {
        const auto customOpen = [this](std::string id) {
            return sound(std::move(id), 1.0F, nextPitch(0.9F, 1.1F), SoundGroup::Interface);
        };
        const auto nativeOpen = [this](std::string id) {
            return SoundCommand{std::move(id), 1.0F, nextPitch(0.9F, 1.1F), SoundGroup::Interface};
        };
        if (contains(screen, "anvil")) return {customOpen("anvil_open")};
        if (contains(screen, "brewing")) return {customOpen("brewing_stand_open")};
        if (contains(screen, "enchant")) return {customOpen("enchanting_table_open")};
        if (contains(screen, "grindstone")) return {customOpen("grindstone_open")};
        if (contains(screen, "lectern") || contains(screen, "book")) return {customOpen("lectern_open")};
        if (contains(screen, "loom")) return {customOpen("loom_open")};
        if (contains(screen, "smithing")) return {customOpen("smithing_table_open")};
        if (contains(screen, "cartography")) return {nativeOpen("block.cartography_table.use")};
        if (contains(screen, "stonecutter")) return {nativeOpen("block.stonecutter.use")};
        if (contains(screen, "smoker")) return {nativeOpen("block.campfire.crackle")};
        if (contains(screen, "furnace")) return {nativeOpen("fire.fire")};
        if (contains(screen, "beacon")) return {nativeOpen("resonate.amethyst_block")};
        if (contains(screen, "crafting") || contains(screen, "crafter")) return {nativeOpen("hit.wood")};
        if (contains(screen, "hopper") || contains(screen, "dispenser") || contains(screen, "dropper"))
            return {nativeOpen("hit.stone")};
        if (screen == "inventory_screen" || screen == "creative_inventory_screen")
            return {interfaceSound("inventory_open")};
        // Chest/Ender Chest/barrel/shulker have their own world sound. Unknown
        // menus (including pause/options) must not gain an inventory overlay.
        return {};
    }
    return {};
}

std::vector<SoundCommand> SoundEngine::onCharacterTyped(bool backspace, TimePoint now) {
    if (lastTypeSound_ && now - *lastTypeSound_ < TypeCooldown) return {};
    lastTypeSound_ = now;
    return {sound(backspace ? "backtype" : "type", 1.0F, nextPitch(0.8F, 1.1F), SoundGroup::Interface)};
}

std::vector<SoundCommand> SoundEngine::onTabListChanged(bool open, TimePoint) {
    if (tabListOpen_ == open) return {};
    tabListOpen_ = open;
    if (!open) return {};
    return {sound("tab_open", 1.0F, 1.0F, SoundGroup::Interface)};
}

void SoundEngine::armInventoryInteraction(InventoryIntent intent, TimePoint now) {
    pendingInventoryInteraction_ = {{intent, now}};
}

std::vector<SoundCommand> SoundEngine::onInventorySnapshot(std::vector<InventorySlotState> slots,
                                                            bool containerOpen, TimePoint now) {
    if (!containerOpen) {
        inventorySlots_.clear();
        pendingInventoryInteraction_.reset();
        return {};
    }
    if (inventorySlots_.empty()) {
        inventorySlots_ = std::move(slots);
        pendingInventoryInteraction_.reset();
        return {};
    }
    if (slots == inventorySlots_) {
        if (pendingInventoryInteraction_ && now - pendingInventoryInteraction_->second > InventoryWindow)
            pendingInventoryInteraction_.reset();
        return {};
    }

    const auto previous = std::move(inventorySlots_);
    inventorySlots_ = std::move(slots);
    if (!pendingInventoryInteraction_ || now - pendingInventoryInteraction_->second > InventoryWindow) {
        pendingInventoryInteraction_.reset();
        return {};
    }

    const auto intent = pendingInventoryInteraction_->first;
    pendingInventoryInteraction_.reset();
    const auto totalItems = [](const std::vector<InventorySlotState>& values) {
        std::uint32_t total = 0;
        for (const auto& value : values) total += value.count;
        return total;
    };
    const auto before = totalItems(previous);
    const auto after = totalItems(inventorySlots_);

    if (intent == InventoryIntent::Drop) {
        if (!lastInventoryDrop_ || now - *lastInventoryDrop_ > std::chrono::milliseconds(300))
            inventoryDropPitch_ = 0.5F;
        const float pitch = inventoryDropPitch_;
        inventoryDropPitch_ = std::min(1.5F, inventoryDropPitch_ + 0.1F);
        lastInventoryDrop_ = now;
        return {sound("drop_item", 1.0F, pitch, SoundGroup::Item)};
    }
    if (intent == InventoryIntent::Swap || before == after)
        return {sound("swap_slot", 0.5F, 1.0F, SoundGroup::Item)};
    if (after < before)
        return {sound("hold_item", 0.6F, 1.0F, SoundGroup::Item)};
    return {sound("place_inventory_slot", 0.3F, 1.0F, SoundGroup::Item)};
}

std::vector<SoundCommand> SoundEngine::onLobbyInventory(const std::vector<InventorySlotState>& slots) {
    const auto isItem = [&slots](std::size_t slot, std::string_view id) {
        return slot < slots.size() && slots[slot].count > 0 && slots[slot].itemId == id;
    };
    const auto isBed = [&slots](std::size_t slot) {
        return slot < slots.size() && slots[slot].count > 0 &&
               (slots[slot].itemId == "minecraft:bed" || endsWith(slots[slot].itemId, "_bed"));
    };

    int pattern = 0;
    if (isBed(0) && isItem(2, "minecraft:book") && isItem(4, "minecraft:nether_star") &&
        isItem(6, "minecraft:clock") && isItem(8, "minecraft:emerald")) {
        pattern = 2;
    } else if (isItem(0, "minecraft:book") && isBed(8)) {
        pattern = 1;
    }

    if (pattern == 0) {
        lobbyPattern_ = 0;
        return {};
    }
    if (pattern == lobbyPattern_) return {};
    lobbyPattern_ = pattern;
    return {sound(pattern == 1 ? "game_join" : "game_join2", 1.0F, 1.0F, SoundGroup::BedWars)};
}

std::vector<SoundCommand> SoundEngine::onCompactTowerPlaced(TimePoint now) {
    if (lastCompactTower_ && now - *lastCompactTower_ < std::chrono::milliseconds(250)) return {};
    lastCompactTower_ = now;
    return {sound("tower", 1.0F, 1.0F, SoundGroup::BedWars)};
}

std::vector<SoundCommand> SoundEngine::onBedWarsTntSpawn(SoundPosition position, float distance) const {
    if (!std::isfinite(distance) || distance < 0.0F || distance > 16.0F) return {};
    auto command = sound("timer", 1.0F, 1.0F, SoundGroup::BedWars);
    command.position = position;
    return {std::move(command)};
}

std::vector<SoundCommand> SoundEngine::onBedWarsBedBroken(SoundPosition playerPosition, TimePoint now) {
    if (lastBedWarsBedBreak_ && now - *lastBedWarsBedBreak_ < std::chrono::milliseconds(250)) return {};
    lastBedWarsBedBreak_ = now;
    auto command = sound("bed_dig", 1.0F, 1.0F, SoundGroup::BedWars);
    command.position = playerPosition;
    return {std::move(command)};
}

std::vector<SoundCommand> SoundEngine::onTitleMessage(std::string text, TimePoint now) {
    text = normalizeServerText(std::move(text));
    if (text.empty()) return {};
    if (lastServerMessage_ && text == lastServerText_ &&
        now - *lastServerMessage_ < std::chrono::milliseconds(750)) {
        return {};
    }
    lastServerText_ = text;
    lastServerMessage_ = now;

    std::optional<SoundCommand> result;
    if (containsAny(text, {"victory", "chiến thắng"}))
        result = sound("victory", 1.0F, 1.0F, SoundGroup::BedWars);
    else if (containsAny(text, {"game over", "kết thúc"}))
        result = sound("defeat", 1.0F, 1.0F, SoundGroup::BedWars);
    else if (containsAny(text, {"sudden death", "tử chiến"}))
        result = sound("deathmatch", 1.0F, 1.0F, SoundGroup::BedWars);
    else if (containsAny(text, {"spectating", "đang theo dõi"}))
        result = sound("spectate_player", 1.0F, 1.0F, SoundGroup::BedWars);
    else if (containsAny(text, {"respawned", "đã hồi sinh"})) {
        if (!lastPlayerRespawnSound_ || now - *lastPlayerRespawnSound_ >= std::chrono::milliseconds(750)) {
            result = sound("revive", 1.0F, 1.0F, SoundGroup::Status);
        }
        lastPlayerRespawnSound_ = now;
    } else if (containsAny(text, {"exiting spectator mode", "đã thoát chế độ theo dõi"}))
        result = sound("spectate_map", 1.0F, 1.0F, SoundGroup::BedWars);
    else if (containsAny(text, {"bed destroyed", "giường đã bị phá"}))
        result = sound("bed_destroy", 1.0F, 1.0F, SoundGroup::BedWars);
    else if (contains(text, "afk"))
        result = sound("afk", 1.0F, 1.0F, SoundGroup::BedWars);
    else if (containsAny(text, {"you died", "bạn đã chết"})) {
        if (!lastPlayerDeathSound_ || now - *lastPlayerDeathSound_ >= std::chrono::milliseconds(750)) {
            result = sound("player_died", 1.0F, 1.0F, SoundGroup::Status);
        }
        lastPlayerDeathSound_ = now;
        awaitingRespawnSound_ = true;
    } else if (containsAny(text, {"bắt đầu", "fight"}))
        result = sound("fight", 1.0F, 1.0F, SoundGroup::BedWars);

    if (!result) return {};
    lastChatSound_ = now;
    return {*result};
}

std::vector<SoundCommand> SoundEngine::onChatMessage(std::string text, std::string playerName, TimePoint now) {
    text = normalizeServerText(std::move(text));
    playerName = lowerAscii(std::move(playerName));
    if (!text.empty() && lastServerMessage_ && text == lastServerText_ &&
        now - *lastServerMessage_ < std::chrono::milliseconds(750)) {
        return {};
    }
    lastServerText_ = text;
    lastServerMessage_ = now;
    if (lastChatSound_ && now - *lastChatSound_ < ChatCooldown) return {};

    std::vector<SoundCommand> result;
    auto addIf = [&](std::string_view id, std::initializer_list<std::string_view> phrases) {
        if (containsAny(text, phrases)) result.push_back(sound(std::string(id)));
    };
    const bool rejectedPurchase = containsAny(text, {"not enough", "don't have enough", "already purchased",
                                                      "already have a higher tier", "no emerald", "không đủ",
                                                      "đã mua món đó rồi", "cần thêm"});
    if (rejectedPurchase) result.push_back(sound("no"));
    addIf("dig_warning", {"can't break", "cannot break", "không thể phá"});
    addIf("coin_recieve", {"coins!", "coin reward", "mystery dust", "quest completed", "phần thưởng chiến thắng"});
    addIf("sharpness_upgrade", {"sharpened swords", "sharpness upgrade", "nâng cấp sắc bén"});
    addIf("trap_trigger", {"trap has been triggered", "was set off", "đã được kích hoạt"});
    addIf("slow_falling", {"slow falling", "giày đệm êm"});
    if (contains(text, "reinforced armor iv")) result.push_back(sound("armor_upgrade4"));
    else if (contains(text, "reinforced armor iii")) result.push_back(sound("armor_upgrade3"));
    else if (contains(text, "reinforced armor ii")) result.push_back(sound("armor_upgrade2"));
    else if (contains(text, "reinforced armor i")) result.push_back(sound("armor_upgrade1"));
    if (contains(text, "maniac miner ii")) result.push_back(sound("mining_upgrade2"));
    else if (contains(text, "maniac miner i")) result.push_back(sound("mining_upgrade1"));
    addIf("heal_pool", {"heal pool"});
    addIf("dragon_buff", {"dragon buff", "cushioned boost", "first person spectating", "select a player using your compass"});
    addIf("rejoin", {"you will respawn", "respawning in"});
    addIf("trap_alarm", {"alarm trap"});
    addIf("trap_counter", {"counter-offensive trap"});
    addIf("trap_alarm2", {"blindness trap"});
    addIf("trap_miner", {"miner fatigue trap"});
    addIf("forge_upgrade1", {"iron forge"});
    addIf("forge_upgrade2", {"golden forge"});
    addIf("forge_upgrade3", {"emerald forge", "molten forge"});
    addIf("spectate_map", {"you are a spectator", "see other spectators", "đang theo dõi phòng"});
    addIf("disconnect", {"disconnected", "has quit", "đã thoát"});
    addIf("3", {"game starts in 3 seconds", "trận đấu sẽ bắt đầu sau 3 giây", "trò chơi bắt đầu trong 3 giây"});
    addIf("2", {"game starts in 2 seconds", "trận đấu sẽ bắt đầu sau 2 giây", "trò chơi bắt đầu trong 2 giây"});
    addIf("1", {"game starts in 1 second", "trận đấu sẽ bắt đầu sau 1 giây", "trò chơi bắt đầu trong 1 giây"});
    addIf("fight", {"protect your bed and destroy", "nhiệm vụ của bạn là bảo vệ giường"});
    addIf("experience_received", {"bed wars xp", "điểm kinh nghiệm"});
    addIf("login", {"you now have night vision", "Đăng nhập thành công"});
    addIf("diamond_armor_buy", {"permanent diamond armor", "giáp kim cương"});
    addIf("chain_armor_buy", {"permanent chainmail armor", "giáp xích"});
    addIf("iron_armor_buy", {"permanent iron armor", "giáp sắt"});
    addIf("potion_purchase", {"speed ii potion", "jump v potion", "jump boost v potion", "invisibility potion",
                              "thuốc tăng tốc", "thuốc nhảy cao", "thuốc tàng hình"});
    addIf("ender_pearl_purchase", {"purchased ender pearl", "đã mua ngọc ender"});
    addIf("diamond_sword_purchase", {"purchased diamond sword", "đã mua kiếm kim cương"});
    addIf("obsidian_purchase", {"purchased obsidian", "đã mua hắc diện thạch"});
    addIf("bow_emerald_purchase", {"bow (power i, punch i)", "bow with punch i", "cung (sức mạnh i, bật lùi i)"});
    addIf("bow_gold_purchase", {"bow (power i)", "bow with power i", "cung (sức mạnh i)"});
    addIf("place_warning", {"can't place blocks here", "cannot build", "build height limit"});
    addIf("error", {"cannot say the same message", "please wait", "not allowed to use"});
    addIf("item_put", {"deposited"});
    addIf("game_event", {"generators have been upgraded", "generators upgraded", "máy tạo tài nguyên được nâng cấp"});
    addIf("team_select", {"joined the team", "you started the", "đã tham gia team", "đã chọn đội"});
    addIf("bed_destroy", {"all beds have been destroyed", "tất cả giường đã bị phá hủy"});
    addIf("claim", {"successfully claimed", "đã nhận quà"});
    addIf("invisibility_disable", {"no longer invisible", "không còn tàng hình"});
    addIf("inventory_open", {"player visibility enabled"});
    addIf("inventory_close", {"player visibility disabled"});
    addIf("slowness", {"you no longer have"});
    addIf("speed", {"you now have speed"});
    addIf("team_eliminated", {"team eliminated", "no longer see other spectators"});
    addIf("you_eliminated", {"you have been eliminated", "bạn đã bị loại"});
    addIf("creature_use", {"creature will fight for you", "dream defender", "bed bug"});
    addIf("experience_earned", {"quest completed", "offensive completed", "defensive completed", "support completed"});

    if (contains(text, "purchased bridge egg")) result.push_back(sound("egg_purchase"));
    if ((contains(text, "you purchased") || contains(text, "đã mua")) && !rejectedPurchase)
        result.push_back(sound("yes"));
    if (!playerName.empty()) {
        const bool byPlayer = contains(text, "by " + playerName) || contains(text, "bởi " + playerName);
        if (byPlayer && containsAny(text, {"shot by", "sniped by", "bị bắn"})) {
            result.push_back(sound("headshot")); result.push_back(sound("arrow_kill_player"));
        } else if (byPlayer && containsAny(text, {"love bomb", "nổ tung"})) {
            result.push_back(sound("arrow_hit_combo"));
        } else if (contains(text, playerName + " đã gọi thêm người chơi")) {
            result.push_back(sound("call"));
        } else if (byPlayer && containsAny(text, {"void", "vực", "đẩy"})) {
            if (lastVoidSound_ && now - *lastVoidSound_ > std::chrono::seconds(10)) voidPitch_ = 1.0F;
            result.push_back(sound("void", 1.0F, voidPitch_));
            voidPitch_ = std::min(1.5F, voidPitch_ + 0.05F);
            lastVoidSound_ = now;
        } else if ((byPlayer && containsAny(text, {"bed", "giường"})) || contains(text, "giường của bạn đã bị phá")) {
            result.push_back(sound("bed_dig"));
        } else if (byPlayer || contains(text, "combat to " + playerName) ||
                   contains(text, "edge with " + playerName) || contains(text, "drinking contest with " + playerName)) {
            result.push_back(sound("kill"));
            if (lastChatKill_ && now - *lastChatKill_ > std::chrono::seconds(10)) killCombo_ = 1;
            result.push_back(sound("star" + std::to_string(killCombo_)));
            killCombo_ = std::min(5U, killCombo_ + 1);
            lastChatKill_ = now;
        }
        if (contains(text, playerName + " đã tham gia trò chơi")) result.push_back(sound("game_join"));
    }
    if (!result.empty()) {
        lastChatSound_ = now;
        for (auto& command : result) command.group = SoundGroup::BedWars;
    }
    return result;
}

void SoundEngine::expire(TimePoint now) {
    std::erase_if(attacks_, [now](const auto& entry) {
        const auto& attack = entry.second;
        const auto reference = attack.damageConfirmed ? attack.confirmedAt : attack.attackedAt;
        const auto window = attack.damageConfirmed ? DeathWindow : HitWindow;
        return now - reference > window;
    });
    for (auto it = projectiles_.begin(); it != projectiles_.end();) {
        if (now - it->second.spawnedAt > ProjectileMaximumLifetime) {
            projectileUniqueByRuntime_.erase(it->second.runtimeId);
            it = projectiles_.erase(it);
        } else {
            ++it;
        }
    }
    std::erase_if(deflectedProjectiles_, [now](const auto& entry) {
        return now - entry.second > DeflectCooldown * 2;
    });
}

std::optional<std::string> SoundEngine::impactSound(WeaponKind weapon) {
    switch (weapon) {
    case WeaponKind::WoodenSword: return "soundify.sword_impact";
    case WeaponKind::Sword: return "soundify.sword_impact";
    case WeaponKind::Axe: return "soundify.axe_impact";
    case WeaponKind::Tool: return "soundify.tool_impact";
    case WeaponKind::Stick: return "soundify.stick_swing";
    case WeaponKind::Mace: return "soundify.mace_impact";
    case WeaponKind::Trident: return "soundify.trident_swing";
    case WeaponKind::Other: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::string> SoundEngine::selectionSound(const std::string& itemId) {
    if (itemId.empty()) return "soundify.default_select";
    if (!itemId.starts_with("minecraft:")) return std::nullopt;
    const auto custom = [&](std::string_view id) { return std::optional<std::string>{"soundify." + std::string(id)}; };
    if (itemId == "minecraft:clock") return custom("clock_select");
    if (itemId == "minecraft:compass" || itemId == "minecraft:recovery_compass") return custom("compass_select");
    if (contains(itemId, "bucket") && itemId != "minecraft:bucket") return custom("water_select");
    if (itemId == "minecraft:shears") return custom("shear_select");
    if (itemId == "minecraft:ender_pearl" || itemId == "minecraft:ender_eye") return custom("pearl_select");
    if (itemId == "minecraft:fire_charge") return custom("fireball_select");
    if (itemId == "minecraft:flint_and_steel") return custom("flint_select");
    if (itemId == "minecraft:bow") return custom("bow_select");
    if (itemId == "minecraft:crossbow") return custom("crossbow_select");
    if (itemId == "minecraft:trident") return custom("trident_select");
    if (itemId == "minecraft:mace") return custom("mace_select");
    if (itemId == "minecraft:shield") return custom("shield_select");
    if (itemId == "minecraft:end_crystal") return custom("end_crystal_select");
    if (itemId == "minecraft:spyglass") return custom("spyglass_select");
    if (itemId == "minecraft:brush") return custom("brush_select");
    if (itemId == "minecraft:elytra") return custom("elytra_select");
    if (itemId == "minecraft:fishing_rod" || contains(itemId, "_on_a_stick")) return custom("fishing_rod_select");
    if (containsAny(itemId, {"book", "paper", "map"})) return custom("paper_select");
    if (containsAny(itemId, {"potion", "bottle"})) return custom("potion_select");
    if (itemId == "minecraft:lever" || itemId == "minecraft:tripwire_hook") return custom("lever_select");
    if (contains(itemId, "button") || contains(itemId, "pressure_plate")) return custom("redstone_select");
    if (endsWith(itemId, "_spear") || itemId == "minecraft:spear") return custom("spear_select");
    if (endsWith(itemId, "_sword")) return custom(itemId == "minecraft:wooden_sword" ? "wooden_tool_select" : "sword_select");
    if (endsWith(itemId, "_axe") || endsWith(itemId, "_pickaxe") || endsWith(itemId, "_shovel") || endsWith(itemId, "_hoe")) {
        return custom(contains(itemId, "wooden_") ? "wooden_tool_select" : "metal_tool_select");
    }
    return custom("default_select");
}

std::optional<std::string> SoundEngine::primarySound(const std::string& itemId) {
    if (!itemId.starts_with("minecraft:")) return std::nullopt;
    if (endsWith(itemId, "_sword")) return "soundify.sword_swing";
    if (endsWith(itemId, "_axe")) return "soundify.axe_swing";
    if (endsWith(itemId, "_pickaxe") || endsWith(itemId, "_shovel") || endsWith(itemId, "_hoe")) return "soundify.tool_swing";
    if (itemId == "minecraft:stick") return "soundify.stick_swing";
    if (itemId == "minecraft:mace") return "soundify.mace_swing";
    if (itemId == "minecraft:trident") return "soundify.trident_swing";
    return std::nullopt;
}

std::optional<std::string> SoundEngine::effectSound(int effectId) {
    switch (effectId) {
    case 1: return "speed";
    case 2: return "slowness";
    case 4: return "mining_fatigue";
    case 5: return "strength";
    case 8: return "leaping";
    case 10: return "regen";
    case 11: return "resistance";
    case 12: return "fire_resistance";
    case 13: return "water_breathing";
    case 14: return "invisibility";
    case 16: return "nightvision";
    case 18: return "weakness";
    case 19: return "poison";
    case 20: return "player_died";
    case 22: return "absorption";
    case 27: return "slow_falling";
    default: return std::nullopt;
    }
}

float SoundEngine::nextPitch(float minimum, float maximum) {
    effectPitchState_ = effectPitchState_ * 1664525U + 1013904223U;
    const float unit = static_cast<float>(effectPitchState_ & 0x00FFFFFFU) / 16777215.0F;
    return minimum + unit * (maximum - minimum);
}

} // namespace soundify
