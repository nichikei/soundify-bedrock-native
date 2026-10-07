#pragma once
#include <string_view>

namespace soundify {
// ScreenView callbacks include HUD controls and loading/notification overlays.
// Those layers must never become the menu used for sound or text routing.
inline bool isActiveScreenLayer(std::string_view root, bool cursorGrabbed) {
    if (cursorGrabbed) return root == "hud_screen";
    if (!root.ends_with("_screen") || root.starts_with("hud_") ||
        root.find("debug") != root.npos || root.find("loading") != root.npos ||
        root.find("toast") != root.npos || root.find("overlay") != root.npos ||
        root.find("progress") != root.npos) return false;
    return true;
}
} // namespace soundify
