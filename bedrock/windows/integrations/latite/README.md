# Experimental Latite adapter

Target candidate: Minecraft for Windows 26.45, x64 package 1.26.4501.0.
Pinned upstream: https://github.com/LatiteClient/Latite at
2271ce919a64ae4c13d208a3c4c70f083801a212. Upstream accepts 1.26.4x;
its v2.9.1 release explicitly announces 26.44. This does not verify 26.45.

This is native C++ integration, not a JavaScript plugin. In this upstream revision,
PluginManager::init comments out its scripting object registrations.

The module registers attack, input, movement, text, title, screen, actor-event, mob-effect and level-sound packets, tick and
leave callbacks. It covers hotbar selection, weapon swings, common item use, sneak/run and
armor movement, low-health heartbeat, interface typing/open/close and common English
and Vietnamese BedWars chat/title/actionbar messages. Vietnamese uppercase accents are
case-folded explicitly. It also detects local death and respawn from health transitions,
strips Minecraft text formatting and suppresses duplicate
server messages for 750 ms. It emits one player-list sound per Tab-key press. Protocol-2169
explosion packets are rewritten before vanilla playback: Creeper, End Crystal and locally
used Respawn Anchor explosions select their Java Soundify event, while TNT remains generic.
Actor identifiers are preferred; priming/position/local-use correlations cover packet-order
cases. Playback is
sent directly to Latite's SoundPlayerInterface to avoid
an extra game-tick delay. Hotbar changes are observed on HUD render frames with tick polling
as a fallback. Persisted 0-100% sliders control master, combat, item, movement, status,
interface, potion-effect and BedWars volumes; combat defaults to 60% and the others to 100%. A newly
applied local potion effect plays once; updates, join restoration, beacon refreshes and long
command effects are filtered. Active Jump Boost adds its Java jump sound. State is
reset on world/player/dimension changes. Combat uses timing correlation, cannot prove the damage
source, and is disabled by default. Critical sounds are not generated without evidence.
Version 0.3.10 flushes coalesced effects on the render cadence rather than the 20 Hz tick and
observes wheel-selected hotbar slots in Latite's post-input callback.
Version 0.3.11 reads the local player's level from protocol-2169 attribute packets. It keeps a
silent join baseline and ports Java's level-up rule, including the divisible-by-five exception.
Version 0.3.12 reads large-fireball spawns directly from protocol-2169 `AddActorPacket` and plays
the Java event at the packet position with its original distance-volume curve and no tick polling.
Version 0.3.13 correlates snowball/egg unique and runtime IDs across add/remove packets, updates
their positions from live actors and ports Java's guarded remove-impact fallback within 64 blocks.
Version 0.3.14 adds the Windows login companion and gates playback on the existing GDuck Store
`/auth` and `/verify` service. A stable CNG ECDSA P-256 installation identity replaces mutable
hardware fingerprints; the verified token is protected with per-user Windows DPAPI. The DLL
rechecks every 15 minutes and allows at most 30 minutes of in-memory grace after a refresh
network failure. It never saves the account password.
For an existing Java customer, authentication reuses the Java mod's validated 64-character
HWID cache when present, while DPAPI session storage remains bound to the new installation key.
Version 0.3.15 adds protocol-2169 shield cooldown and mace-smash handling, local fireball/wind-charge
deflect sounds with per-projectile deduplication, and both Java lobby hotbar-pattern transitions.
Pack 0.3.4 adds the missing deflect and second-lobby events and their original Java assets.
Version 0.3.16 detects Power, Flame and Piercing from held-item NBT, confirms placement of the
named Compact Pop-up Tower chest, and ports scoreboard-gated BedWars TNT timer and targeted-bed
break sounds with the Java leather-armor and distance guards.
Version 0.3.17 correlates the protocol-2169 `Animate/CriticalHit` target runtime ID with the
local attack, preserves the critical layer across either packet order and suppresses duplicates.
Version 0.3.18 enables the Soundify module for a new Latite profile and ships with the unified
launcher that authenticates, installs/activates the pack, starts Minecraft and loads this DLL.
Version 0.3.19 tracks splash and lingering potion actors through protocol-2169 add/remove packets
and plays the Java bottle-break event at the last observed impact position. Pack 0.3.6 adds its
three original Java samples; the pack now ports 69 Java events through 72 native overrides plus
one DLL-triggered event and 170 prepared OGG assets.
Version 0.3.20 matches Java text-entry playback at full volume, random pitch 0.8-1.1 and an
8 ms gate, while filtering Latite control-character events. Pack 0.3.7 corrects custom block
break/place pitch to 0.8, mining-hit pitch to 0.5, metal-break pitch to 0.7-1.1 and expands
coverage from 92 blocks/20 groups to 111 blocks/27 groups.

The integrated Release DLL compiled and linked successfully with MSVC 19.43 on 2026-09-14.
It is experimental; injection, pack activation, hotbar/action, inventory and explosion behavior
have been user-verified in game. The 0.3.9 potion/effect path still needs its focused check.
Exact crafting-result transactions, GUI hover semantics, sweep ownership,
scoreboard-entry-only BedWars detection and support for game versions other than 26.45 remain unavailable.

## Build

Follow the pinned upstream's toolchain instructions (MSVC, Windows SDK, MinGW ld.exe,
clang-format and CMake). The integration runs at the end of upstream configuration.
It generates an alternate ModuleManager.cpp in the build directory without editing
the upstream checkout. Do not configure this include with the standalone Soundify project.
The integration also generates a Keystrokes.cpp compatibility copy adding the explicit
empty lambda parameter list required by MSVC 19.43. Upstream source files remain untouched.

The `bedrock/tools/build_native.ps1` helper checks the pinned commit and MinGW linker,
then configures and builds. Example:

