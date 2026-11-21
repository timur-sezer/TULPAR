#pragma once

#include "../dependencies/imgui/imgui.h"

// ------------------------------------------------------------------
// Design tokens. Every custom-drawn element uses these so the whole
// app stays consistent with the ImGui widget theme.
// ------------------------------------------------------------------
namespace Theme {
    constexpr ImVec4 Hex(unsigned rgb, float a = 1.0f) {
        return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
    }
    // Surfaces, darkest to lightest
    constexpr ImVec4 Chrome        = Hex(0x0A0B0E); // header + status bar
    constexpr ImVec4 Canvas        = Hex(0x0D0F12); // video wells, node canvas
    constexpr ImVec4 Panel         = Hex(0x121418); // docked windows
    constexpr ImVec4 Surface       = Hex(0x1A1D23); // inputs, cards, nodes
    constexpr ImVec4 SurfaceHover  = Hex(0x22262E);
    constexpr ImVec4 SurfaceActive = Hex(0x2B3039);
    constexpr ImVec4 Border        = Hex(0x262A32);
    constexpr ImVec4 BorderStrong  = Hex(0x343944);
    // Text
    constexpr ImVec4 Text          = Hex(0xE7E9EE);
    constexpr ImVec4 TextMuted     = Hex(0x8C93A0);
    constexpr ImVec4 TextFaint     = Hex(0x5A616D);
    // Accent + status
    constexpr ImVec4 Accent        = Hex(0x5B8CFF);
    constexpr ImVec4 AccentHover   = Hex(0x7BA3FF);
    constexpr ImVec4 Success       = Hex(0x3DD68C);
    constexpr ImVec4 Warning       = Hex(0xF5A524);
    constexpr ImVec4 Danger        = Hex(0xF2555A);

    inline ImVec4 Alpha(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }
    inline ImU32  U32(ImVec4 c)            { return ImGui::ColorConvertFloat4ToU32(c); }
    inline ImU32  U32(ImVec4 c, float a)   { return ImGui::ColorConvertFloat4ToU32(Alpha(c, a)); }
}

namespace Font {
    // Applies the TULPAR dark theme (colors + style metrics) to the current ImGui context.
    void embraceTheDarkness();

    // Loads the embedded Inter + icon fonts. `contentScale` is the monitor's DPI
    // scale (2 on Retina) so text is rasterized at native resolution.
    void Load(float contentScale);

    ImFont* Regular();   // 15 px, body text (default font)
    ImFont* SemiBold();  // 15 px, titles and emphasis
    ImFont* Small();     // 12.5 px, captions, status bar, node details
    ImFont* Heading();   // 17 px semibold, brand
}
