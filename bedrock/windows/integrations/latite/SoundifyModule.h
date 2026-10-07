#pragma once
#include "client/feature/module/Module.h"
#include "soundify/client/SoundifyClient.hpp"
#include "soundify/core/ExplosionRouter.hpp"
#include "soundify/core/TextInputRouter.hpp"
#include "soundify/core/InputMailbox.hpp"
#include "soundify/security/AuthClient.hpp"
#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

// Experimental native integration for the pinned Minecraft for Windows build.
class SoundifyModule final : public Module, private soundify::ClientAdapter {
public:
    SoundifyModule();
    ~SoundifyModule() override;
    void onEnable() override;
    void onDisable() override;
private:
    std::string_view gameBuild() const override;
    bool supported() const override;
    void play(const soundify::SoundCommand& command) override;
    void onAttack(Event& event);
    void onPacket(Event& event);
    void onClick(Event& event);
    void onMove(Event& event);
    void onCharacter(Event& event);
    void onKey(Event& event);
    void processInput(soundify::SoundEngine::TimePoint now);
    void reportSlowOperation(std::string_view name, soundify::SoundEngine::TimePoint started);
    void onText(Event& event);
    void onRenderLayer(Event& event);
    void onTick(Event& event);
    void onLeave(Event& event);
    std::string heldItem() const;
    soundify::ItemUseTraits heldItemTraits() const;
    std::vector<soundify::InventorySlotState> inventorySnapshot() const;
    void observeInventory(soundify::SoundEngine::TimePoint now);
    void updateHotbar(soundify::SoundEngine::TimePoint now);
    void observeTrackedActors(soundify::ExplosionRouter::TimePoint now);
    void observeRespawnAnchorUse(soundify::ExplosionRouter::TimePoint now);
    void observeCompactTowerUse(soundify::SoundEngine::TimePoint now);
    void confirmCompactTower(soundify::SoundEngine::TimePoint now);
    void updateBedWarsWorld(soundify::SoundEngine::TimePoint now);
    bool hasMatchingLeatherArmor() const;
    void startAuthCheck(bool refresh = false);
    void updateAuthentication();
    void launchLoginApp();
    void reset();

    soundify::SoundifyClient client_{*this};
    soundify::ExplosionRouter explosionRouter_;
    static soundify::security::AuthOptions authOptions();
    soundify::security::ProtectedTokenStore tokenStore_{soundify::security::defaultTokenPath()};
    soundify::security::WinHttpTransport authTransport_;
    // No identity provider: the game process uses the device id the launcher saved.
    soundify::security::AuthClient authClient_{tokenStore_, authTransport_, {}, authOptions()};
    std::future<soundify::security::AuthResult> authFuture_;
    std::chrono::steady_clock::time_point nextAuthCheck_{};
    std::chrono::steady_clock::time_point nextTokenPoll_{};
    std::chrono::steady_clock::time_point authValidUntil_{};
    bool authenticated_{};
    bool authCheckWasRefresh_{};
    bool loginLaunched_{};
    bool tokenObserved_{};
    std::recursive_mutex mutex_;
    const void* level_{};
    const void* player_{};
    const void* dimension_{};
    int slot_{-1};
    std::string logicalScreen_;
    bool containerOpen_{};
    soundify::TextInputRouter textInput_;
    soundify::InputMailbox inputMailbox_;
    soundify::SoundEngine::TimePoint nextSlowLog_{};
    soundify::SoundEngine::TimePoint nextGlassLog_{};
    std::string pendingContainerScreen_;
    std::optional<soundify::SoundEngine::TimePoint> pendingContainerTime_;
    std::optional<soundify::SoundEngine::TimePoint> recentMaceAttack_;
    std::optional<soundify::SoundPosition> pendingTowerPosition_;
    std::optional<soundify::SoundEngine::TimePoint> pendingTowerTime_;
    std::unordered_set<std::uint64_t> observedBedWarsTnt_;
    std::optional<soundify::SoundPosition> trackedBedPosition_;
    std::optional<soundify::SoundEngine::TimePoint> trackedBedTime_;
    bool bedWarsScoreboard_{};
    ValueType combatApproximation_ = BoolValue(false);
    ValueType actionSounds_ = BoolValue(true);
    ValueType specialExplosionSounds_ = BoolValue(true);
    ValueType masterVolume_ = FloatValue(100.f);
    ValueType attackVolume_ = FloatValue(60.f);
    ValueType itemVolume_ = FloatValue(100.f);
    ValueType inventorySounds_ = BoolValue(true);
    ValueType movementSounds_ = BoolValue(true);
    ValueType movementVolume_ = FloatValue(100.f);
    ValueType playerStatusSounds_ = BoolValue(true);
    ValueType statusVolume_ = FloatValue(100.f);
    ValueType effectSounds_ = BoolValue(true);
    ValueType effectVolume_ = FloatValue(100.f);
    ValueType experienceSounds_ = BoolValue(true);
    ValueType interfaceSounds_ = BoolValue(true);
    ValueType interfaceVolume_ = FloatValue(100.f);
    ValueType bedwarsSounds_ = BoolValue(true);
    ValueType bedwarsVolume_ = FloatValue(100.f);
    ValueType heartbeatSound_ = BoolValue(true);
};
