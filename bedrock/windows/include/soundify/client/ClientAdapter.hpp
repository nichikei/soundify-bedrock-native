#pragma once

#include "soundify/core/SoundEngine.hpp"

#include <functional>
#include <string_view>

namespace soundify {

class ClientAdapter {
public:
    virtual ~ClientAdapter() = default;
    virtual std::string_view gameBuild() const = 0;
    virtual bool supported() const = 0;
    virtual void play(const SoundCommand& command) = 0;
};

// A build-specific adapter will translate verified Minecraft client callbacks
// into SoundEngine calls. Keeping this boundary explicit prevents stale binary
// layouts from being used after a Bedrock update.

} // namespace soundify
