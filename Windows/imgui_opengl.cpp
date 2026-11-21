#define IMGUI_DEFINE_MATH_OPERATORS // ImVec2 + - * operators
#include "imgui_opengl.h"
#include "../dependencies/imgui/imgui.h"
#include "../dependencies/imgui/imgui_internal.h" // DockBuilder API
#include "../dependencies/imgui/imgui_impl_glfw.h"
#include "../dependencies/imgui/imgui_impl_opengl3.h"
#include "font.h"
#include "icons.h"
#include "node_editor.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace Theme;

// Window names: the part after ### is the stable ID, the rest is the visible title.
// (A window can only live in one dockspace, so each page has its own windows.)
static const char* kWinProcessing = ICON_SLIDERS "  Processing###Processing";
static const char* kWinInput      = ICON_VIDEO "  Input###DesignInput";
static const char* kWinOutput     = ICON_IMAGE "  Output###DesignOutput";
static const char* kWinPipeInput  = ICON_VIDEO "  Input###PipeInput";
static const char* kWinPipeOutput = ICON_DISPLAY "  Pipeline Output###PipeOutput";
static const char* kWinNodeEditor = ICON_DIAGRAM "  Node Editor###NodeEditor";
static const char* kWinPalette    = ICON_LIST "  Nodes###NodePalette";

static NodeGraph g_graph;

enum Page { PAGE_DESIGN = 0, PAGE_PIPELINE };

void InitImGui(GLFWwindow* window, const char* glsl_version) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr; // always start from the default docked layout

    ImGui::StyleColorsDark();
    Font::embraceTheDarkness();
    float xs = 1.0f, ys = 1.0f;
    glfwGetWindowContentScale(window, &xs, &ys);
    Font::Load(xs);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
}

// ------------------------------------------------------------------
// Layouts
// ------------------------------------------------------------------

// Design page:
// +-------------+-----------------------+-----------------------+
// | Processing  |         Input         |        Output         |
// +-------------+-----------------------+-----------------------+
static void BuildDesignLayout(ImGuiID dockspaceId, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, size);

    ImGuiID left, right, input, output;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.30f, &left, &right);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Left, 0.5f, &input, &output);

    ImGui::DockBuilderDockWindow(kWinProcessing, left);
    ImGui::DockBuilderDockWindow(kWinInput, input);
    ImGui::DockBuilderDockWindow(kWinOutput, output);
    ImGui::DockBuilderFinish(dockspaceId);
}

// Pipeline page:
// +--------+--------------------------------------------+
// | Nodes  |               Node Editor                  |
// |        +-----------------+--------------------------+
// |        |      Input      |     Pipeline Output      |
// +--------+-----------------+--------------------------+
static void BuildPipelineLayout(ImGuiID dockspaceId, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, size);

    ImGuiID palette, right, editor, feeds, input, output;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.17f, &palette, &right);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.56f, &editor, &feeds);
    ImGui::DockBuilderSplitNode(feeds, ImGuiDir_Left, 0.42f, &input, &output);

    ImGui::DockBuilderDockWindow(kWinPalette, palette);
    ImGui::DockBuilderDockWindow(kWinNodeEditor, editor);
    ImGui::DockBuilderDockWindow(kWinPipeInput, input);
    ImGui::DockBuilderDockWindow(kWinPipeOutput, output);
    ImGui::DockBuilderFinish(dockspaceId);
}

// ------------------------------------------------------------------
// Small drawing helpers
// ------------------------------------------------------------------

// UI unit: 1 at the default 15 px font, so custom geometry follows the font size.
static float U() { return ImGui::GetFontSize() / 15.0f; }

static ImVec2 TextSize(ImFont* font, const char* text) {
    ImGui::PushFont(font);
    ImVec2 s = ImGui::CalcTextSize(text);
    ImGui::PopFont();
    return s;
}

static void DrawText(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 col, const char* text) {
    ImGui::PushFont(font);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, col, text);
    ImGui::PopFont();
}

// Square icon button drawn in the header. `on` highlights it as an active toggle.
static bool IconButton(const char* id, const char* icon, const char* tooltip, ImVec2 pos, float size, bool on = false) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    bool hovered = ImGui::IsItemHovered();
    ImVec4 bg = on ? Alpha(Accent, hovered ? 0.28f : 0.18f) : (hovered ? SurfaceHover : Surface);
    dl->AddRectFilled(pos, pos + ImVec2(size, size), U32(bg), 7 * U());
    ImVec2 ts = ImGui::CalcTextSize(icon);
    dl->AddText(pos + (ImVec2(size, size) - ts) * 0.5f, U32(on ? AccentHover : (hovered ? Text : TextMuted)), icon);
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

// Video well: the frame is fitted, rounded and labelled; an empty state otherwise.
static void DrawFeed(GLuint texture, int w, int h, const char* label, const char* emptyIcon, const char* emptyText) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float u = U();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 8 || avail.y < 8) return;
    dl->AddRectFilled(p, p + avail, U32(Canvas), 8 * u);

    if (texture == 0 || w <= 0 || h <= 0) {
        ImGui::PushFont(Font::Heading());
        const float iconSize = ImGui::GetFontSize() * 2.0f;
        ImVec2 is = ImGui::GetFont()->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, emptyIcon);
        ImVec2 ts = TextSize(Font::Regular(), emptyText);
        float y = p.y + avail.y * 0.5f - (is.y + ts.y + 10 * u) * 0.5f;
        dl->AddText(ImGui::GetFont(), iconSize, ImVec2(p.x + (avail.x - is.x) * 0.5f, y), U32(TextFaint), emptyIcon);
        ImGui::PopFont();
        DrawText(dl, Font::Regular(), ImVec2(p.x + (avail.x - ts.x) * 0.5f, y + is.y + 10 * u), U32(TextMuted), emptyText);
        ImGui::Dummy(avail);
        return;
    }

    float scale = std::min(avail.x / w, avail.y / h);
    ImVec2 size(w * scale, h * scale);
    ImVec2 a = p + (avail - size) * 0.5f;
    dl->AddImageRounded((ImTextureID)(intptr_t)texture, a, a + size, ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32_WHITE, 6 * u);

    // Label chip in the top-left corner of the image
    char res[32];
    snprintf(res, sizeof(res), "%d \xC3\x97 %d", w, h); // "W × H"
    ImVec2 ls = TextSize(Font::SemiBold(), label), rs = TextSize(Font::Small(), res);
    ImVec2 c0 = a + ImVec2(10 * u, 10 * u);
    ImVec2 c1 = c0 + ImVec2(ls.x + rs.x + 26 * u, std::max(ls.y, rs.y) + 10 * u);
    dl->AddRectFilled(c0, c1, U32(Chrome, 0.78f), 6 * u);
    DrawText(dl, Font::SemiBold(), c0 + ImVec2(9 * u, 5 * u), U32(Text), label);
    DrawText(dl, Font::Small(), ImVec2(c0.x + ls.x + 17 * u, c0.y + 5 * u + (ls.y - rs.y) * 0.5f), U32(TextMuted), res);
    ImGui::Dummy(avail);
}

