#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace soundify {
struct NativeInput {
    enum class Kind { Key, Character };
    Kind kind;
    std::uint32_t value;
    bool pressed{};
    bool inUI{};
    bool shortcut{};
    std::chrono::steady_clock::time_point receivedAt;
};

// Window callbacks only copy input here. They never wait for the module/game
// lock or call the native audio engine. Drain on the game/render cadence.
class InputMailbox final {
public:
    static constexpr std::size_t Capacity = 128;
    static constexpr auto MaxAge = std::chrono::milliseconds(100);
    void push(NativeInput input) {
        std::lock_guard lock(mutex_);
        if (inputs_.size() == Capacity) inputs_.pop_front();
        inputs_.push_back(input);
    }
    std::vector<NativeInput> drain(std::chrono::steady_clock::time_point now) {
        std::deque<NativeInput> pending;
        { std::lock_guard lock(mutex_); pending.swap(inputs_); }
        std::vector<NativeInput> result;
        result.reserve(pending.size());
        for (const auto& input : pending) {
            if (now >= input.receivedAt && now - input.receivedAt <= MaxAge) result.push_back(input);
        }
        return result;
    }
    void clear() { std::lock_guard lock(mutex_); inputs_.clear(); }
private:
    std::mutex mutex_;
    std::deque<NativeInput> inputs_;
};
} // namespace soundify
