#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace soundify {

// GameCore can deliver only key messages; other builds also deliver WM_CHAR.
// Combine both streams without playing a key and its following character twice.
class TextInputRouter final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    bool nativeAvailable() const { return nativeAvailable_; }
    bool fallbackUsed() const { return fallbackAt_.has_value(); }

    bool fallbackKey(bool printable, bool shortcut, bool textScreen, TimePoint now) {
        // GameCore may alternate key-only and WM_CHAR input (e.g. IME/focus).
        // Seeing one WM_CHAR must not permanently silence the key-only path.
        if (!printable || shortcut || !textScreen) return false;
        fallbackAt_ = now;
        return true;
    }

    bool nativeCharacter(std::uint32_t value, TimePoint now) {
        if (value < 0x20 || value == 0x7f || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdbff)) return false;
        nativeAvailable_ = true;
        if (fallbackAt_ && now >= *fallbackAt_ && now - *fallbackAt_ < std::chrono::milliseconds(100)) {
            fallbackAt_.reset();
            return false;
        }
        fallbackAt_.reset();
        return true;
    }

    void reset() { nativeAvailable_ = false; fallbackAt_.reset(); }

private:
    bool nativeAvailable_{};
    std::optional<TimePoint> fallbackAt_;
};

} // namespace soundify
