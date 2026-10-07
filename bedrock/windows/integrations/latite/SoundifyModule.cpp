#include "pch.h"
#include "SoundifyModule.h"
#include "AddActorPacketLayout.h"
#include "AnimatePacketLayout.h"
#include "LevelSoundEventPacketLayout.h"
#include "MobEffectPacketLayout.h"
#include "PlayerStartItemCooldownPacketLayout.h"
#include "RemoveActorPacketLayout.h"
#include "SetDisplayObjectivePacketLayout.h"
#include "UpdateAttributesPacketLayout.h"
#include "client/Latite.h"
#include "client/event/events/AttackEvent.h"
#include "client/event/events/AfterMoveEvent.h"
#include "NativeTextInputEvent.h"
#include "soundify/core/GameVersion.hpp"
#include "soundify/core/ScreenLayer.hpp"
#include "soundify/core/ProjectileImpact.hpp"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/ClientTextEvent.h"
#include "client/event/events/KeyUpdateEvent.h"
#include "client/event/events/LeaveGameEvent.h"
#include "client/event/events/PacketReceiveEvent.h"
#include "client/event/events/RenderLayerEvent.h"
#include "client/event/events/TickEvent.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/client/gui/ScreenView.h"
#include "mc/common/client/gui/controls/UIControl.h"
#include "mc/common/client/gui/controls/VisualTree.h"
#include "mc/common/network/packet/ActorEventPacket.h"
#include "mc/common/network/packet/SetTitlePacket.h"
#include "mc/common/network/packet/TextPacket.h"
#include "mc/common/world/ItemStack.h"
#include "mc/common/world/Minecraft.h"
#include "mc/common/world/level/BlockSource.h"
#include "mc/common/world/level/HitResult.h"
#include "mc/common/world/level/block/Block.h"
#include "util/Util.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <system_error>

namespace {
// ActorFlags are protocol state, not PlayerAuthInput requests. Values from LeviLamina's
// ActorFlags.h, unchanged from its 1.26.40 headers through its 1.26.51 target (2026-10).
constexpr int GlidingFlag = 32;
constexpr int SwimmingFlag = 57;
constexpr auto AuthRefreshInterval = std::chrono::minutes(15);
constexpr auto AuthOfflineGrace = std::chrono::minutes(30);
constexpr auto AuthRetryInterval = std::chrono::seconds(30);
constexpr auto TokenPollInterval = std::chrono::seconds(2);

extern "C" IMAGE_DOS_HEADER __ImageBase;

soundify::WeaponKind weaponFor(std::string_view id) {
    using W = soundify::WeaponKind;
    if (!id.starts_with("minecraft:")) return W::Other;
    if (id.ends_with("_sword")) return W::Sword;
    if (id.ends_with("_pickaxe") || id.ends_with("_shovel") || id.ends_with("_hoe")) return W::Tool;
    if (id.ends_with("_axe")) return W::Axe;
    if (id == "minecraft:stick") return W::Stick;
    if (id == "minecraft:mace") return W::Mace;
    if (id == "minecraft:trident") return W::Trident;
    return W::Other;
}

float volumeMultiplier(const ValueType& setting) {
    return std::clamp(std::get<FloatValue>(setting).value / 100.f, 0.f, 1.f);
}

bool isContainerScreen(std::string_view name) {
    static constexpr auto markers = std::to_array<std::string_view>({
        "inventory", "container", "chest", "crafting", "furnace", "hopper", "dispenser", "dropper",
        "brewing", "enchant", "anvil", "grindstone", "smithing", "loom", "stonecutter", "cartography",
        "shulker", "barrel", "merchant", "trade", "beacon", "smoker", "lectern", "crafter",
    });
    return std::any_of(markers.begin(), markers.end(),
                       [name](std::string_view marker) { return name.find(marker) != std::string_view::npos; });
}

bool hasSolidBlockAbove(SDK::Actor& player, SDK::BlockSource* region) {
    if (!region || !player.aabbShape) return false;
    const auto& box = player.getBoundingBox();
    const int y = static_cast<int>(std::floor(box.higher.y + 0.02F));
    const std::array<int, 2> xs = {
        static_cast<int>(std::floor(box.lower.x + 0.02F)),
        static_cast<int>(std::floor(box.higher.x - 0.02F)),
    };
    const std::array<int, 2> zs = {
        static_cast<int>(std::floor(box.lower.z + 0.02F)),
        static_cast<int>(std::floor(box.higher.z - 0.02F)),
    };
    for (const int x : xs)
        for (const int z : zs)
            if (region->isSolidBlockingBlock(x, y, z)) return true;
    return false;
}

std::string stripMinecraftFormatting(std::string value) {
    constexpr std::string_view marker{"\xC2\xA7"};
    for (std::size_t pos = value.find(marker); pos != std::string::npos; pos = value.find(marker, pos)) {
        value.erase(pos, std::min<std::size_t>(marker.size() + 1, value.size() - pos));
    }
    return value;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool hasEnchantmentId(std::string_view text, int wanted) {
    for (std::size_t pos = text.find("id"); pos != std::string_view::npos; pos = text.find("id", pos + 2)) {
        auto cursor = pos + 2;
        while (cursor < text.size() && (text[cursor] == '"' || std::isspace(static_cast<unsigned char>(text[cursor]))))
            ++cursor;
        if (cursor >= text.size() || (text[cursor] != ':' && text[cursor] != '=')) continue;
        ++cursor;
        while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
        int value = 0;
        bool foundDigit = false;
        while (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor]))) {
            foundDigit = true;
            value = value * 10 + (text[cursor++] - '0');
        }
        if (foundDigit && value == wanted) return true;
    }
    return false;
}

bool hasEnchantment(const SDK::ItemStack* stack, int numericId, std::string_view name) {
    if (!stack || !stack->tag) return false;
    try {
        auto text = lowerAscii(stack->tag->toString());
        const auto enchantments = text.find("ench");
        if (enchantments == std::string::npos) return false;
        const auto end = text.find(']', enchantments);
        const auto section = std::string_view(text).substr(
            enchantments, end == std::string::npos ? std::string::npos : end - enchantments + 1);
        const auto namespacedName = "minecraft:" + std::string(name);
        const auto quotedName = "\"" + std::string(name) + "\"";
        return section.find(namespacedName) != std::string_view::npos ||
               section.find(quotedName) != std::string_view::npos || hasEnchantmentId(section, numericId);
    } catch (...) {
        return false;
    }
}

std::optional<std::string> leatherColor(const SDK::ItemStack* stack) {
    if (!stack || !stack->tag) return std::string{"default"};
    try {
        const auto text = lowerAscii(stack->tag->toString());
        auto key = text.find("customcolor");
        if (key == std::string::npos) key = text.find("custom_color");
        if (key == std::string::npos) key = text.find("dyed_color");
        if (key == std::string::npos) return std::string{"default"};
        auto value = text.find_first_of(":=", key);
        if (value == std::string::npos) return std::nullopt;
        ++value;
        while (value < text.size() && std::isspace(static_cast<unsigned char>(text[value]))) ++value;
        const auto end = text.find_first_not_of("-+0123456789abcdefx", value);
        if (end == value) return std::nullopt;
        return text.substr(value, end - value);
    } catch (...) {
        return std::nullopt;
    }
}