// Begin/End a docked window that shows a video well edge to edge.
static bool BeginFeedWindow(const char* name) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    bool open = ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    return open;
}

// Muted small-caps style section label.
static void SectionLabel(const char* text) {
    ImGui::PushFont(Font::Small());
    ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// Card: a rounded surface behind a group of widgets (background drawn after the
// content is measured, using a draw-list channel split). Not nestable.
static ImVec2 s_cardStart;
static float  s_cardWidth;
static void BeginCard() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pad = 12 * U();
    s_cardStart = ImGui::GetCursorScreenPos();
    s_cardWidth = ImGui::GetContentRegionAvail().x;
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(s_cardStart + ImVec2(pad, pad));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(s_cardStart.x + s_cardWidth - pad);
}
static void EndCard() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pad = 12 * U();
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImVec2 end(s_cardStart.x + s_cardWidth, ImGui::GetItemRectMax().y + pad);
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(s_cardStart, end, U32(Surface), 8 * U());
    dl->AddRect(s_cardStart, end, U32(Border), 8 * U());
    dl->ChannelsMerge();
    ImGui::SetCursorScreenPos(ImVec2(s_cardStart.x, end.y));
    ImGui::Dummy(ImVec2(s_cardWidth, 0));
}

// Small info marker with a tooltip.
static void Help(const char* text) {
    ImGui::SameLine();
    ImGui::PushFont(Font::Small());
    ImGui::TextColored(TextFaint, ICON_INFO);
    ImGui::PopFont();
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Slider for an odd kernel size stored as k where size = 2k+1.
static void OddKernelSlider(const char* label, int* k, int maxSize) {
    char fmt[32];
    snprintf(fmt, sizeof(fmt), "%d x %d", *k * 2 + 1, *k * 2 + 1);
    ImGui::SliderInt(label, k, 1, (maxSize - 1) / 2, fmt);
}

// HSV range controls + color presets (shared by Color Mask and Color Tracking).
static void HsvRangeEditor(EffectsSettings& s) {
    ImGui::DragIntRange2("Hue", &s.hMin, &s.hMax, 1.0f, 0, 179, "Min %d", "Max %d");
    Help("OpenCV hue is 0..179. If Min > Max the range wraps around (e.g. red 170..10).");
    ImGui::DragIntRange2("Saturation", &s.sMin, &s.sMax, 1.0f, 0, 255, "Min %d", "Max %d");
    ImGui::DragIntRange2("Value", &s.vMin, &s.vMax, 1.0f, 0, 255, "Min %d", "Max %d");

    // Color swatch of the middle of the range
    float h = (s.hMin <= s.hMax ? (s.hMin + s.hMax) * 0.5f : std::fmod((s.hMin + s.hMax + 180) * 0.5f, 180.0f)) / 179.0f;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, (s.sMin + s.sMax) / 510.0f, (s.vMin + s.vMax) / 510.0f, r, g, b);
    ImGui::ColorButton("##swatch", ImVec4(r, g, b, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
    ImGui::SameLine();
    ImGui::TextDisabled("Presets:");

    struct Preset { const char* name; int h0, h1, s0, v0; };
    static const Preset presets[] = {
        { "Red", 170, 10, 120, 70 }, { "Orange", 10, 25, 120, 70 }, { "Yellow", 25, 35, 100, 100 },
        { "Green", 35, 85, 80, 50 }, { "Blue", 90, 130, 80, 50 }, { "Purple", 130, 160, 60, 50 },
        { "White", 0, 179, 0, 200 }, { "Black", 0, 179, 0, 0 }, { "Skin", 0, 25, 40, 60 },
    };
    for (int i = 0; i < IM_ARRAYSIZE(presets); ++i) {
        if (i % 3) ImGui::SameLine();
        if (ImGui::Button(presets[i].name, ImVec2(70, 0))) {
            s.hMin = presets[i].h0; s.hMax = presets[i].h1;
            s.sMin = presets[i].s0; s.sMax = 255;
            s.vMin = presets[i].v0; s.vMax = 255;
            if (!strcmp(presets[i].name, "White")) s.sMax = 40;
            if (!strcmp(presets[i].name, "Black")) { s.sMax = 255; s.vMax = 50; }
            if (!strcmp(presets[i].name, "Skin"))  { s.sMax = 170; }
        }
    }
}

// ------------------------------------------------------------------
// Live results card
// ------------------------------------------------------------------
static void StatRow(const char* key, const char* fmt, ...) {
    ImGui::TextColored(TextMuted, "%s", key);
    ImGui::SameLine(ImGui::GetFontSize() * 7.0f);
    va_list args;
    va_start(args, fmt);
    ImGui::PushFont(Font::SemiBold());
    ImGui::TextV(fmt, args);
    ImGui::PopFont();
    va_end(args);
}

static void StatDetail(const char* fmt, ...) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 7.0f);
    va_list args;
    va_start(args, fmt);
    ImGui::PushFont(Font::Small());
    ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
    ImGui::TextWrappedV(fmt, args);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    va_end(args);
}

static void StatError(const char* key, const std::string& error) {
    ImGui::TextColored(TextMuted, "%s", key);
    ImGui::SameLine(ImGui::GetFontSize() * 7.0f);
    ImGui::TextColored(Danger, "%s", error.c_str());
}

