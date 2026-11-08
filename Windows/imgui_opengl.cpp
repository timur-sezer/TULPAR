#include "imgui_opengl.h"
#include "../dependencies/imgui/imgui.h"
#include "../dependencies/imgui/imgui_internal.h" // DockBuilder API
#include "../dependencies/imgui/imgui_impl_glfw.h"
#include "../dependencies/imgui/imgui_impl_opengl3.h"
#include "font.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

static const char* kWinCamera     = "Camera Settings";
static const char* kWinProcessing = "Image Processing";
static const char* kWinInput      = "Input Feed";
static const char* kWinOutput     = "Output Feed";

void InitImGui(GLFWwindow* window, const char* glsl_version) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr; // always start from the default docked layout

    ImGui::StyleColorsDark();
    Font::embraceTheDarkness();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
}

// Layout:
// +-----------------+-----------------------+-----------------------+
// | Camera Settings |                       |                       |
// +-----------------+      Input Feed       |      Output Feed      |
// | Image Processing|                       |                       |
// +-----------------+-----------------------+-----------------------+
static void BuildDefaultLayout(ImGuiID dockspaceId, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, size);

    ImGuiID left, right;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.30f, &left, &right);
    ImGuiID leftTop, leftBottom;
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.20f, &leftTop, &leftBottom);
    ImGuiID input, output;
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Left, 0.5f, &input, &output);

    ImGui::DockBuilderDockWindow(kWinCamera, leftTop);
    ImGui::DockBuilderDockWindow(kWinProcessing, leftBottom);
    ImGui::DockBuilderDockWindow(kWinInput, input);
    ImGui::DockBuilderDockWindow(kWinOutput, output);
    ImGui::DockBuilderFinish(dockspaceId);
}

// Draw a texture scaled to fit the window while keeping the frame's aspect ratio.
static void DrawFeed(GLuint texture, int w, int h, const char* emptyText) {
    if (texture == 0 || w <= 0 || h <= 0) {
        ImGui::TextDisabled("%s", emptyText);
        return;
    }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float scale = std::min(avail.x / w, avail.y / h);
    if (scale <= 0.0f) return;
    ImVec2 size(w * scale, h * scale);
    ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - size.x) * 0.5f,
                               cursor.y + (avail.y - size.y) * 0.5f));
    ImGui::Image((ImTextureID)(intptr_t)texture, size);
}

