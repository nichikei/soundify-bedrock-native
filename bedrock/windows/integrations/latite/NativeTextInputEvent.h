#pragma once
#include "client/event/Event.h"
#include "util/Crypto.h"
#include <cstdint>

class NativeTextInputEvent final : public Event {
public:
    static constexpr std::uint32_t hash = TOHASH(NativeTextInputEvent);
    explicit NativeTextInputEvent(std::uint32_t value) : codePoint(value) {}
    const std::uint32_t codePoint;
};