static void DrawResults(const EffectsSettings& s, const DetectionStats& st) {
    BeginCard();
    SectionLabel("LIVE RESULTS");
    StatRow("Processing", "%.1f ms", st.processingMs);
    StatRow("Frame rate", "%.0f fps", ImGui::GetIO().Framerate);

    bool any = false;
    if (s.detectContours) {
        any = true;
        StatRow("Shapes", "%d", st.contours);
        StatDetail("Tri %d \xC2\xB7 Sq %d \xC2\xB7 Rect %d \xC2\xB7 Pent %d \xC2\xB7 Hex %d \xC2\xB7 Circle %d \xC2\xB7 Other %d",
                   st.triangles, st.squares, st.rectangles, st.pentagons, st.hexagons, st.circles, st.others);
    }
    if (s.houghCircles)               { any = true; StatRow("Circles", "%d", st.houghCircles); }
    if (s.houghLines)                 { any = true; StatRow("Lines", "%d", st.lines); }
    if (s.detectCorners)              { any = true; StatRow("Corners", "%d", st.corners); }
    if (s.featureMethod != FEAT_NONE) { any = true; StatRow("Keypoints", "%d", st.keypoints); }
    if (s.detectCascade) {
        any = true;
        if (!st.cascadeError.empty()) StatError("Objects", st.cascadeError);
        else if (s.detectEyes && s.cascadeType == CASCADE_FACE) StatRow("Objects", "%d  (eyes %d)", st.objects, st.eyes);
        else StatRow("Objects", "%d", st.objects);
    }
    if (s.detectQR) {
        any = true;
        StatRow("QR codes", "%d", (int)st.qrCodes.size());
        for (const auto& q : st.qrCodes) StatDetail("%s", q.c_str());
    }
    if (s.detectAruco) {
        any = true;
        if (!st.arucoError.empty()) StatError("Markers", st.arucoError);
        else {
            std::string ids;
            for (int id : st.arucoIds) ids += (ids.empty() ? "" : ", ") + std::to_string(id);
            StatRow("Markers", "%d", (int)st.arucoIds.size());
            if (!ids.empty()) StatDetail("ids %s", ids.c_str());
        }
    }
    if (s.trackColor) {
        any = true;
        if (st.tracked) {
            StatRow("Target", "(%.0f, %.0f)", st.trackX, st.trackY);
            StatDetail("dx %+.2f \xC2\xB7 dy %+.2f", st.trackErrX, st.trackErrY);
        } else {
            StatRow("Target", "not found");
        }
    }
    if (s.detectMotion) {
        any = true;
        StatRow("Motion", "%d regions", st.motionRegions);
        StatDetail("%.1f%% of the frame", st.motionPercent);
    }
    if (!any) {
        ImGui::PushFont(Font::Small());
        ImGui::TextColored(TextFaint, "Turn on a detector in Detect, Objects or Tracking to see its results here.");
        ImGui::PopFont();
    }

    if (s.showHistogram && st.histValid) {
        static const char* names[] = { "Blue", "Green", "Red" };
        static const ImVec4 colors[] = { Hex(0x5B8CFF), Hex(0x3DD68C), Hex(0xF2555A) };
        float w = s_cardWidth - 24 * U() - ImGui::CalcTextSize("Green").x - ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Canvas);
        for (int c = 0; c < st.histChannels; ++c) {
            ImGui::PushStyleColor(ImGuiCol_PlotLines, st.histChannels > 1 ? colors[c] : TextMuted);
            ImGui::PlotLines(st.histChannels > 1 ? names[c] : "Gray", st.hist[c].data(), 256, 0,
                             nullptr, 0.0f, 1.0f, ImVec2(w, 42 * U()));
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor();
    }
    EndCard();
}