bool isBedBlock(SDK::Block* block) {
    auto* legacy = block ? block->legacyBlock : nullptr;
    if (!legacy) return false;
    const auto id = legacy->namespacedId.getString();
    return id == "minecraft:bed" || (id.starts_with("minecraft:") && id.ends_with("_bed"));
}
}

SoundifyModule::SoundifyModule()
    : Module("Soundify", L"Soundify (experimental)",
             L"Requires the Soundify resource pack. Native integration under development.", GAME, nokeybind) {
    addSetting("combatApproximation", L"Experimental combat sounds",
               L"Critical hits use server confirmation; hit and kill attribution still use timing correlation.",
               combatApproximation_);
    addSetting("actionSounds", L"Weapon and item use sounds",
               L"Swing, bow, trident, shield, throwable and utility item sounds.", actionSounds_);
    addSetting("specialExplosionSounds", L"Special explosion sounds",
               L"Use Java Soundify sounds for creepers, end crystals and locally used respawn anchors.",
               specialExplosionSounds_);
    addSliderSetting("masterVolume", L"Soundify master volume", L"Volume of every native Soundify sound, in percent.",
                     masterVolume_, FloatValue(0.f), FloatValue(100.f), FloatValue(5.f));
    addSliderSetting("attackVolume", L"Combat sound volume",
                     L"Volume of Soundify weapon swings, hits, critical and kill sounds, in percent.",
                     attackVolume_, FloatValue(0.f), FloatValue(100.f), FloatValue(5.f));
    addSliderSetting("itemVolume", L"Item sound volume", L"Volume of hotbar and item-use sounds, in percent.",
                     itemVolume_, FloatValue(0.f), FloatValue(100.f), FloatValue(5.f));
    addSetting("inventorySounds", L"Inventory interaction sounds",
               L"Plays only after a clicked or keyed inventory transaction changes a player slot.",
               inventorySounds_);
    addSetting("movementSounds", L"Movement sounds", L"Sneak, stand, sprint, swim, elytra and head-hit sounds.",
               movementSounds_);
    addSliderSetting("movementVolume", L"Movement sound volume", L"Volume of movement and armor overlays, in percent.",
                     movementVolume_, FloatValue(0.f), FloatValue(100.f), FloatValue(5.f));
    addSetting("playerStatusSounds", L"Death and respawn sounds",
               L"Plays Soundify sounds on confirmed local health transitions.", playerStatusSounds_);
    addSliderSetting("statusVolume", L"Status sound volume", L"Volume of heartbeat, death and respawn sounds, in percent.",
                     statusVolume_, FloatValue(0.f), FloatValue(100.f), FloatValue(5.f));
    addSetting("effectSounds", L"Potion effect sounds",
               L"Plays Soundify sounds when a local potion effect is newly applied.", effectSounds_);
    addSliderSetting("effectVolume", L"Potion effect volume", L"Volume of newly applied potion effects, in percent.",
                     effectVolume_, FloatValue(0.f), FloatValue(100.f), FloatValue(5.f));
    addSetting("experienceSounds", L"Experience level sounds",
               L"Plays the Java Soundify level-up sound except at levels divisible by five.", experienceSounds_);
    addSetting("heartbeatSound", L"Low health heartbeat", L"Heartbeat at four health or lower.", heartbeatSound_);
    addSetting("interfaceSounds", L"Interface sounds", L"Screen open, close and keyboard sounds.", interfaceSounds_);
    addSliderSetting("interfaceVolume", L"Interface sound volume", L"Volume of screen and keyboard sounds, in percent.",
                     interfaceVolume_, FloatValue(0.f), FloatValue(100.f), FloatValue(5.f));
    addSetting("bedwarsSounds", L"BedWars sounds",
               L"Messages, lobby hotbar patterns and confirmed Compact Pop-up Tower placement.", bedwarsSounds_);
    addSliderSetting("bedwarsVolume", L"BedWars sound volume", L"Volume of BedWars chat and title sounds, in percent.",
                     bedwarsVolume_, FloatValue(0.f), FloatValue(100.f), FloatValue(5.f));
    listen<AttackEvent>(&SoundifyModule::onAttack);
    listen<PacketReceiveEvent>(&SoundifyModule::onPacket);
    listen<ClickEvent>(&SoundifyModule::onClick);
    listen<AfterMoveEvent>(&SoundifyModule::onMove);
    listen<NativeTextInputEvent>(&SoundifyModule::onCharacter);
    listen<KeyUpdateEvent>(&SoundifyModule::onKey);
    listen<ClientTextEvent>(&SoundifyModule::onText);
    listen<RenderLayerEvent>(&SoundifyModule::onRenderLayer);
    listen<TickEvent>(&SoundifyModule::onTick, true);
    listen<LeaveGameEvent>(&SoundifyModule::onLeave, true);
    startAuthCheck();
    try {
        std::error_code ignored;
        if (!std::filesystem::exists(soundify::security::defaultTokenPath(), ignored)) launchLoginApp();
    } catch (const std::exception&) {
        // No LOCALAPPDATA: the license check reports it; never break Latite's start-up.
    }
    // Soundify is the product's primary module. New installs should work immediately;
    // Latite's saved configuration can still apply the user's later choice.
    setEnabled(true);
}

std::string_view SoundifyModule::gameBuild() const { return Latite::get().gameVersion; }
soundify::security::AuthOptions SoundifyModule::authOptions() {
    soundify::security::AuthOptions options;
    options.minecraftVersion = std::string(Latite::get().gameVersion);
    return options;
}
SoundifyModule::~SoundifyModule() {
    // Stop callbacks before the engine and mutex members are destroyed.
    Eventing::get().unlisten(this);
    if (authFuture_.valid()) authFuture_.wait();
}
void SoundifyModule::onEnable() {
    std::lock_guard lock(mutex_);
    reset();
    Logger::Info("Soundify adapter: client={}, game={}, licensed={}, combatApproximation={}",
                 soundify::security::BedrockClientVersion, gameBuild(), authenticated_, static_cast<bool>(std::get<BoolValue>(combatApproximation_)));
    if (!soundify::matchesGameLine(gameBuild(), soundify::NativeGameLine))
        Logger::Warn("Soundify is inactive: this build targets Minecraft {}", soundify::NativeGameLine);
    else if (!authenticated_) Logger::Warn("Soundify is inactive until GDuck Store login succeeds");
}
bool SoundifyModule::supported() const {
    // The packet layouts are checked per line (see *PacketLayout.h), not per hotfix.
    return soundify::matchesGameLine(gameBuild(), soundify::NativeGameLine) && authenticated_;
}

