#include "font.h"
#include "icons.h"
#include "../dependencies/fonts/inter_regular.h"
#include "../dependencies/fonts/inter_semibold.h"
#include "../dependencies/fonts/fa_solid.h"

namespace Font {

static ImFont* s_regular = nullptr;
static ImFont* s_semibold = nullptr;
static ImFont* s_small = nullptr;
static ImFont* s_heading = nullptr;

ImFont* Regular()  { return s_regular; }
ImFont* SemiBold() { return s_semibold; }
ImFont* Small()    { return s_small; }
ImFont* Heading()  { return s_heading; }

void Load(float contentScale) {
    const float s = contentScale > 0.0f ? contentScale : 1.0f;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Latin + Latin-1 + Latin Extended-A (Turkish etc.), dashes, bullets, arrows
    static const ImWchar textRanges[] = { 0x0020, 0x007E, 0x00A0, 0x017F, 0x2010, 0x2027, 0x2030, 0x203A,
                                          0x2190, 0x2193, 0x2212, 0x2212, 0 };
    static const ImWchar iconRanges[] = { ICON_MIN, ICON_MAX, 0 };

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;      // data is a static array
    cfg.OversampleH = s > 1.5f ? 1 : 2;    // native-resolution Retina text needs no oversampling
    cfg.OversampleV = 1;

    auto add = [&](const unsigned char* data, int size, float px) {
        ImFont* f = io.Fonts->AddFontFromMemoryTTF((void*)data, size, px * s, &cfg, textRanges);
        ImFontConfig icons = cfg;
        icons.MergeMode = true;
        icons.PixelSnapH = true;
        icons.GlyphMinAdvanceX = px * s * 1.2f; // fixed-width icons line up in lists
        icons.GlyphOffset = ImVec2(0, px * s * 0.02f);
        io.Fonts->AddFontFromMemoryTTF((void*)kFaSolid, kFaSolid_size, px * s * 0.86f, &icons, iconRanges);
        return f;
    };
    s_regular  = add(kInterRegular, kInterRegular_size, 15.0f);
    s_semibold = add(kInterSemiBold, kInterSemiBold_size, 15.0f);
    s_small    = add(kInterRegular, kInterRegular_size, 12.5f);
    s_heading  = add(kInterSemiBold, kInterSemiBold_size, 17.0f);
    io.FontDefault = s_regular;

#ifdef __APPLE__
    // macOS works in points: rasterize at pixel size, draw at point size.
    io.FontGlobalScale = 1.0f / s;
#else
    // Windows/Linux work in pixels: scale the whole UI up instead.
    ImGui::GetStyle().ScaleAllSizes(s);
#endif
}

void embraceTheDarkness() {
    using namespace Theme;
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    c[ImGuiCol_Text]                  = Text;
    c[ImGuiCol_TextDisabled]          = TextMuted;
    c[ImGuiCol_WindowBg]              = Panel;
    c[ImGuiCol_ChildBg]               = Alpha(Panel, 0.0f);
    c[ImGuiCol_PopupBg]               = Hex(0x16191E, 0.98f);
    c[ImGuiCol_Border]                = Border;
    c[ImGuiCol_BorderShadow]          = Alpha(Border, 0.0f);

    c[ImGuiCol_FrameBg]               = Surface;
    c[ImGuiCol_FrameBgHovered]        = SurfaceHover;
    c[ImGuiCol_FrameBgActive]         = SurfaceActive;

    c[ImGuiCol_TitleBg]               = Chrome;
    c[ImGuiCol_TitleBgActive]         = Chrome;
    c[ImGuiCol_TitleBgCollapsed]      = Chrome;
    c[ImGuiCol_MenuBarBg]             = Chrome;

    c[ImGuiCol_ScrollbarBg]           = Alpha(Panel, 0.0f);
    c[ImGuiCol_ScrollbarGrab]         = SurfaceActive;
    c[ImGuiCol_ScrollbarGrabHovered]  = BorderStrong;
    c[ImGuiCol_ScrollbarGrabActive]   = TextFaint;

    c[ImGuiCol_CheckMark]             = Accent;
    c[ImGuiCol_SliderGrab]            = Alpha(Accent, 0.85f);
    c[ImGuiCol_SliderGrabActive]      = AccentHover;

    c[ImGuiCol_Button]                = Surface;
    c[ImGuiCol_ButtonHovered]         = SurfaceHover;
    c[ImGuiCol_ButtonActive]          = SurfaceActive;

    c[ImGuiCol_Header]                = Surface;      // collapsing headers, selected list items
    c[ImGuiCol_HeaderHovered]         = SurfaceHover;
    c[ImGuiCol_HeaderActive]          = SurfaceActive;

    c[ImGuiCol_Separator]             = Border;
    c[ImGuiCol_SeparatorHovered]      = Alpha(Accent, 0.6f);
    c[ImGuiCol_SeparatorActive]       = Accent;

    c[ImGuiCol_ResizeGrip]            = Alpha(Panel, 0.0f);
    c[ImGuiCol_ResizeGripHovered]     = Alpha(Accent, 0.5f);
    c[ImGuiCol_ResizeGripActive]      = Accent;

    // Docked window tabs: the active one blends into its panel
    c[ImGuiCol_Tab]                   = Chrome;
    c[ImGuiCol_TabHovered]            = SurfaceHover;
    c[ImGuiCol_TabActive]             = Panel;
    c[ImGuiCol_TabUnfocused]          = Chrome;
    c[ImGuiCol_TabUnfocusedActive]    = Panel;

    c[ImGuiCol_DockingPreview]        = Alpha(Accent, 0.35f);
    c[ImGuiCol_DockingEmptyBg]        = Chrome;

    c[ImGuiCol_PlotLines]             = TextMuted;
    c[ImGuiCol_PlotLinesHovered]      = Accent;
    c[ImGuiCol_PlotHistogram]         = Accent;
    c[ImGuiCol_PlotHistogramHovered]  = AccentHover;

    c[ImGuiCol_TableHeaderBg]         = Surface;
    c[ImGuiCol_TableBorderStrong]     = Border;
    c[ImGuiCol_TableBorderLight]      = Alpha(Border, 0.6f);
    c[ImGuiCol_TableRowBg]            = Alpha(Panel, 0.0f);
    c[ImGuiCol_TableRowBgAlt]         = Hex(0xFFFFFF, 0.025f);

    c[ImGuiCol_TextSelectedBg]        = Alpha(Accent, 0.35f);
    c[ImGuiCol_DragDropTarget]        = Accent;
    c[ImGuiCol_NavHighlight]          = Accent;
    c[ImGuiCol_NavWindowingHighlight] = Alpha(Accent, 0.7f);
    c[ImGuiCol_NavWindowingDimBg]     = Hex(0x000000, 0.4f);
    c[ImGuiCol_ModalWindowDimBg]      = Hex(0x000000, 0.5f);

    style.WindowPadding            = ImVec2(14, 12);
    style.FramePadding             = ImVec2(10, 6);
    style.CellPadding              = ImVec2(8, 5);
    style.ItemSpacing              = ImVec2(8, 8);
    style.ItemInnerSpacing         = ImVec2(8, 6);
    style.IndentSpacing            = 14;
    style.ScrollbarSize            = 10;
    style.GrabMinSize              = 6;

    style.WindowBorderSize         = 0;
    style.ChildBorderSize          = 0;
    style.PopupBorderSize          = 1;
    style.FrameBorderSize          = 0;
    style.TabBorderSize            = 0;

    style.WindowRounding           = 0;   // docked panels sit edge to edge
    style.ChildRounding            = 8;
    style.FrameRounding            = 6;
    style.PopupRounding            = 8;
    style.ScrollbarRounding        = 6;
    style.GrabRounding             = 4;
    style.TabRounding              = 6;

    style.WindowTitleAlign         = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None; // no dock-menu triangle on the tabs
    style.SeparatorTextAlign       = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize  = 1;
    style.SeparatorTextPadding     = ImVec2(0, 4);
    style.ButtonTextAlign          = ImVec2(0.5f, 0.5f);
    style.LogSliderDeadzone        = 4;
}

} // namespace Font