// ------------------------------------------------------------------
// Tab: Filters
// ------------------------------------------------------------------
static void DrawFiltersTab(EffectsSettings& s) {
    if (ImGui::CollapsingHeader("Pre-processing", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Brightness", &s.brightness, -100.0f, 100.0f, "%.0f");
        ImGui::SliderFloat("Contrast", &s.contrast, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("Gamma", &s.gamma, 0.2f, 3.0f, "%.2f");
        Help("< 1 brightens dark areas, > 1 darkens them.");
        ImGui::SliderFloat("Sharpen", &s.sharpen, 0.0f, 3.0f, "%.2f");
        Help("Unsharp mask. Makes edges crisper.");
        ImGui::Checkbox("CLAHE", &s.clahe);
        Help("Contrast Limited Adaptive Histogram Equalization on luminance.\n"
             "Great for uneven lighting.");
        if (s.clahe) { ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::SliderFloat("Clip", &s.claheClip, 1.0f, 10.0f, "%.1f"); }
        ImGui::Checkbox("Equalize (gray)", &s.equalizeHist);
        Help("Global histogram equalization on the grayscale image used for detection.");
        ImGui::SameLine();
        ImGui::Checkbox("Invert", &s.invert);
        const char* blurs[] = { "None", "Gaussian", "Box", "Median", "Bilateral" };
        ImGui::Combo("Blur", &s.blurMethod, blurs, IM_ARRAYSIZE(blurs));
        Help("Reduce noise before detection. Bilateral smooths while keeping edges sharp (slow).");
        if (s.blurMethod != BLUR_NONE) OddKernelSlider("Blur Kernel", &s.blurSize, 31);
    }

    if (ImGui::CollapsingHeader("Color")) {
        const char* modes[] = { "BGR (Color)", "Grayscale", "HSV" };
        ImGui::Combo("Color Mode", &s.colorMode, modes, IM_ARRAYSIZE(modes));
        ImGui::Checkbox("HSV Color Mask", &s.colorMask);
        Help("Keep only pixels whose Hue/Saturation/Value fall inside the ranges.\n"
             "The mask also feeds threshold/edges/contours.");
        if (s.colorMask) HsvRangeEditor(s);
    }

    if (ImGui::CollapsingHeader("Threshold")) {
        const char* types[] = { "None", "Binary", "Binary Inverted", "Otsu (auto)",
                                "Adaptive Mean", "Adaptive Gaussian" };
        ImGui::Combo("Type", &s.thresholdType, types, IM_ARRAYSIZE(types));
        if (s.thresholdType == TH_BINARY || s.thresholdType == TH_BINARY_INV)
            ImGui::SliderInt("Threshold", &s.thresholdValue, 0, 255);
        if (s.thresholdType == TH_ADAPTIVE_MEAN || s.thresholdType == TH_ADAPTIVE_GAUSSIAN) {
            OddKernelSlider("Block Size", &s.adaptiveBlock, 51);
            ImGui::SliderInt("C", &s.adaptiveC, -20, 20);
            Help("Constant subtracted from the local mean.");
        }
    }

    if (ImGui::CollapsingHeader("Edge Detection")) {
        const char* methods[] = { "None", "Canny", "Sobel", "Laplacian" };
        ImGui::Combo("Method", &s.edgeMethod, methods, IM_ARRAYSIZE(methods));
        if (s.edgeMethod == EDGE_CANNY) {
            ImGui::DragIntRange2("Thresholds", &s.cannyLow, &s.cannyHigh, 1.0f, 0, 500, "Low %d", "High %d");
            Help("Gradients above High are edges; between Low and High only if connected to an edge.\n"
                 "A High:Low ratio of 2:1 to 3:1 works well.");
            const char* apertures[] = { "3", "5", "7" };
            ImGui::Combo("Aperture", &s.cannyAperture, apertures, IM_ARRAYSIZE(apertures));
            ImGui::Checkbox("L2 Gradient", &s.cannyL2);
        } else if (s.edgeMethod == EDGE_SOBEL || s.edgeMethod == EDGE_LAPLACIAN) {
            const char* ks[] = { "1", "3", "5", "7" };
            ImGui::Combo("Kernel", &s.edgeKernel, ks, IM_ARRAYSIZE(ks));
        }
    }

    if (ImGui::CollapsingHeader("Morphology")) {
        const char* ops[] = { "None", "Erode", "Dilate", "Open", "Close", "Gradient" };
        ImGui::Combo("Operation", &s.morphOp, ops, IM_ARRAYSIZE(ops));
        Help("Applied to the binary image.\nOpen removes small specks, Close fills small holes/gaps.");
        if (s.morphOp != MORPH_OP_NONE) {
            const char* shapes[] = { "Rectangle", "Cross", "Ellipse" };
            ImGui::Combo("Kernel Shape", &s.morphShape, shapes, IM_ARRAYSIZE(shapes));
            OddKernelSlider("Kernel Size", &s.morphSize, 21);
            ImGui::SliderInt("Iterations", &s.morphIterations, 1, 10);
        }
    }
}

// ------------------------------------------------------------------
// Tab: Detect (geometry)
// ------------------------------------------------------------------
static void DrawDetectTab(EffectsSettings& s) {
    if (ImGui::CollapsingHeader("Contours & Shape Detection", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Detect Shapes", &s.detectContours);
        Help("Finds contours in the binary image and classifies them by vertex count:\n"
             "Triangle, Square, Rectangle, Pentagon, Hexagon, Circle.\n"
             "If no threshold/edge/mask step is enabled, Canny is used automatically.");
        if (s.detectContours) {
            ImGui::Checkbox("External Only", &s.externalOnly);
            Help("Ignore contours nested inside other contours.");
            ImGui::SliderFloat("Min Area", &s.minArea, 0.0f, 20000.0f, "%.0f px", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Approx. Epsilon", &s.approxEpsilon, 0.5f, 10.0f, "%.1f %%");
            Help("Polygon approximation tolerance as % of the perimeter.\nHigher = fewer vertices.");
            ImGui::TextDisabled("Draw:");
            ImGui::Checkbox("Contour", &s.drawContours);      ImGui::SameLine();
            ImGui::Checkbox("Box", &s.drawBoundingBox);       ImGui::SameLine();
            ImGui::Checkbox("Rotated Box", &s.drawRotatedRect);
            ImGui::Checkbox("Min Circle", &s.drawMinCircle);  ImGui::SameLine();
            ImGui::Checkbox("Convex Hull", &s.drawHull);
            ImGui::Checkbox("Label", &s.drawLabels);          ImGui::SameLine();
            ImGui::Checkbox("Measurements", &s.drawMeasurements); ImGui::SameLine();
            ImGui::Checkbox("Center", &s.drawCentroid);
        }
    }

    if (ImGui::CollapsingHeader("Hough Circles")) {
        ImGui::Checkbox("Detect Circles", &s.houghCircles);
        if (s.houghCircles) {
            ImGui::SliderFloat("dp", &s.circleDp, 1.0f, 3.0f, "%.1f");
            Help("Inverse accumulator resolution. 1 = same as image.");
            ImGui::SliderInt("Min Distance", &s.circleMinDist, 1, 300);
            ImGui::SliderInt("Canny High", &s.circleParam1, 10, 300);
            ImGui::SliderInt("Accumulator", &s.circleParam2, 5, 150);
            Help("Lower = more (possibly false) circles.");
            ImGui::DragIntRange2("Radius", &s.circleMinRadius, &s.circleMaxRadius, 1.0f, 0, 500, "Min %d", "Max %d");
        }
    }

    if (ImGui::CollapsingHeader("Hough Lines")) {
        ImGui::Checkbox("Detect Lines", &s.houghLines);
        if (s.houghLines) {
            ImGui::SliderInt("Votes", &s.lineThreshold, 10, 300);
            ImGui::SliderInt("Min Length", &s.lineMinLength, 1, 500);
            ImGui::SliderInt("Max Gap", &s.lineMaxGap, 0, 100);
        }
    }

    if (ImGui::CollapsingHeader("Corners")) {
        ImGui::Checkbox("Detect Corners", &s.detectCorners);
        Help("Shi-Tomasi 'good features to track'.");
        if (s.detectCorners) {
            ImGui::SliderInt("Max Corners", &s.maxCorners, 1, 500);
            ImGui::SliderFloat("Quality", &s.cornerQuality, 0.001f, 0.3f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderInt("Min Distance##corner", &s.cornerMinDistance, 1, 100);
        }
    }

    if (ImGui::CollapsingHeader("Feature Keypoints")) {
        const char* methods[] = { "None", "ORB", "FAST", "SIFT" };
        ImGui::Combo("Detector", &s.featureMethod, methods, IM_ARRAYSIZE(methods));
        Help("Distinctive points used for matching, tracking and visual odometry.\n"
             "ORB: fast, rotation invariant. FAST: fastest, corners only. SIFT: most robust, slowest.");
        if (s.featureMethod != FEAT_NONE) {
            ImGui::SliderInt("Max Features", &s.maxFeatures, 10, 2000);
            if (s.featureMethod == FEAT_FAST) ImGui::SliderInt("FAST Threshold", &s.fastThreshold, 1, 100);
            ImGui::Checkbox("Show Size & Orientation", &s.richKeypoints);
        }
    }
}

// ------------------------------------------------------------------
// Tab: Objects
// ------------------------------------------------------------------
static void DrawObjectsTab(EffectsSettings& s) {
    if (ImGui::CollapsingHeader("Cascade Detector (Haar)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Detect Objects", &s.detectCascade);
        Help("Classic Viola-Jones detectors shipped with OpenCV.");
        if (s.detectCascade) {
            const char* types[] = { "Face (frontal)", "Face (profile)", "Smile", "Upper body",
                                    "Full body", "Cat face", "License plate" };
            ImGui::Combo("Target", &s.cascadeType, types, IM_ARRAYSIZE(types));
            if (s.cascadeType == CASCADE_FACE) ImGui::Checkbox("Detect Eyes", &s.detectEyes);
            ImGui::SliderFloat("Scale Factor", &s.cascadeScaleFactor, 1.01f, 1.5f, "%.2f");
            Help("Image pyramid step. Smaller = more accurate but slower.");
            ImGui::SliderInt("Min Neighbors", &s.cascadeMinNeighbors, 1, 15);
            Help("Higher = fewer false positives.");
            ImGui::SliderInt("Min Size", &s.cascadeMinSize, 10, 300, "%d px");
        }
    }

    if (ImGui::CollapsingHeader("QR Codes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Detect & Decode QR", &s.detectQR);
        Help("Detects multiple QR codes and decodes their text.\nTip: set Scale to 1.0 for small codes.");
    }

    if (ImGui::CollapsingHeader("ArUco / AprilTag Markers", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Detect Markers", &s.detectAruco);
        Help("Fiducial markers for robot localization / pose estimation.\n"
             "Red dot marks the marker's first (top-left) corner.");
        if (s.detectAruco) {
            const char* dicts[] = { "4x4 (50)", "5x5 (100)", "6x6 (250)", "7x7 (1000)",
                                    "ArUco original", "AprilTag 36h11" };
            ImGui::Combo("Dictionary", &s.arucoDict, dicts, IM_ARRAYSIZE(dicts));
        }
    }
}

// ------------------------------------------------------------------
// Tab: Tracking
// ------------------------------------------------------------------
static void DrawTrackingTab(EffectsSettings& s) {
    if (ImGui::CollapsingHeader("Color Tracking", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Track Color", &s.trackColor);
        Help("Follows the largest blob inside the HSV range.\n"
             "dx/dy = offset from image center (-1..1), handy for steering a rover.");
        if (s.trackColor) {
            HsvRangeEditor(s);
            ImGui::SliderFloat("Min Blob Area", &s.trackMinArea, 10.0f, 20000.0f, "%.0f px", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderInt("Trail Length", &s.trailLength, 0, 120);
        }
    }

    if (ImGui::CollapsingHeader("Motion Detection", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Detect Motion", &s.detectMotion);
        Help("MOG2 background subtraction. Works best with a static camera.\n"
             "Set View to 'Motion Mask' to see the foreground.");
        if (s.detectMotion) {
            ImGui::SliderInt("History", &s.motionHistory, 10, 1000);
            Help("Frames used to model the background.");
            ImGui::SliderFloat("Sensitivity", &s.motionVarThreshold, 4.0f, 100.0f, "%.0f");
            Help("Variance threshold. Lower = more sensitive.");
            ImGui::SliderFloat("Learning Rate", &s.motionLearningRate, -1.0f, 0.1f, s.motionLearningRate < 0 ? "auto" : "%.3f");
            ImGui::SliderFloat("Min Region", &s.motionMinArea, 10.0f, 20000.0f, "%.0f px", ImGuiSliderFlags_Logarithmic);
            ImGui::Checkbox("Detect Shadows", &s.motionShadows);
        }
    }
}

// ------------------------------------------------------------------
// Processing panel (Design page)
// ------------------------------------------------------------------
static void DrawProcessingPanel(EffectsSettings& s, const DetectionStats& st) {
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.58f);

    SectionLabel("OUTPUT");
    const char* views[] = { "Processed", "Binary / Edges", "Original + Overlay", "Motion Mask" };
    ImGui::Combo("View", &s.outputView, views, IM_ARRAYSIZE(views));
    Help("Processed: the color image after filters.\n"
         "Binary / Edges: result of mask / threshold / edges / morphology.\n"
         "Original + Overlay: the untouched frame with detections drawn on it.\n"
         "Motion Mask: foreground from motion detection.");
    ImGui::SliderFloat("Scale", &s.scale, 0.25f, 1.0f, "%.2f");
    Help("Processing resolution relative to the camera. Lower is faster.");
    ImGui::Checkbox("Histogram", &s.showHistogram);

    // "Reset all" right-aligned on the same row
    const char* resetLabel = ICON_RESET "  Reset all";
    float bw = ImGui::CalcTextSize(resetLabel).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw);
    ImGui::PushStyleColor(ImGuiCol_Text, TextMuted);
    if (ImGui::Button(resetLabel)) s = EffectsSettings{};
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset every processing setting to its default");

    ImGui::Dummy(ImVec2(0, 2));
    DrawResults(s, st);
    ImGui::Dummy(ImVec2(0, 4));

    // In-panel tabs: quiet pills instead of the docking tab look
    ImGui::PushStyleColor(ImGuiCol_Tab, Panel);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, SurfaceHover);
    ImGui::PushStyleColor(ImGuiCol_TabActive, SurfaceActive);
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, SurfaceActive);
    // Tab colors are read when each tab is drawn, so they stay pushed for the whole bar.
    if (ImGui::BeginTabBar("ProcessingTabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        if (ImGui::BeginTabItem(ICON_DROPLET " Filters"))     { ImGui::Spacing(); DrawFiltersTab(s);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(ICON_SHAPES " Detect"))       { ImGui::Spacing(); DrawDetectTab(s);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(ICON_CUBE " Objects"))        { ImGui::Spacing(); DrawObjectsTab(s);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(ICON_CROSSHAIRS " Tracking")) { ImGui::Spacing(); DrawTrackingTab(s); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleColor(4);

    ImGui::PopItemWidth();
}

// ------------------------------------------------------------------
// Header: brand, page switcher, camera controls
// ------------------------------------------------------------------
static void DrawHeader(int& page, float height, cv::VideoCapture& cap, CameraTextures& tex, int& currentIdx,
                       std::vector<int>& cams, EffectsSettings& settings, RuntimeControl& control) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float u = U();
    const ImVec2 p0 = ImGui::GetWindowPos();
    const float W = ImGui::GetWindowWidth();
    dl->AddRectFilled(p0, p0 + ImVec2(W, height), U32(Chrome));
    dl->AddLine(p0 + ImVec2(0, height - 1), p0 + ImVec2(W, height - 1), U32(Border));

    // ---- Brand ----
    const float logo = 28 * u;
    ImVec2 lp = p0 + ImVec2(16 * u, (height - logo) * 0.5f);
    dl->AddRectFilled(lp, lp + ImVec2(logo, logo), U32(Accent), 8 * u);
    ImVec2 is = ImGui::CalcTextSize(ICON_ROBOT);
    dl->AddText(lp + (ImVec2(logo, logo) - is) * 0.5f, U32(Hex(0xFFFFFF)), ICON_ROBOT);
    ImVec2 ns = TextSize(Font::Heading(), "TULPAR");
    float tx = lp.x + logo + 10 * u;
    DrawText(dl, Font::Heading(), ImVec2(tx, p0.y + (height - ns.y) * 0.5f), U32(Text), "TULPAR");
    ImVec2 ss = TextSize(Font::Small(), "Vision Studio");
    DrawText(dl, Font::Small(), ImVec2(tx + ns.x + 8 * u, p0.y + (height - ss.y) * 0.5f + 1 * u),
             U32(TextFaint), "Vision Studio");

    // ---- Page switcher (segmented control, centered) ----
    const char* pages[] = { ICON_SLIDERS "  Design", ICON_DIAGRAM "  Pipeline" };
    const float segH = 30 * u, segPad = 3 * u;
    float segW[2], total = segPad * 2;
    for (int i = 0; i < 2; ++i) { segW[i] = TextSize(Font::SemiBold(), pages[i]).x + 32 * u; total += segW[i]; }
    ImVec2 sp = p0 + ImVec2((W - total) * 0.5f, (height - segH - segPad * 2) * 0.5f);
    dl->AddRectFilled(sp, sp + ImVec2(total, segH + segPad * 2), U32(Surface), 9 * u);
    float x = sp.x + segPad;
    for (int i = 0; i < 2; ++i) {
        ImVec2 a(x, sp.y + segPad), b(x + segW[i], sp.y + segPad + segH);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##page", b - a)) page = i;
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        bool active = page == i;
        if (active) dl->AddRectFilled(a, b, U32(SurfaceActive), 7 * u);
        ImVec2 ts = TextSize(Font::SemiBold(), pages[i]);
        DrawText(dl, Font::SemiBold(), a + (b - a - ts) * 0.5f, U32(active ? Text : (hovered ? Text : TextMuted)), pages[i]);
        x += segW[i];
    }

    // ---- Right side, laid out right to left ----
    const float btn = 30 * u, gap = 6 * u;
    const float cy = p0.y + (height - btn) * 0.5f;
    float right = p0.x + W - 16 * u;

    // Status pill
    const char* state = !cap.isOpened() ? "No camera" : (control.paused ? "Paused" : "Live");
    ImVec4 stateCol = !cap.isOpened() ? Danger : (control.paused ? Warning : Success);
    ImVec2 st = TextSize(Font::SemiBold(), state);
    float pillW = st.x + 30 * u;
    ImVec2 pa(right - pillW, cy), pb(right, cy + btn);
    dl->AddRectFilled(pa, pb, U32(stateCol, 0.12f), btn * 0.5f);
    float pulse = control.paused || !cap.isOpened() ? 1.0f : 0.55f + 0.45f * std::sin((float)ImGui::GetTime() * 3.0f);
    dl->AddCircleFilled(ImVec2(pa.x + 13 * u, pa.y + btn * 0.5f), 4 * u, U32(stateCol, pulse));
    DrawText(dl, Font::SemiBold(), ImVec2(pa.x + 22 * u, pa.y + (btn - st.y) * 0.5f), U32(stateCol), state);
    right = pa.x - 12 * u;

    // Snapshot / pause / mirror
    right -= btn;
    if (IconButton("##snap", ICON_CAMERA, "Save input + output snapshot to ~/Pictures/TULPAR", ImVec2(right, cy), btn))
        control.snapshotRequested = true;
    right -= btn + gap;
    if (IconButton("##pause", control.paused ? ICON_PLAY : ICON_PAUSE,
                   control.paused ? "Resume the camera" : "Freeze the current frame (settings still apply)",
                   ImVec2(right, cy), btn, control.paused))
        control.paused = !control.paused;
    right -= btn + gap;
    if (IconButton("##mirror", ICON_MIRROR, "Mirror the image horizontally", ImVec2(right, cy), btn, settings.mirror))
        settings.mirror = !settings.mirror;
    right -= btn + gap;
    if (IconButton("##camsettings", ICON_SLIDERS, "Camera & Sensor Settings (Arducam / Raspberry Pi / Resolution)",
                   ImVec2(right, cy), btn, control.showCameraSettings))
        control.showCameraSettings = !control.showCameraSettings;

    // Camera selector
    const float comboW = 210 * u;
    right -= gap * 2 + comboW;
    ImGui::SetCursorScreenPos(ImVec2(right, cy));
    ImGui::SetNextItemWidth(comboW);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * u, (btn - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7 * u);

    const CameraDeviceInfo* activeDev = GetCameraDeviceInfo(currentIdx);
    std::string preview;
    if (cams.empty()) {
        preview = ICON_VIDEO "  No camera";
    } else if (activeDev) {
        std::string shortName = activeDev->name;
        if (shortName.size() > 22) shortName = shortName.substr(0, 19) + "...";
        preview = ICON_VIDEO "  " + shortName;
    } else {
        preview = ICON_VIDEO "  Camera " + std::to_string(currentIdx);
    }

    if (ImGui::BeginCombo("##camera", preview.c_str())) {
        const std::vector<CameraDeviceInfo>& devList = GetAvailableCameraDevices();
        for (const auto& dev : devList) {
            bool isSelected = (currentIdx == dev.id);
            std::string label = dev.name;
            if (dev.isArducam) label = "[Arducam] " + label;
            else if (dev.isRaspberryPi) label = "[RPi] " + label;

            if (ImGui::Selectable(label.c_str(), isSelected)) {
                currentIdx = dev.id;
                InitializeWebcam(cap, tex, currentIdx);
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        if (devList.empty()) ImGui::TextDisabled("No cameras detected");
        ImGui::Separator();
        if (ImGui::Selectable(ICON_SLIDERS "  Camera & Sensor Settings...")) {
            control.showCameraSettings = true;
        }
        if (ImGui::Selectable(ICON_REFRESH "  Rescan devices")) {
            RescanCameraDevices();
            cams = GetAvailableCameraIndices();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar(2);
}

// ------------------------------------------------------------------
// Camera & Sensor settings modal
// ------------------------------------------------------------------
static void DrawCameraSettingsModal(cv::VideoCapture& cap, CameraTextures& tex, int& currentIdx,
                                    std::vector<int>& cams, RuntimeControl& control) {
    const float u = U();
    ImGui::SetNextWindowSize(ImVec2(480 * u, 530 * u), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_SLIDERS "  Camera & Sensor Settings###CameraSettingsModal", &control.showCameraSettings,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    CameraConfig& cfg = GetCameraConfig();
    const CameraDeviceInfo* activeDev = GetCameraDeviceInfo(currentIdx);

    // 1. Device Info
    SectionLabel("CONNECTED CAMERA");
    if (activeDev) {
        ImGui::Text("%s", activeDev->name.c_str());
        if (activeDev->isArducam) {
            ImGui::SameLine();
            ImGui::TextColored(Accent, "[Arducam]");
        }
        if (activeDev->isRaspberryPi) {
            ImGui::SameLine();
            ImGui::TextColored(Success, "[Raspberry Pi]");
        }
        ImGui::TextDisabled("Type: %s  \xC2\xB7  %s",
            activeDev->type == CAM_TYPE_LIBCAMERA ? "libcamera / CSI" :
            (activeDev->type == CAM_TYPE_GSTREAMER ? "GStreamer" : "V4L2 / UVC"),
            activeDev->description.c_str());
        if (tex.width > 0) {
            ImGui::Text("Active Frame: %d \xC3\x97 %d", tex.width, tex.height);
        }
    } else {
        ImGui::TextDisabled("No camera connected.");
    }

    ImGui::Dummy(ImVec2(0, 4 * u));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4 * u));

    // 2. Hardware Controls (Autofocus for Arducam & RPi)
    SectionLabel("HARDWARE CONTROLS (ARDUCAM / RASPBERRY PI)");
    bool afChanged = ImGui::Checkbox("Hardware Auto-Focus", &cfg.autoFocus);
    Help("Controls motorized focus on Arducam IMX519, 64MP Hawkeye/OwlSight, Raspberry Pi Camera 3, and UVC AF lenses.");
    if (afChanged) {
        SetCameraFocus(cap, cfg.autoFocus, cfg.manualFocus);
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_CROSSHAIRS "  Trigger AF Sweep")) {
        TriggerCameraAutoFocus(cap);
    }
    Help("Initiate a single autofocus sweep search.");

    if (!cfg.autoFocus) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
        if (ImGui::SliderInt("Manual Focus", &cfg.manualFocus, 0, 1023)) {
            SetCameraFocus(cap, false, cfg.manualFocus);
        }
        Help("Manual lens focus position (0 = infinity / far, 1023 = near / macro).");
    }

    ImGui::Dummy(ImVec2(0, 4 * u));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4 * u));

    // 3. Resolution & Framerate
    SectionLabel("RESOLUTION & STREAM CONFIGURATION");
    const char* resPresets[] = { "1280 \xC3\x97 720 (720p HD - Recommended)",
                                 "1920 \xC3\x97 1080 (1080p Full HD)",
                                 "640 \xC3\x97 480 (VGA - Fast)",
                                 "Custom / Camera Default" };
    static int resIndex = 0;
    if (cfg.width == 1280 && cfg.height == 720) resIndex = 0;
    else if (cfg.width == 1920 && cfg.height == 1080) resIndex = 1;
    else if (cfg.width == 640 && cfg.height == 480) resIndex = 2;
    else resIndex = 3;

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.75f);
    if (ImGui::Combo("Resolution Preset", &resIndex, resPresets, IM_ARRAYSIZE(resPresets))) {
        if (resIndex == 0)      { cfg.width = 1280; cfg.height = 720; }
        else if (resIndex == 1) { cfg.width = 1920; cfg.height = 1080; }
        else if (resIndex == 2) { cfg.width = 640;  cfg.height = 480; }
    }

    const char* fpsPresets[] = { "30 FPS", "60 FPS", "15 FPS", "Default" };
    static int fpsIndex = 0;
    if (cfg.fps == 30) fpsIndex = 0;
    else if (cfg.fps == 60) fpsIndex = 1;
    else if (cfg.fps == 15) fpsIndex = 2;
    else fpsIndex = 3;

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.75f);
    if (ImGui::Combo("Target Framerate", &fpsIndex, fpsPresets, IM_ARRAYSIZE(fpsPresets))) {
        if (fpsIndex == 0) cfg.fps = 30;
        else if (fpsIndex == 1) cfg.fps = 60;
        else if (fpsIndex == 2) cfg.fps = 15;
        else cfg.fps = 0;
    }

    const char* formats[] = { "Auto (Recommended)", "MJPEG (High FPS for Arducam USB)", "YUYV (Uncompressed)" };
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.75f);
    ImGui::Combo("Pixel Format", &cfg.fourcc, formats, IM_ARRAYSIZE(formats));
    Help("MJPEG enables full 30-60 fps on Arducam USB and webcams over USB 2.0 without bandwidth throttling.");

    ImGui::Dummy(ImVec2(0, 4 * u));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4 * u));

    // 4. Custom GStreamer Pipeline / Libcamera Presets
    SectionLabel("GSTREAMER / LIBCAMERA PIPELINE");
    ImGui::TextDisabled("Quick presets for Raspberry Pi & Arducam CSI:");
    if (ImGui::Button("Pi / Arducam 720p")) {
        cfg.customPipeline = BuildLibcameraPipeline(1280, 720, 30);
        cfg.type = CAM_TYPE_LIBCAMERA;
    }
    ImGui::SameLine();
    if (ImGui::Button("Pi / Arducam 1080p")) {
        cfg.customPipeline = BuildLibcameraPipeline(1920, 1080, 30);
        cfg.type = CAM_TYPE_LIBCAMERA;
    }

    static char pipeBuf[512] = "";
    if (pipeBuf[0] == '\0' && !cfg.customPipeline.empty()) {
        snprintf(pipeBuf, sizeof(pipeBuf), "%s", cfg.customPipeline.c_str());
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##pipeline", "Custom pipeline or device path (e.g. libcamerasrc ! ...)", pipeBuf, sizeof(pipeBuf))) {
        cfg.customPipeline = pipeBuf;
    }

    ImGui::Dummy(ImVec2(0, 6 * u));
    if (ImGui::Button(ICON_REFRESH "  Apply & Reconnect Camera", ImVec2(-1, 32 * u))) {
        if (pipeBuf[0] != '\0') cfg.customPipeline = pipeBuf;
        InitializeCameraWithConfig(cap, tex, cfg);
    }

    ImGui::End();
}