void SoundifyModule::startAuthCheck(bool refresh) {
    if (authFuture_.valid()) return;
    authCheckWasRefresh_ = refresh && authenticated_;
    authFuture_ = std::async(std::launch::async, [this] { return authClient_.verifyStored(); });
}

void SoundifyModule::launchLoginApp() {
    if (loginLaunched_) return;
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path.data(),
                                           static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        Logger::Warn("Soundify could not locate its login application");
        return;
    }
    auto executable = std::filesystem::path(std::wstring_view(path.data(), length)).parent_path() /
                      L"Soundify-Login.exe";
    if (!std::filesystem::exists(executable)) {
        Logger::Warn("Soundify-Login.exe is missing beside the injected DLL");
        return;
    }
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    auto application = executable.wstring();
    if (!CreateProcessW(application.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr,
                        executable.parent_path().c_str(), &startup, &process)) {
        Logger::Warn("Soundify could not start Soundify-Login.exe");
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    loginLaunched_ = true;
}

void SoundifyModule::updateAuthentication() {
    const auto now = std::chrono::steady_clock::now();
    if (authFuture_.valid() && authFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const auto result = authFuture_.get();
        if (result.authenticated()) {
            authenticated_ = true;
            loginLaunched_ = false;
            nextAuthCheck_ = now + AuthRefreshInterval;
            authValidUntil_ = now + AuthOfflineGrace;
            Logger::Info("Soundify license verified");
        } else if (result.status == soundify::security::AuthStatus::NetworkError &&
                   authCheckWasRefresh_ && now < authValidUntil_) {
            // Keep an already verified in-memory session during a brief outage and retry soon.
            nextAuthCheck_ = now + AuthRetryInterval;
            Logger::Warn("Soundify license refresh failed: {}", result.message);
        } else {
            authenticated_ = false;
            nextAuthCheck_ = now + AuthRetryInterval;
            Logger::Warn("Soundify license unavailable: {}", result.message);
            if (result.status != soundify::security::AuthStatus::NetworkError) launchLoginApp();
        }
        authCheckWasRefresh_ = false;
    }

    if (now >= nextTokenPoll_) {
        nextTokenPoll_ = now + TokenPollInterval;
        std::error_code ignored;
        const bool tokenExists = std::filesystem::exists(soundify::security::defaultTokenPath(), ignored);
        const bool tokenChanged = tokenExists != tokenObserved_;
        tokenObserved_ = tokenExists;
        if (authenticated_ && !tokenExists) {
            authenticated_ = false;
            reset();
            launchLoginApp();
        } else if (!authenticated_ && tokenExists && !authFuture_.valid() &&
                   (tokenChanged || now >= nextAuthCheck_)) {
            startAuthCheck();
        }
    }
    if (authenticated_ && !authFuture_.valid() && now >= nextAuthCheck_) startAuthCheck(true);
}
void SoundifyModule::play(const soundify::SoundCommand& command) {
    // Latite itself calls PlaySoundUI directly from packet and input listeners. Avoiding
    // a second queue removes up to one game tick of latency from Soundify triggers.
    if (command.id == "soundify.heartbeat" && !std::get<BoolValue>(heartbeatSound_)) return;
    if ((command.id == "soundify.player_died" || command.id == "soundify.revive") &&
        !std::get<BoolValue>(playerStatusSounds_)) return;
    if (command.group == soundify::SoundGroup::Movement && !std::get<BoolValue>(movementSounds_)) return;
    if (command.group == soundify::SoundGroup::Effect && !std::get<BoolValue>(effectSounds_)) return;
    if (command.id == "soundify.levelup" && !std::get<BoolValue>(experienceSounds_)) return;
    float volume = command.volume * volumeMultiplier(masterVolume_);
    switch (command.group) {
    case soundify::SoundGroup::Combat: volume *= volumeMultiplier(attackVolume_); break;
    case soundify::SoundGroup::Item: volume *= volumeMultiplier(itemVolume_); break;
    case soundify::SoundGroup::Movement: volume *= volumeMultiplier(movementVolume_); break;
    case soundify::SoundGroup::Interface: volume *= volumeMultiplier(interfaceVolume_); break;
    case soundify::SoundGroup::Status: volume *= volumeMultiplier(statusVolume_); break;
    case soundify::SoundGroup::Effect: volume *= volumeMultiplier(effectVolume_); break;
    case soundify::SoundGroup::BedWars: volume *= volumeMultiplier(bedwarsVolume_); break;
    case soundify::SoundGroup::General: break;
    }
    if (command.position) {
        auto* instance = SDK::ClientInstance::get();
        auto* level = instance && instance->minecraft ? instance->minecraft->getLevel() : nullptr;
        if (!level) return;
        const auto& position = *command.position;
        const auto started = soundify::SoundEngine::Clock::now();
        level->playSoundEvent(command.id, {position.x, position.y, position.z}, volume, command.pitch);
        reportSlowOperation("positional audio call", started);
        return;
    }
    const auto started = soundify::SoundEngine::Clock::now();
    util::PlaySoundUI(command.id, volume, command.pitch);
    reportSlowOperation("UI audio call", started);
}
std::string SoundifyModule::heldItem() const {
    auto* instance = SDK::ClientInstance::get();
    auto* player = instance ? instance->getLocalPlayer() : nullptr;
    auto* supplies = player ? player->supplies : nullptr;
    if (!supplies || !supplies->inventory || supplies->selectedSlot < 0 || supplies->selectedSlot > 8) return {};
    auto* stack = supplies->inventory->getItem(supplies->selectedSlot);
    auto* item = stack ? stack->getItem() : nullptr;
    return item ? item->namespacedId.getString() : std::string{};
}
soundify::ItemUseTraits SoundifyModule::heldItemTraits() const {
    auto* instance = SDK::ClientInstance::get();
    auto* player = instance ? instance->getLocalPlayer() : nullptr;
    auto* supplies = player ? player->supplies : nullptr;
    if (!supplies || !supplies->inventory || supplies->selectedSlot < 0 || supplies->selectedSlot > 8) return {};
    auto* stack = supplies->inventory->getItem(supplies->selectedSlot);
    return {
        hasEnchantment(stack, 19, "power"),
        hasEnchantment(stack, 21, "flame"),
        hasEnchantment(stack, 34, "piercing"),
    };
}
std::vector<soundify::InventorySlotState> SoundifyModule::inventorySnapshot() const {
    std::vector<soundify::InventorySlotState> result;
    result.reserve(36);
    auto* instance = SDK::ClientInstance::get();
    auto* player = instance ? instance->getLocalPlayer() : nullptr;
    auto* supplies = player ? player->supplies : nullptr;
    auto* inventory = supplies ? supplies->inventory : nullptr;
    if (!inventory) return result;
    for (int slot = 0; slot < 36; ++slot) {
        auto* stack = inventory->getItem(slot);
        auto* item = stack ? stack->getItem() : nullptr;
        result.push_back({item ? item->namespacedId.getString() : std::string{},
                          item ? stack->itemCount : std::uint8_t{0}});
    }
    return result;
}
void SoundifyModule::observeInventory(soundify::SoundEngine::TimePoint now) {
    auto snapshot = inventorySnapshot();
    if (std::get<BoolValue>(bedwarsSounds_) && !snapshot.empty()) client_.onLobbyInventory(snapshot);
    if (!std::get<BoolValue>(inventorySounds_)) {
        client_.onInventorySnapshot({}, false, now);
        return;
    }
    if (containerOpen_ && snapshot.empty()) return;
    client_.onInventorySnapshot(std::move(snapshot), containerOpen_, now);
}
void SoundifyModule::updateHotbar(soundify::SoundEngine::TimePoint now) {
    auto* instance = SDK::ClientInstance::get();
    auto* player = instance ? instance->getLocalPlayer() : nullptr;
    auto* supplies = player ? player->supplies : nullptr;
    if (!supplies || supplies->selectedSlot < 0 || supplies->selectedSlot > 8) return;
    if (slot_ >= 0 && slot_ != supplies->selectedSlot) client_.onHotbarChanged(heldItem(), now);
    slot_ = supplies->selectedSlot;
}
void SoundifyModule::onAttack(Event& event) {
    std::lock_guard lock(mutex_);
    if (!supported()) return;
    auto* target = static_cast<AttackEvent&>(event).getActor();
    if (!target) return;
    const auto now = soundify::SoundEngine::Clock::now();
    const auto held = heldItem();
    if (std::get<BoolValue>(actionSounds_)) {
        const auto type = target->getEntityTypeName();
        if (type == "minecraft:fireball" || type == "fireball" || type == "minecraft:wind_charge_projectile" ||
            type == "wind_charge_projectile" || type == "minecraft:breeze_wind_charge_projectile" ||
            type == "breeze_wind_charge_projectile") {
            const auto& position = target->getPos();
            client_.onProjectileDeflected(static_cast<std::int64_t>(target->getRuntimeID()),
                                          {position.x, position.y, position.z}, now);
        }
        if (held == "minecraft:mace") recentMaceAttack_ = now;
    }
    if (!std::get<BoolValue>(combatApproximation_)) return;
    // Critical-hit evidence is unavailable here; do not guess from jump state.
    client_.onAttack(static_cast<std::int64_t>(target->getRuntimeID()), weaponFor(held), false, now);
}
void SoundifyModule::onPacket(Event& event) {
    std::lock_guard lock(mutex_);
    if (!supported()) return;
    auto* packet = static_cast<PacketReceiveEvent&>(event).getPacket();
    if (!packet) return;
    if (static_cast<std::uint8_t>(packet->getID()) == soundify::protocol::AddActorPacketId) {
        auto* addActor = soundify::protocol::addActorPayload(packet);
        const auto now = soundify::SoundEngine::Clock::now();
        if (std::get<BoolValue>(actionSounds_) &&
            (addActor->actorType == "minecraft:fireball" || addActor->actorType == "fireball")) {
            auto* instance = SDK::ClientInstance::get();
            auto* player = instance ? instance->getLocalPlayer() : nullptr;
            if (player) {
                const auto& playerPosition = player->getPos();
                const auto dx = addActor->position.x - playerPosition.x;
                const auto dy = addActor->position.y - playerPosition.y;
                const auto dz = addActor->position.z - playerPosition.z;
                client_.onFireballSpawn(
                    {addActor->position.x, addActor->position.y, addActor->position.z},
                    std::sqrt(dx * dx + dy * dy + dz * dz));
            }
        }
        if (addActor->actorType == "minecraft:snowball" || addActor->actorType == "snowball") {
            client_.onProjectileSpawn(addActor->uniqueId, addActor->runtimeId, soundify::ProjectileKind::Snowball,
                                      {addActor->position.x, addActor->position.y, addActor->position.z}, now);
        } else if (addActor->actorType == "minecraft:egg" || addActor->actorType == "egg") {
            client_.onProjectileSpawn(addActor->uniqueId, addActor->runtimeId, soundify::ProjectileKind::Egg,
                                      {addActor->position.x, addActor->position.y, addActor->position.z}, now);
        }
        return;
    }
    if (static_cast<std::uint8_t>(packet->getID()) == soundify::protocol::RemoveActorPacketId) {
        auto* removeActor = soundify::protocol::removeActorPayload(packet);
        auto* instance = SDK::ClientInstance::get();
        auto* player = instance ? instance->getLocalPlayer() : nullptr;
        if (player) {
            const auto& position = player->getPos();
            client_.onProjectileRemoved(removeActor->uniqueId, {position.x, position.y, position.z},
                                         !std::get<BoolValue>(actionSounds_), soundify::SoundEngine::Clock::now());
        }
        return;
    }
    if (static_cast<std::uint8_t>(packet->getID()) == soundify::protocol::UpdateAttributesPacketId) {
        auto* update = soundify::protocol::updateAttributesPayload(packet);
        auto* instance = SDK::ClientInstance::get();
        auto* player = instance ? instance->getLocalPlayer() : nullptr;
        const auto attributeCount = update->attributes.validatedSize(64);
        if (player && update->runtimeId == player->getRuntimeID() && attributeCount <= 64) {
            for (std::size_t index = 0; index < attributeCount; ++index) {
                auto& attribute = update->attributes.begin[index];
                if (attribute.name.getString() != "minecraft:player.level") continue;
                if (std::isfinite(attribute.currentValue) && attribute.currentValue >= 0.0F &&
                    attribute.currentValue < static_cast<float>(std::numeric_limits<int>::max())) {
                    client_.onExperienceLevel(static_cast<int>(std::lround(attribute.currentValue)),
                                              player->ticksExisted < 40);
                }
                break;
            }
        }
        return;
    }
    if (static_cast<std::uint8_t>(packet->getID()) == soundify::protocol::MobEffectPacketId) {
        auto* effect = soundify::protocol::mobEffectPayload(packet);
        auto* instance = SDK::ClientInstance::get();
        auto* player = instance ? instance->getLocalPlayer() : nullptr;
        if (player && effect->runtimeId == player->getRuntimeID()) {
            client_.onMobEffect(effect->eventId, effect->effectId, effect->durationTicks, effect->ambient,
                                effect->showParticles,
                                player->ticksExisted < 40);
        }
        return;
    }
    if (static_cast<std::uint8_t>(packet->getID()) ==
        soundify::protocol::PlayerStartItemCooldownPacketId) {
        auto* cooldown = soundify::protocol::playerStartItemCooldownPayload(packet);
        if (std::get<BoolValue>(actionSounds_)) {
            client_.onShieldCooldown(cooldown->itemCategory, cooldown->durationTicks,
                                     soundify::SoundEngine::Clock::now());
        }
        return;
    }
    if (static_cast<std::uint8_t>(packet->getID()) == soundify::protocol::SetDisplayObjectivePacketId) {
        auto* objective = soundify::protocol::setDisplayObjectivePayload(packet);
        if (lowerAscii(objective->displaySlotName) == "sidebar") {
            const auto text = lowerAscii(stripMinecraftFormatting(
                objective->objectiveDisplayName + " " + objective->objectiveName));
            bedWarsScoreboard_ = text.find("bed wars") != std::string::npos ||
                                  text.find("bedwars") != std::string::npos ||
                                  text.find("bed war") != std::string::npos;
            if (!bedWarsScoreboard_) {
                observedBedWarsTnt_.clear();
                trackedBedPosition_.reset();
                trackedBedTime_.reset();
            }
        }
        return;
    }
    if (static_cast<std::uint8_t>(packet->getID()) == soundify::protocol::AnimatePacketId) {
        if (std::get<BoolValue>(combatApproximation_)) {
            auto* animate = soundify::protocol::animatePayload(packet);
            if (animate->action == soundify::protocol::CriticalHitAction) {
                client_.onCriticalHitConfirmed(static_cast<std::int64_t>(animate->targetRuntimeId),
                                               soundify::SoundEngine::Clock::now());
            }
        }
        return;
    }
    if (static_cast<std::uint8_t>(packet->getID()) == soundify::protocol::LevelSoundEventPacketId) {
        auto* levelSound = soundify::protocol::payload(packet);
        // Thrown bottle entity components declare hit_sound=glass. Replace that native
        // impact instead of guessing that any RemoveActor packet means a collision.
        const auto glassNow = soundify::SoundEngine::Clock::now();
        const auto* glassNumeric = std::get_if<std::uint32_t>(&levelSound->soundEvent);
        const auto* glassNamed = std::get_if<std::string>(&levelSound->soundEvent);
        if (((glassNumeric && *glassNumeric == soundify::GlassImpactEvent) ||
             (glassNamed && *glassNamed == "glass")) && glassNow >= nextGlassLog_) {
            nextGlassLog_ = glassNow + std::chrono::seconds(2);
            Logger::Info("Soundify glass event: actor={}, uniqueId={}",
                         levelSound->actorIdentifier, levelSound->actorUniqueId);
        }
        bool bottleImpact = false;
        if (const auto* numeric = std::get_if<std::uint32_t>(&levelSound->soundEvent))
            bottleImpact = soundify::isThrownBottleImpact(levelSound->actorIdentifier, *numeric);
        else if (const auto* named = std::get_if<std::string>(&levelSound->soundEvent))
            bottleImpact = soundify::isThrownBottleImpact(levelSound->actorIdentifier, *named);
        if (bottleImpact && std::get<BoolValue>(actionSounds_)) {
            levelSound->soundEvent = std::string{"soundify.splash_potion_break"};
            Logger::Info("Soundify bottle: native glass impact replaced (actor={})", levelSound->actorIdentifier);
            return;
        }
        const auto soundNow = soundify::SoundEngine::Clock::now();
        if (std::get<BoolValue>(actionSounds_) && recentMaceAttack_ &&
            soundNow - *recentMaceAttack_ <= std::chrono::milliseconds(750) &&
            soundify::protocol::isMaceSmashEvent(levelSound->soundEvent)) {
            client_.onMaceSmash(soundNow);
            recentMaceAttack_.reset();
        }
        const auto* legacyEvent = std::get_if<std::uint32_t>(&levelSound->soundEvent);
        if (legacyEvent && *legacyEvent == soundify::protocol::ExplodeEvent &&
            std::get<BoolValue>(specialExplosionSounds_)) {
            const auto now = soundify::ExplosionRouter::Clock::now();
            const auto replacement = explosionRouter_.route(
                levelSound->actorIdentifier,
                {levelSound->position.x, levelSound->position.y, levelSound->position.z}, now);
            if (replacement) {
                Logger::Info("Soundify explosion: actor={}, sound={}", levelSound->actorIdentifier, *replacement);
                // Let Minecraft perform the normal positional playback. Changing only the
                // event keeps its native attenuation and prevents a second vanilla sound.
                levelSound->soundEvent = *replacement;
            } else {
                Logger::Info("Soundify explosion: actor={}, sound=random.explode", levelSound->actorIdentifier);
            }
        }
        return;
    }
    if (packet->getID() == SDK::PacketID::SET_TITLE && std::get<BoolValue>(bedwarsSounds_)) {
        auto* title = static_cast<SDK::SetTitlePacket*>(packet);
        if (title->type == SDK::TitleType::Title || title->type == SDK::TitleType::Subtitle ||
            title->type == SDK::TitleType::Actionbar || title->type == SDK::TitleType::TitleRaw ||
            title->type == SDK::TitleType::SubtitleRaw || title->type == SDK::TitleType::ActionbarRaw) {
            client_.onTitleMessage(title->text, soundify::SoundEngine::Clock::now());
        }
        return;
    }
    if (packet->getID() != SDK::PacketID::ACTOR_EVENT) return;
    auto* actor = static_cast<SDK::ActorEventPacket*>(packet);
    const auto id = static_cast<std::int64_t>(actor->runtimeID);
    const auto now = soundify::SoundEngine::Clock::now();
    if (static_cast<unsigned char>(actor->eventID) == 32) {
        auto* instance = SDK::ClientInstance::get();
        auto* level = instance && instance->minecraft ? instance->minecraft->getLevel() : nullptr;
        if (level) {
            for (auto* candidate : level->getRuntimeActorList()) {
                if (candidate && candidate->getRuntimeID() == actor->runtimeID) {
                    const auto& position = candidate->getPos();
                    explosionRouter_.onCreeperPrimed(actor->runtimeID, {position.x, position.y, position.z}, now);
                    break;
                }
            }
        }
    }
    if (!std::get<BoolValue>(combatApproximation_)) return;
    if (actor->eventID == SDK::ActorEventID::HURT_ANIMATION) client_.onDamageConfirmed(id, now);
    else if (actor->eventID == SDK::ActorEventID::DEATH_ANIMATION) client_.onDeath(id, now);
}
void SoundifyModule::onClick(Event& event) {
    std::lock_guard lock(mutex_);
    if (!supported()) return;
    auto* instance = SDK::ClientInstance::get();
    if (!instance || !instance->minecraftGame) return;
    auto& click = static_cast<ClickEvent&>(event);
    const auto now = soundify::SoundEngine::Clock::now();
    if (click.getClickType() == ClickEvent::ClickType::Right && click.isDown()) {
        pendingContainerScreen_.clear(); pendingContainerTime_.reset();
        auto* level = instance->minecraft ? instance->minecraft->getLevel() : nullptr;
        auto* hit = level ? level->getHitResult() : nullptr;
        auto* region = instance->getRegion();
        auto* block = hit && hit->hitType == SDK::HitType::BLOCK && region ? region->getBlock(hit->hitBlock) : nullptr;
        auto* legacy = block ? block->legacyBlock : nullptr;
        if (legacy) {
            const auto id = legacy->namespacedId.getString();
            if (isContainerScreen(id) || id.find("lectern") != std::string::npos) {
                pendingContainerScreen_ = id + "_screen";
                pendingContainerTime_ = now;
            }
        }
    }
    // Latite dispatches ClickEvent after Minecraft has consumed mouse input, so
    // the selected slot is already current here. Handle wheel selection without
    // waiting for the next rendered HUD frame.
    if (instance->minecraftGame->isCursorGrabbed() &&
        click.getClickType() == ClickEvent::ClickType::Wheel) {
        updateHotbar(now);
        return;
    }
    if (!instance->minecraftGame->isCursorGrabbed()) {
        if (containerOpen_ && std::get<BoolValue>(inventorySounds_) && click.isDown() &&
            (click.getClickType() == ClickEvent::ClickType::Left ||
             click.getClickType() == ClickEvent::ClickType::Right ||
             click.getClickType() == ClickEvent::ClickType::Middle)) {
            client_.armInventoryInteraction(soundify::InventoryIntent::Pointer, now);
        }
        return;
    }
    const auto item = heldItem();
    if (click.getClickType() == ClickEvent::ClickType::Right && click.isDown() &&
        std::get<BoolValue>(specialExplosionSounds_)) {
        observeRespawnAnchorUse(now);
    }
    if (click.getClickType() == ClickEvent::ClickType::Right && click.isDown() &&
        std::get<BoolValue>(bedwarsSounds_)) {
        observeCompactTowerUse(now);
    }
    if (!std::get<BoolValue>(actionSounds_)) return;
    if (click.getClickType() == ClickEvent::ClickType::Left && click.isDown()) client_.onPrimaryAction(item, now);
    else if (click.getClickType() == ClickEvent::ClickType::Right) {
        client_.onUseChanged(item, click.isDown(), now, heldItemTraits());
    }
}
void SoundifyModule::onMove(Event& event) {
    std::lock_guard lock(mutex_);
    if (!supported() || !std::get<BoolValue>(movementSounds_)) return;
    auto* move = static_cast<AfterMoveEvent&>(event).getMoveInputHandler();
    auto* instance = SDK::ClientInstance::get();
    auto* player = instance ? instance->getLocalPlayer() : nullptr;
    if (!move || !player) return;
    const auto now = soundify::SoundEngine::Clock::now();
    client_.onSneakChanged(move->sneaking, now);
    const auto& velocity = player->getVelocity();
    const bool swimming = player->getStatusFlag(SwimmingFlag);
    const bool gliding = player->getStatusFlag(GlidingFlag);
    client_.onJumpMotion(player->ticksExisted >= 20 && move->rawInputState.jumpInputWasPressed, velocity.y,
                         hasSolidBlockAbove(*player, instance->getRegion()), swimming, gliding, now);
    std::string bootsId;
    if (auto* boots = player->getArmor(3)) {
        if (auto* item = boots->getItem()) bootsId = item->namespacedId.getString();
    }
    client_.onMovement(move->sprinting, std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z), bootsId, now);
}
void SoundifyModule::onCharacter(Event& event) {
    inputMailbox_.push({soundify::NativeInput::Kind::Character,
                        static_cast<NativeTextInputEvent&>(event).codePoint, false, true, false,
                        soundify::SoundEngine::Clock::now()});
}
void SoundifyModule::onKey(Event& event) {
    auto& keyEvent = static_cast<KeyUpdateEvent&>(event);
    const bool shortcut = (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
                          (GetKeyState(VK_MENU) & 0x8000) != 0;
    inputMailbox_.push({soundify::NativeInput::Kind::Key,
                        static_cast<std::uint32_t>(keyEvent.getKey()), keyEvent.isDown(),
                        keyEvent.inUI(), shortcut, soundify::SoundEngine::Clock::now()});
}
void SoundifyModule::processInput(soundify::SoundEngine::TimePoint now) {
    const bool textScreen = logicalScreen_.find("chat") != std::string::npos ||
                            logicalScreen_.find("sign") != std::string::npos ||
                            logicalScreen_.find("book") != std::string::npos;
    for (const auto& input : inputMailbox_.drain(now)) {
        if (now - input.receivedAt > std::chrono::milliseconds(40))
            reportSlowOperation("input waiting for game frame", input.receivedAt);
        if (input.kind == soundify::NativeInput::Kind::Character) {
            if (!textScreen || !std::get<BoolValue>(interfaceSounds_)) continue;
            const bool hadNativeInput = textInput_.nativeAvailable();
            const bool playType = textInput_.nativeCharacter(input.value, input.receivedAt);
            if (!hadNativeInput && textInput_.nativeAvailable())
                Logger::Info("Soundify typing input: native WM_CHAR (game thread)");
            if (playType) client_.onCharacterTyped(false, input.receivedAt);
            continue;
        }
        const auto keyCode = input.value;
        if (input.pressed && input.inUI && containerOpen_ && std::get<BoolValue>(inventorySounds_)) {
            if (keyCode == 'Q' || keyCode == VK_DELETE)
                client_.armInventoryInteraction(soundify::InventoryIntent::Drop, input.receivedAt);
            else if (keyCode >= '1' && keyCode <= '9')
                client_.armInventoryInteraction(soundify::InventoryIntent::Swap, input.receivedAt);
        }
        if (!std::get<BoolValue>(interfaceSounds_)) continue;
        const bool printable = (keyCode >= 'A' && keyCode <= 'Z') || (keyCode >= '0' && keyCode <= '9') ||
                               (keyCode >= VK_NUMPAD0 && keyCode <= VK_DIVIDE) || keyCode == VK_SPACE ||
                               (keyCode >= VK_OEM_1 && keyCode <= VK_OEM_3) ||
                               (keyCode >= VK_OEM_4 && keyCode <= VK_OEM_8) || keyCode == VK_OEM_102;
        if (input.pressed && input.inUI &&
            textInput_.fallbackKey(printable, input.shortcut, textScreen, input.receivedAt))
            client_.onCharacterTyped(false, input.receivedAt);
        if (keyCode == VK_TAB && !input.inUI)
            client_.onTabListChanged(input.pressed, input.receivedAt);
        else if (input.pressed && input.inUI && textScreen &&
                 (keyCode == VK_BACK || keyCode == VK_DELETE))
            client_.onCharacterTyped(true, input.receivedAt);
    }
}
void SoundifyModule::reportSlowOperation(std::string_view name, soundify::SoundEngine::TimePoint started) {
    const auto now = soundify::SoundEngine::Clock::now();
    const auto elapsed = std::chrono::duration<double, std::milli>(now - started).count();
    if (elapsed < 12.0 || now < nextSlowLog_) return;
    nextSlowLog_ = now + std::chrono::seconds(10);
    Logger::Warn("Soundify latency: {} took {:.1f} ms", name, elapsed);
}
void SoundifyModule::onText(Event& event) {
    std::lock_guard lock(mutex_);
    if (!supported() || !std::get<BoolValue>(bedwarsSounds_)) return;
    auto* packet = static_cast<ClientTextEvent&>(event).getTextPacket();
    const auto* message = packet ? packet->getMessage() : nullptr;
    auto* instance = SDK::ClientInstance::get();
    auto* player = instance ? instance->getLocalPlayer() : nullptr;
    if (message) client_.onChatMessage(*message, player ? player->playerName : std::string{},
                                       soundify::SoundEngine::Clock::now());
}
void SoundifyModule::onRenderLayer(Event& event) {
    const auto started = soundify::SoundEngine::Clock::now();
    auto* instance = SDK::ClientInstance::get();
    auto* view = static_cast<RenderLayerEvent&>(event).getScreenView();
    auto* tree = view ? view->visualTree : nullptr;
    auto* root = tree ? tree->rootControl : nullptr;
    if (!instance || !instance->minecraftGame || !root || root->name == "debug_screen") return;
    const bool cursorGrabbed = instance->minecraftGame->isCursorGrabbed();
    if (!soundify::isActiveScreenLayer(root->name, cursorGrabbed)) return;
    std::lock_guard lock(mutex_);
    if (!supported()) return;
    const auto now = soundify::SoundEngine::Clock::now();
    // Coalesce all effect packets received before this frame, then play on the
    // render cadence instead of waiting as long as one 20 Hz game tick.
    client_.flushPendingSounds();
    if (cursorGrabbed) logicalScreen_ = "hud_screen";
    else if (logicalScreen_.empty() || logicalScreen_ == "hud_screen") {
        logicalScreen_ = root->name;
        if (pendingContainerTime_ && now - *pendingContainerTime_ < std::chrono::seconds(2) &&
            isContainerScreen(root->name)) logicalScreen_ = pendingContainerScreen_;
        pendingContainerScreen_.clear(); pendingContainerTime_.reset();
        Logger::Info("Soundify screen opened: {}", logicalScreen_);
    }
    containerOpen_ = !cursorGrabbed && isContainerScreen(logicalScreen_);
    processInput(now);
    observeInventory(now);
    // ScreenView renders more often than the 20 Hz game tick. Observing the slot here cuts
    // keyboard, wheel and controller hotbar latency from up to one tick to about one frame.
    if (cursorGrabbed) updateHotbar(now);
    if (std::get<BoolValue>(interfaceSounds_)) client_.onScreenChanged(logicalScreen_, now);
    reportSlowOperation("active screen callback", started);
}
void SoundifyModule::onTick(Event& event) {
    const auto started = soundify::SoundEngine::Clock::now();
    std::lock_guard lock(mutex_);
    // Tick runs while the module is off only to keep the license fresh; `.toggle Soundify`
    // must silence hotbar, inventory, movement and health sounds too.
    updateAuthentication();
    if (!isEnabled() || !supported()) { reset(); return; }
    auto* instance = SDK::ClientInstance::get();
    auto* player = instance ? instance->getLocalPlayer() : nullptr;
    if (!player) { reset(); return; }
    const auto* level = static_cast<TickEvent&>(event).getLevel();
    const auto* dimension = player->dimension.get();
    if (level_ != level || player_ != player || dimension_ != dimension) {
        reset(); level_ = level; player_ = player; dimension_ = dimension;
    }
    const auto now = soundify::SoundEngine::Clock::now();
    processInput(now);
    observeTrackedActors(now);
    updateBedWarsWorld(now);
    confirmCompactTower(now);
    // Tick remains a fallback when a frame is not rendered (loading/minimized).
    updateHotbar(now);
    observeInventory(now);
    // These server-synchronized flags change only after the requested pose is accepted.
    client_.onTraversalState(player->getStatusFlag(SwimmingFlag), player->getStatusFlag(GlidingFlag), now);
    if (const auto health = player->getHealth()) client_.onHealth(*health, now);
    client_.tick(now);
    reportSlowOperation("game tick callback", started);
}
void SoundifyModule::observeTrackedActors(soundify::ExplosionRouter::TimePoint now) {
    auto* instance = SDK::ClientInstance::get();
    auto* level = instance && instance->minecraft ? instance->minecraft->getLevel() : nullptr;
    if (!level) return;
    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor) continue;
        const auto type = actor->getEntityTypeName();
        const auto& actorPosition = actor->getPos();
        if (type == "minecraft:snowball" || type == "snowball" || type == "minecraft:egg" || type == "egg") {
            client_.onProjectileMoved(actor->getRuntimeID(),
                                      {actorPosition.x, actorPosition.y, actorPosition.z});
        }
        if (type == "minecraft:creeper" || type == "creeper") {
            explosionRouter_.updatePrimedCreeper(actor->getRuntimeID(),
                                                  {actorPosition.x, actorPosition.y, actorPosition.z});
            continue;
        }
        if (type != "minecraft:ender_crystal" && type != "minecraft:end_crystal" && type != "ender_crystal") continue;
        explosionRouter_.observeEndCrystal(actor->getRuntimeID(),
                                            {actorPosition.x, actorPosition.y, actorPosition.z}, now);
    }
}
void SoundifyModule::observeRespawnAnchorUse(soundify::ExplosionRouter::TimePoint now) {
    // Glowstone charges an anchor instead of detonating it. Tracking only another
    // held item mirrors the Java-side local-interaction guard and avoids replacing
    // unrelated explosions near an anchor that was merely charged.
    if (heldItem() == "minecraft:glowstone") return;
    auto* instance = SDK::ClientInstance::get();
    auto* level = instance && instance->minecraft ? instance->minecraft->getLevel() : nullptr;
    auto* hit = level ? level->getHitResult() : nullptr;
    auto* region = instance ? instance->getRegion() : nullptr;
    if (!hit || hit->hitType != SDK::HitType::BLOCK || !region) return;
    auto* block = region->getBlock(hit->hitBlock);
    auto* legacy = block ? block->legacyBlock : nullptr;
    if (!legacy || legacy->namespacedId.getString() != "minecraft:respawn_anchor") return;
    explosionRouter_.onRespawnAnchorUsed(
        {static_cast<float>(hit->hitBlock.x) + 0.5F, static_cast<float>(hit->hitBlock.y) + 0.5F,
         static_cast<float>(hit->hitBlock.z) + 0.5F}, now);
}
void SoundifyModule::observeCompactTowerUse(soundify::SoundEngine::TimePoint now) {
    const auto itemId = heldItem();
    if (itemId != "minecraft:chest" && itemId != "minecraft:trapped_chest") return;
    auto* instance = SDK::ClientInstance::get();
    auto* player = instance ? instance->getLocalPlayer() : nullptr;
    auto* supplies = player ? player->supplies : nullptr;
    auto* inventory = supplies ? supplies->inventory : nullptr;
    if (!inventory || supplies->selectedSlot < 0 || supplies->selectedSlot > 8) return;
    auto* stack = inventory->getItem(supplies->selectedSlot);
    if (!stack || stripMinecraftFormatting(stack->getHoverName()) != "Compact Pop-up Tower") return;

    auto* level = instance->minecraft ? instance->minecraft->getLevel() : nullptr;
    auto* hit = level ? level->getHitResult() : nullptr;
    auto* region = instance->getRegion();
    if (!hit || hit->hitType != SDK::HitType::BLOCK || !region || hit->face < 0 || hit->face > 5) return;
    static constexpr auto offsets = std::to_array<BlockPos>({
        {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0},
    });
    const auto& offset = offsets[static_cast<std::size_t>(hit->face)];
    const BlockPos target{hit->hitBlock.x + offset.x, hit->hitBlock.y + offset.y, hit->hitBlock.z + offset.z};
    auto* targetBlock = region->getBlock(target);
    auto* targetLegacy = targetBlock ? targetBlock->legacyBlock : nullptr;
    if (!targetLegacy || targetLegacy->namespacedId.getString() != "minecraft:air") return;
    pendingTowerPosition_ = soundify::SoundPosition{static_cast<float>(target.x), static_cast<float>(target.y),
                                                    static_cast<float>(target.z)};
    pendingTowerTime_ = now;
}
void SoundifyModule::confirmCompactTower(soundify::SoundEngine::TimePoint now) {
    if (!pendingTowerPosition_ || !pendingTowerTime_) return;
    if (now - *pendingTowerTime_ > std::chrono::milliseconds(750)) {
        pendingTowerPosition_.reset();
        pendingTowerTime_.reset();
        return;
    }
    auto* instance = SDK::ClientInstance::get();
    auto* region = instance ? instance->getRegion() : nullptr;
    if (!region) return;
    const auto& position = *pendingTowerPosition_;
    auto* block = region->getBlock({static_cast<int>(position.x), static_cast<int>(position.y),
                                    static_cast<int>(position.z)});
    auto* legacy = block ? block->legacyBlock : nullptr;
    if (!legacy) return;
    const auto id = legacy->namespacedId.getString();
    if (id != "minecraft:chest" && id != "minecraft:trapped_chest") return;
    client_.onCompactTowerPlaced(now);
    pendingTowerPosition_.reset();
    pendingTowerTime_.reset();
}
bool SoundifyModule::hasMatchingLeatherArmor() const {
    auto* instance = SDK::ClientInstance::get();
    auto* player = instance ? instance->getLocalPlayer() : nullptr;
    if (!player) return false;
    auto* helmet = player->getArmor(0);
    auto* chest = player->getArmor(1);
    auto* helmetItem = helmet ? helmet->getItem() : nullptr;
    auto* chestItem = chest ? chest->getItem() : nullptr;
    if (!helmetItem || !chestItem || helmetItem->namespacedId.getString() != "minecraft:leather_helmet" ||
        chestItem->namespacedId.getString() != "minecraft:leather_chestplate") return false;
    const auto helmetColor = leatherColor(helmet);
    const auto chestColor = leatherColor(chest);
    return helmetColor && chestColor && *helmetColor == *chestColor;
}
void SoundifyModule::updateBedWarsWorld(soundify::SoundEngine::TimePoint now) {
    if (!std::get<BoolValue>(bedwarsSounds_) || !bedWarsScoreboard_) {
        observedBedWarsTnt_.clear();
        trackedBedPosition_.reset();
        trackedBedTime_.reset();
        return;
    }
    auto* instance = SDK::ClientInstance::get();
    auto* player = instance ? instance->getLocalPlayer() : nullptr;
    auto* level = instance && instance->minecraft ? instance->minecraft->getLevel() : nullptr;
    auto* region = instance ? instance->getRegion() : nullptr;
    if (!player || !level || !region) return;
    const bool matchingArmor = hasMatchingLeatherArmor();
    const auto& playerPosition = player->getPos();
    std::unordered_set<std::uint64_t> liveTnt;
    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor) continue;
        const auto type = actor->getEntityTypeName();
        if (type != "minecraft:tnt" && type != "tnt") continue;
        const auto runtimeId = actor->getRuntimeID();
        liveTnt.insert(runtimeId);
        if (!observedBedWarsTnt_.insert(runtimeId).second) continue;
        if (!matchingArmor) continue;
        const auto& position = actor->getPos();
        const auto dx = position.x - playerPosition.x;
        const auto dy = position.y - playerPosition.y;
        const auto dz = position.z - playerPosition.z;
        client_.onBedWarsTntSpawn({position.x, position.y, position.z}, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    std::erase_if(observedBedWarsTnt_, [&liveTnt](std::uint64_t id) { return !liveTnt.contains(id); });

    if (trackedBedPosition_ && trackedBedTime_) {
        const auto& tracked = *trackedBedPosition_;
        const BlockPos blockPos{static_cast<int>(tracked.x), static_cast<int>(tracked.y), static_cast<int>(tracked.z)};
        if (!isBedBlock(region->getBlock(blockPos))) {
            if (matchingArmor)
                client_.onBedWarsBedBroken({playerPosition.x, playerPosition.y, playerPosition.z}, now);
            trackedBedPosition_.reset();
            trackedBedTime_.reset();
        } else if (now - *trackedBedTime_ > std::chrono::seconds(2)) {
            trackedBedPosition_.reset();
            trackedBedTime_.reset();
        }
    }
    auto* hit = level->getHitResult();
    if (hit && hit->hitType == SDK::HitType::BLOCK && isBedBlock(region->getBlock(hit->hitBlock))) {
        trackedBedPosition_ = soundify::SoundPosition{static_cast<float>(hit->hitBlock.x),
                                                      static_cast<float>(hit->hitBlock.y),
                                                      static_cast<float>(hit->hitBlock.z)};
        trackedBedTime_ = now;
    }
}
void SoundifyModule::reset() {
    client_.onWorldChanged(); explosionRouter_.onWorldChanged(); level_ = nullptr;
    player_ = nullptr; dimension_ = nullptr; slot_ = -1; logicalScreen_.clear(); containerOpen_ = false;
    recentMaceAttack_.reset();
    textInput_.reset(); inputMailbox_.clear();
    pendingContainerScreen_.clear(); pendingContainerTime_.reset();
    pendingTowerPosition_.reset(); pendingTowerTime_.reset();
    observedBedWarsTnt_.clear(); trackedBedPosition_.reset(); trackedBedTime_.reset();
    bedWarsScoreboard_ = false;
}
void SoundifyModule::onDisable() { std::lock_guard lock(mutex_); reset(); }
void SoundifyModule::onLeave(Event&) { std::lock_guard lock(mutex_); reset(); }