// Small "(?)" marker with a tooltip.
static void Help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
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
// Results (always visible above the tabs)
// ------------------------------------------------------------------
static void DrawResults(const EffectsSettings& s, const DetectionStats& st) {
    ImGui::SeparatorText("Results");
    ImGui::Text("Processing: %.1f ms   UI: %.0f FPS", st.processingMs, ImGui::GetIO().Framerate);
    if (s.detectContours) {
        ImGui::Text("Shapes: %d", st.contours);
        ImGui::TextDisabled("Tri %d  Sq %d  Rect %d  Pent %d  Hex %d  Circ %d  Other %d",
                            st.triangles, st.squares, st.rectangles, st.pentagons,
                            st.hexagons, st.circles, st.others);
    }
    if (s.houghCircles)                 ImGui::Text("Hough circles: %d", st.houghCircles);
    if (s.houghLines)                   ImGui::Text("Hough lines: %d", st.lines);
    if (s.detectCorners)                ImGui::Text("Corners: %d", st.corners);
    if (s.featureMethod != FEAT_NONE)   ImGui::Text("Keypoints: %d", st.keypoints);
    if (s.detectCascade) {
        if (!st.cascadeError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", st.cascadeError.c_str());
        else ImGui::Text("Objects: %d%s", st.objects,
                         s.detectEyes && s.cascadeType == CASCADE_FACE ? ("   Eyes: " + std::to_string(st.eyes)).c_str() : "");
    }
    if (s.detectQR) {
        ImGui::Text("QR codes: %d", (int)st.qrCodes.size());
        for (const auto& q : st.qrCodes) ImGui::BulletText("%s", q.c_str());
    }
    if (s.detectAruco) {
        if (!st.arucoError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", st.arucoError.c_str());
        else {
            std::string ids;
            for (int id : st.arucoIds) ids += std::to_string(id) + " ";
            ImGui::Text("Markers: %d  %s", (int)st.arucoIds.size(), ids.c_str());
        }
    }
    if (s.trackColor) {
        if (st.tracked)
            ImGui::Text("Target: (%.0f, %.0f)  dx %+.2f  dy %+.2f", st.trackX, st.trackY, st.trackErrX, st.trackErrY);
        else
            ImGui::TextDisabled("Target: not found");
    }
    if (s.detectMotion)
        ImGui::Text("Motion: %d regions, %.1f%% of frame", st.motionRegions, st.motionPercent);

    if (s.showHistogram && st.histValid) {
        static const char* names[] = { "Blue", "Green", "Red" };
        static const ImVec4 colors[] = { {0.3f, 0.5f, 1, 1}, {0.3f, 0.9f, 0.3f, 1}, {1, 0.35f, 0.35f, 1} };
        float w = ImGui::GetContentRegionAvail().x;
        for (int c = 0; c < st.histChannels; ++c) {
            if (st.histChannels > 1) ImGui::PushStyleColor(ImGuiCol_PlotLines, colors[c]);
            ImGui::PlotLines(st.histChannels > 1 ? names[c] : "Gray", st.hist[c].data(), 256, 0,
                             nullptr, 0.0f, 1.0f, ImVec2(w - 60, 45));
            if (st.histChannels > 1) ImGui::PopStyleColor();
        }
    }
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
        Help("Detects multiple QR codes and decodes their text.\nTip: use Processing Scale 1.0 for small codes.");
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
             "Set Output View to 'Motion Mask' to see the foreground.");
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

static void DrawProcessingPanel(EffectsSettings& s, const DetectionStats& st) {
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);

    // ---------- Output ----------
    const char* views[] = { "Processed", "Binary / Edges", "Original + Overlay", "Motion Mask" };
    ImGui::Combo("Output View", &s.outputView, views, IM_ARRAYSIZE(views));
    Help("Processed: the color image after filters.\n"
         "Binary / Edges: result of mask / threshold / edges / morphology.\n"
         "Original + Overlay: the untouched frame with detections drawn on it.\n"
         "Motion Mask: foreground from motion detection.");
    ImGui::SliderFloat("Processing Scale", &s.scale, 0.25f, 1.0f, "%.2f");
    Help("Processing resolution relative to the camera. Lower is faster.");
    ImGui::Checkbox("Histogram", &s.showHistogram);
    ImGui::SameLine();
    if (ImGui::Button("Reset All")) s = EffectsSettings{};

    DrawResults(s, st);
    ImGui::Separator();

    if (ImGui::BeginTabBar("ProcessingTabs")) {
        if (ImGui::BeginTabItem("Filters"))  { DrawFiltersTab(s);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Detect"))   { DrawDetectTab(s);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Objects"))  { DrawObjectsTab(s);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Tracking")) { DrawTrackingTab(s); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::PopItemWidth();
}

void RenderUI(cv::VideoCapture& cap, CameraTextures& tex, int& currentIdx, std::vector<int>& cams,
              EffectsSettings& settings, RuntimeControl& control, const DetectionStats& stats) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Host the dockspace in our own full-screen window so the dockspace ID we
    // build the layout on is the same one that DockSpace() submits.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("MainDockHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    static bool layoutBuilt = false;
    if (!layoutBuilt) {
        BuildDefaultLayout(dockspaceId, viewport->WorkSize);
        layoutBuilt = true;
    }
    ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();

    // --- Camera settings ---
    ImGui::Begin(kWinCamera);
    if (ImGui::Button("Refresh Devices")) {
        cams = GetAvailableCameraIndices();
    }
    ImGui::SameLine();
    if (ImGui::Button(control.paused ? "Resume" : "Pause")) control.paused = !control.paused;
    Help("Freeze the current frame. Settings still apply, so you can tune on a still image.");
    ImGui::SameLine();
    if (ImGui::Button("Snapshot")) control.snapshotRequested = true;
    Help("Save input and output images to ~/Pictures/TULPAR.");
    ImGui::SameLine();
    ImGui::Checkbox("Mirror", &settings.mirror);

    if (cams.empty()) {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "No cameras detected!");
    } else {
        std::string comboLabel = "Camera " + std::to_string(currentIdx);
        if (ImGui::BeginCombo("Select Camera", comboLabel.c_str())) {
            for (int i : cams) {
                bool isSelected = (currentIdx == i);
                std::string itemLabel = "Camera " + std::to_string(i);
                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                    currentIdx = i;
                    InitializeWebcam(cap, tex, currentIdx);
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (tex.width > 0)
            ImGui::Text("Camera: %d x %d   Processing: %d x %d%s",
                        tex.width, tex.height, tex.outWidth, tex.outHeight, control.paused ? "   [PAUSED]" : "");
    }
    if (!control.lastSnapshot.empty()) ImGui::TextDisabled("Saved: %s", control.lastSnapshot.c_str());
    ImGui::End();

    // --- Image processing ---
    ImGui::Begin(kWinProcessing);
    DrawProcessingPanel(settings, stats);
    ImGui::End();

    // --- Feeds ---
    bool live = cap.isOpened();
    ImGui::Begin(kWinInput);
    DrawFeed(live ? tex.input : 0, tex.width, tex.height, "No active camera feed.");
    ImGui::End();

    ImGui::Begin(kWinOutput);
    DrawFeed(live ? tex.output : 0, tex.outWidth, tex.outHeight, "No active camera feed.");
    ImGui::End();
}

void CleanupImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