// ------------------------------------------------------------------
// Status bar
// ------------------------------------------------------------------
static void DrawStatusBar(float height, int page, cv::VideoCapture& cap, const CameraTextures& tex, int currentIdx,
                          const RuntimeControl& control, const DetectionStats& stats) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float u = U();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();
    const ImVec2 p0(wp.x, wp.y + ws.y - height);
    dl->AddRectFilled(p0, p0 + ImVec2(ws.x, height), U32(Chrome));
    dl->AddLine(p0, p0 + ImVec2(ws.x, 0), U32(Border));

    ImGui::PushFont(Font::Small());
    const float ty = p0.y + (height - ImGui::GetFontSize()) * 0.5f;
    float x = p0.x + 16 * u;
    auto item = [&](const char* text, ImVec4 col) {
        dl->AddText(ImVec2(x, ty), U32(col), text);
        x += ImGui::CalcTextSize(text).x + 18 * u;
    };

    bool open = cap.isOpened();
    dl->AddCircleFilled(ImVec2(x + 3 * u, p0.y + height * 0.5f), 3.5f * u,
                        U32(!open ? Danger : (control.paused ? Warning : Success)));
    x += 14 * u;
    char buf[160];
    if (open && tex.width > 0) {
        const CameraDeviceInfo* dev = GetCameraDeviceInfo(currentIdx);
        std::string camTitle = dev ? dev->name : ("Camera " + std::to_string(currentIdx));
        snprintf(buf, sizeof(buf), "%s  \xC2\xB7  %d \xC3\x97 %d", camTitle.c_str(), tex.width, tex.height);
        item(buf, TextMuted);
    } else {
        item(open ? "Waiting for camera\xE2\x80\xA6" : "No camera", TextMuted);
    }
    if (page == PAGE_DESIGN) {
        if (tex.outWidth > 0) {
            snprintf(buf, sizeof(buf), "Processing at %d \xC3\x97 %d", tex.outWidth, tex.outHeight);
            item(buf, TextFaint);
        }
        snprintf(buf, sizeof(buf), ICON_STOPWATCH "  %.1f ms", stats.processingMs);
    } else {
        snprintf(buf, sizeof(buf), ICON_STOPWATCH "  Pipeline %.1f ms  \xC2\xB7  %d nodes",
                 g_graph.totalMs, (int)g_graph.nodes.size());
    }
    item(buf, TextFaint);

    // Right side: last snapshot + frame rate
    snprintf(buf, sizeof(buf), "%.0f fps", ImGui::GetIO().Framerate);
    float rx = p0.x + ws.x - 16 * u - ImGui::CalcTextSize(buf).x;
    dl->AddText(ImVec2(rx, ty), U32(TextFaint), buf);
    if (!control.lastSnapshot.empty()) {
        std::string path = control.lastSnapshot;
        if (path.size() > 60) path = "\xE2\x80\xA6" + path.substr(path.size() - 59);
        std::string saved = std::string(ICON_CAMERA "  ") + path;
        float sx = rx - 20 * u - ImGui::CalcTextSize(saved.c_str()).x;
        if (sx > x) dl->AddText(ImVec2(sx, ty), U32(TextMuted), saved.c_str());
    }
    ImGui::PopFont();
}