```powershell
powershell -NoProfile -File bedrock/tools/build_native.ps1 -LatiteSource PATH_TO_PINNED_LATITE -MingwBin PATH_TO_MINGW_BIN
```

```powershell
$integration = (Resolve-Path bedrock/windows/integrations/latite/integrate.cmake).Path
cmake -S PATH_TO_PINNED_LATITE -B bedrock/windows/out/latite -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PROJECT_Latite_INCLUDE=$integration"
cmake --build bedrock/windows/out/latite --config Release
```

The resulting combined client contains Latite code and is subject to its GPL-3.0
license; keep its license and corresponding source when distributing. This integration
does not turn Latite into a standalone proprietary Soundify DLL.

## Required in-game checks before release

- Verify module loading and cleanup on the exact 26.45 binary.
- Activate Soundify resource pack; test UI sound volume and server-pack priority.
- Switch hotbar in a local world and server, including empty slots and reconnects.
- Verify action, movement, heartbeat, screen, typing and BedWars switches independently.
- Apply Speed and Jump Boost, verify one apply sound each, repeated Jump Boost jumps, silent
  duration updates and no beacon refresh spam.
- Increase XP levels through 4, 5 and 6; verify level 5 is silent and losing levels is silent.
- Let a Ghast shoot a large fireball; verify one positional spawn sound and quieter playback at distance.
- Throw snowballs, eggs, splash potions and lingering potions at blocks; verify one positional
  impact each and no distant/untracked sound. Potions must use the Java bottle-break samples.
- Deflect a Ghast fireball and a wind charge; verify one positional sound per projectile. Disable a shield
  with an axe and perform a real falling mace smash; verify one custom sound for each confirmed event.
- Reproduce both Java lobby hotbar patterns and verify one join sound per pattern transition without looping.
- Compare normal/Power/Flame bows and a Piercing crossbow. Place the exact named Compact Pop-up Tower
  chest and verify rejected placement is silent. In a detected BedWars match, test nearby/far TNT and
  a targeted bed break while wearing matching dyed-leather helmet and chestplate.
- Verify every category volume at 0%, 50% and 100%, and verify that master volume affects
  all Soundify events without changing vanilla Minecraft audio.
- Die and respawn once; test the same BedWars message in chat and title/actionbar and confirm
  that each logical event plays once.
- Verify the dedicated victory, defeat, sudden-death, spectating, respawn, bed-destroyed,
  AFK, death and fight title rules in English and Vietnamese.
- Hold and release Tab twice; confirm one player-list sound per press and no held-key repeat.
- Verify the 0.3.4 pack's block mappings do not change textures and that break/place/step
  sounds work for every mapped material family.
- Verify TNT uses the Java generic explosion samples while Creeper, End Crystal and locally
  used Respawn Anchor select `creeper_boom`, `crystal_boom` and `anchor_boom` once each.
- Verify the level-sound callback changes only matched explosion event identifiers and does
  not cancel packets, change positions or affect unrelated sound events.
- With combat enabled explicitly, test miss, hurt from another player, duplicate hurt,
  death, entity unload and disconnect. Document approximation; do not claim kill ownership.
- Test first login, restart/session restore, logout, revoked account, wrong password and offline startup.

Version 0.3.21 removes the inventory overlay from chest/pause/unknown menus,
routes workstations with Java-equivalent native events and randomized open pitch,
and uses clicked block context for shared screen roots. XP bottles (`minecraft:xp_bottle`)
join the add/move/remove projectile path, including impacts below 100 ms. Pack 0.3.8 aliases
`random.enderchestopen` to Java's custom samples. An independent WM_CHAR notification
and a printable-key fallback for chat/sign/book replace Latite's synthetic character polling.
No typed characters or chat contents are logged. In-game validation remains required.


### 0.3.22 regression investigation

Live 0.3.21 logs on package 1.26.4501.0 showed `hud_crosshair_screen` latched as the
menu despite the correct DLL and active pack 0.3.8. Exclude all `hud_*` controls and
loading/notification overlays before acquiring the module lock. Windows character/key
callbacks now only copy input into a bounded mailbox; process on the active render layer
with tick fallback, expire input older than 100 ms, and clear pending input on reset.
A WM_CHAR observation no longer permanently disables key fallback. Native XP bottle
impacts replace the glass LevelSoundEvent (166 / `glass`) only for xp_bottle identifiers;
XP AddActor/RemoveActor synthesis is removed to avoid double playback. This follows the
installed vanilla xp_bottle.json `hit_sound: glass` and Mojang protocol 2169 enum.

Slow audio/callback operations are logged above 12 ms with a 10-second rate limit. These
measure callback/input processing, not acoustic latency. User reported delayed vanilla
sound too; the actual global audio regression is not yet verified resolved in game.
Resource pack stays 0.3.8; no audio file encoding, sample rate or playback speed is changed.
See SELF_TEST.md for same-world comparisons with Soundify disabled and without the DLL.

### 0.3.23 bottle impact routing

Splash and lingering potions also declare hit_sound=glass in the installed vanilla
entity components. Pack 0.3.9 maps glass for splash_potion, lingering_potion and xp_bottle
only, so local native playback does not require AddActor/RemoveActor callbacks. The DLL
can replace confirmed bottle glass packets without adding another sound; potion tracking
through RemoveActor is removed. Native pack overrides, including bottles, are outside
module category toggles/volumes. Existing block mappings and audio files are unchanged.

User verified Ender Chest in 0.3.22, then no delay but no hotbar in a fresh game process
with no DLL loaded. This validates why hotbar was absent; it does not establish whether
Bluetooth or the DLL caused previous latency. Repeat with the DLL on the same output.