// ------------------------------------------------------------------
// Pages
// ------------------------------------------------------------------
static void DrawDesignPage(cv::VideoCapture& cap, CameraTextures& tex, EffectsSettings& settings,
                           const DetectionStats& stats) {
    ImGui::Begin(kWinProcessing);
    DrawProcessingPanel(settings, stats);
    ImGui::End();

    bool open = cap.isOpened();
    const char* empty = open ? "Waiting for the camera\xE2\x80\xA6" : "No camera connected";
    if (BeginFeedWindow(kWinInput))
        DrawFeed(open ? tex.input : 0, tex.width, tex.height, "Camera", ICON_VIDEO, empty);
    ImGui::End();
    if (BeginFeedWindow(kWinOutput))
        DrawFeed(open ? tex.output : 0, tex.outWidth, tex.outHeight, "Processed", ICON_IMAGE, empty);
    ImGui::End();
}

static void DrawPipelinePage(cv::VideoCapture& cap, CameraTextures& tex, const EffectsSettings& settings) {
    if (g_graph.nodes.empty()) InitNodeGraph(g_graph);

    bool open = cap.isOpened();
    if (open) {
        // Same orientation as the Input window (the header's mirror toggle)
        const cv::Mat& raw = GetLastFrame();
        cv::Mat frame;
        if (settings.mirror && !raw.empty()) cv::flip(raw, frame, 1);
        else frame = raw;
        EvaluateNodeGraph(g_graph, frame);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::Begin(kWinPalette);
    ImGui::PopStyleVar();
    DrawNodePalette(g_graph);
    ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin(kWinNodeEditor, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    DrawNodeEditor(g_graph);
    ImGui::End();

    const char* empty = open ? "Waiting for the camera\xE2\x80\xA6" : "No camera connected";
    if (BeginFeedWindow(kWinPipeInput))
        DrawFeed(open ? tex.input : 0, tex.width, tex.height, "Camera", ICON_VIDEO, empty);
    ImGui::End();
    if (BeginFeedWindow(kWinPipeOutput))
        DrawFeed(open ? g_graph.texture : 0, g_graph.texW, g_graph.texH, "Output node", ICON_DISPLAY,
                 open ? "Connect a node to Output in the Node Editor" : empty);
    ImGui::End();
}

void RenderUI(cv::VideoCapture& cap, CameraTextures& tex, int& currentIdx, std::vector<int>& cams,
              EffectsSettings& settings, RuntimeControl& control, const DetectionStats& stats) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Full-screen host window: header, one dockspace per page, status bar.
    // Hosting the dockspaces ourselves keeps the IDs we build layouts on
    // identical to the ones DockSpace() submits.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Chrome);
    ImGui::Begin("MainDockHost", nullptr, hostFlags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    static int page = -1;
    if (page < 0) { // TULPAR_PAGE=pipeline opens the Pipeline page first
        const char* env = std::getenv("TULPAR_PAGE");
        page = (env && !strcmp(env, "pipeline")) ? PAGE_PIPELINE : PAGE_DESIGN;
    }

    const float u = U();
    const float headerH = std::round(52 * u), statusH = std::round(28 * u);
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    DrawHeader(page, headerH, cap, tex, currentIdx, cams, settings, control);

    ImGui::SetCursorScreenPos(wp + ImVec2(0, headerH));
    const ImVec2 dockSize(ws.x, std::max(50.0f, ws.y - headerH - statusH));
    ImGuiID designDock = ImGui::GetID("DesignDockSpace");
    ImGuiID pipelineDock = ImGui::GetID("PipelineDockSpace");
    static bool layoutBuilt = false;
    if (!layoutBuilt) {
        BuildDesignLayout(designDock, dockSize);
        BuildPipelineLayout(pipelineDock, dockSize);
        layoutBuilt = true;
    }
    // Only the active page's dockspace is displayed; the other is kept alive so
    // its windows stay docked (and keep any layout changes) while hidden.
    ImGui::DockSpace(designDock, dockSize,
                     page == PAGE_DESIGN ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly);
    ImGui::DockSpace(pipelineDock, dockSize,
                     page == PAGE_PIPELINE ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly);

    DrawStatusBar(statusH, page, cap, tex, currentIdx, control, stats);
    ImGui::End();

    if (page == PAGE_DESIGN) DrawDesignPage(cap, tex, settings, stats);
    else                     DrawPipelinePage(cap, tex, settings);

    if (control.showCameraSettings) {
        DrawCameraSettingsModal(cap, tex, currentIdx, cams, control);
    }
}

void CleanupImGui() {
    DestroyNodeGraph(g_graph);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
