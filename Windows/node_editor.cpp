#define IMGUI_DEFINE_MATH_OPERATORS // ImVec2 + - * operators
#include "node_editor.h"
#include "../dependencies/imgui/imgui_internal.h" // BeginDragDropTargetCustom, ImRect, AllowItemOverlap
#include "font.h"
#include "icons.h"
// Same OpenCV 4/5 compatibility includes as opencv_backend.cpp
#if __has_include(<opencv2/geometry.hpp>)
#include <opencv2/geometry.hpp>
#endif
#if __has_include(<opencv2/xobjdetect.hpp>)
#include <opencv2/xobjdetect.hpp>
#endif
#if __has_include(<opencv2/objdetect/aruco_detector.hpp>)
#include <opencv2/objdetect/aruco_detector.hpp>
#define HAVE_ARUCO 1
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

// ============================================================
// Node type definitions
// ============================================================

enum NodeType {
    NT_CAMERA, NT_OUTPUT,
    // Color
    NT_GRAY, NT_HSV_RANGE, NT_BRIGHTNESS, NT_EQUALIZE, NT_CLAHE, NT_INVERT,
    // Filter
    NT_GAUSSIAN, NT_MEDIAN, NT_BILATERAL, NT_SHARPEN, NT_RESIZE,
    // Threshold
    NT_THRESHOLD, NT_ADAPTIVE,
    // Edges
    NT_CANNY, NT_SOBEL, NT_LAPLACIAN,
    // Morphology
    NT_MORPH,
    // Combine
    NT_APPLY_MASK, NT_BLEND, NT_BITWISE,
    // Detection
    NT_CONTOURS, NT_HOUGH_CIRCLES, NT_HOUGH_LINES, NT_CORNERS, NT_KEYPOINTS,
    NT_HAAR, NT_QR, NT_ARUCO, NT_MOTION,
    NT_COUNT
};

enum ParamKind { P_INT, P_FLOAT, P_LOG, P_BOOL, P_COMBO, P_ODD };

struct ParamDef {
    ParamKind   kind;
    const char* label;
    float       def, min, max;
    const char* items; // P_COMBO: "A\0B\0C\0"
};

static ParamDef PInt(const char* l, int d, int mn, int mx)       { return { P_INT, l, (float)d, (float)mn, (float)mx, nullptr }; }
static ParamDef PFloat(const char* l, float d, float mn, float mx) { return { P_FLOAT, l, d, mn, mx, nullptr }; }
static ParamDef PLog(const char* l, float d, float mn, float mx)   { return { P_LOG, l, d, mn, mx, nullptr }; }
static ParamDef PBool(const char* l, bool d)                       { return { P_BOOL, l, d ? 1.0f : 0.0f, 0, 1, nullptr }; }
static ParamDef PCombo(const char* l, int d, const char* items)    { return { P_COMBO, l, (float)d, 0, 0, items }; }
static ParamDef POdd(const char* l, int d, int mn, int mx)         { return { P_ODD, l, (float)d, (float)mn, (float)mx, nullptr }; }

struct NodeDef {
    const char* name;
    const char* category;
    int         numInputs;
    const char* inputNames[2];
    std::vector<ParamDef> params;
    const char* help;
};

static const char* kCascadeTargets = "Face\0Profile face\0Smile\0Upper body\0Full body\0Cat face\0License plate\0";

// Order must match NodeType.
static const std::vector<NodeDef>& Defs() {
    static const std::vector<NodeDef> defs = {
        { "Camera", "Input / Output", 0, {}, { PFloat("Scale", 0.5f, 0.1f, 1.0f) },
          "Live camera frame (mirrored with the header's mirror button). Lower Scale = faster processing." },
        { "Output", "Input / Output", 1, { "Image" }, {},
          "Whatever reaches this node is shown in the Camera Output window." },

        // ---- Color ----
        { "Grayscale", "Color", 1, { "Image" }, {}, "cvtColor(BGR2GRAY)" },
        { "HSV Range", "Color", 1, { "Image" },
          { PInt("H min", 0, 0, 179), PInt("H max", 179, 0, 179), PInt("S min", 0, 0, 255),
            PInt("S max", 255, 0, 255), PInt("V min", 0, 0, 255), PInt("V max", 255, 0, 255) },
          "inRange in HSV space -> binary mask. Hue is 0..179; H min > H max wraps around (red)." },
        { "Brightness / Contrast", "Color", 1, { "Image" },
          { PFloat("Brightness", 0, -100, 100), PFloat("Contrast", 1, 0, 3), PFloat("Gamma", 1, 0.2f, 3) },
          "convertTo(alpha = contrast, beta = brightness) + gamma LUT." },
        { "Equalize Histogram", "Color", 1, { "Image" }, {},
          "equalizeHist. Color images are equalized on luminance (YCrCb)." },
        { "CLAHE", "Color", 1, { "Image" }, { PFloat("Clip", 2, 1, 10), PInt("Tiles", 8, 2, 16) },
          "Adaptive histogram equalization. Great for uneven lighting." },
        { "Invert", "Color", 1, { "Image" }, {}, "bitwise_not" },

        // ---- Filter ----
        { "Gaussian Blur", "Filter", 1, { "Image" }, { POdd("Kernel", 5, 1, 31), PFloat("Sigma", 0, 0, 10) },
          "GaussianBlur. Sigma 0 = computed from the kernel size." },
        { "Median Blur", "Filter", 1, { "Image" }, { POdd("Kernel", 5, 3, 31) },
          "medianBlur. Removes salt & pepper noise." },
        { "Bilateral Filter", "Filter", 1, { "Image" },
          { PInt("Diameter", 9, 1, 15), PFloat("Sigma color", 75, 1, 200), PFloat("Sigma space", 75, 1, 200) },
          "bilateralFilter. Smooths while keeping edges sharp (slow)." },
        { "Sharpen", "Filter", 1, { "Image" }, { PFloat("Amount", 1, 0, 3) }, "Unsharp mask." },
        { "Resize", "Filter", 1, { "Image" }, { PFloat("Scale", 0.5f, 0.1f, 2.0f) }, "resize by a factor." },

        // ---- Threshold ----
        { "Threshold", "Threshold", 1, { "Image" },
          { PCombo("Type", 0, "Binary\0Binary inverted\0Otsu\0Otsu inverted\0Truncate\0To zero\0"),
            PInt("Value", 127, 0, 255) },
          "threshold on grayscale. Otsu picks the value automatically." },
        { "Adaptive Threshold", "Threshold", 1, { "Image" },
          { PCombo("Method", 1, "Mean\0Gaussian\0"), POdd("Block", 11, 3, 51), PInt("C", 2, -20, 20),
            PBool("Invert", false) },
          "adaptiveThreshold. Handles uneven lighting." },

        // ---- Edges ----
        { "Canny", "Edges", 1, { "Image" },
          { PInt("Low", 50, 0, 500), PInt("High", 150, 0, 500), PCombo("Aperture", 0, "3\0" "5\0" "7\0"),
            PBool("L2 gradient", false) },
          "Canny edge detector. A High:Low ratio of 2:1 to 3:1 works well." },
        { "Sobel", "Edges", 1, { "Image" },
          { PCombo("Kernel", 1, "1\0" "3\0" "5\0" "7\0"), PCombo("Direction", 0, "X + Y\0X\0Y\0") },
          "Sobel gradient magnitude." },
        { "Laplacian", "Edges", 1, { "Image" }, { PCombo("Kernel", 1, "1\0" "3\0" "5\0" "7\0") },
          "Second derivative edges." },

        // ---- Morphology ----
        { "Morphology", "Morphology", 1, { "Image" },
          { PCombo("Operation", 2, "Erode\0Dilate\0Open\0Close\0Gradient\0Top hat\0Black hat\0"),
            PCombo("Shape", 0, "Rectangle\0Cross\0Ellipse\0"), POdd("Kernel", 3, 1, 21),
            PInt("Iterations", 1, 1, 10) },
          "morphologyEx. Open removes specks, Close fills holes." },

        // ---- Combine ----
        { "Apply Mask", "Combine", 2, { "Image", "Mask" }, {},
          "Keeps only the Image pixels where Mask is non-zero." },
        { "Blend", "Combine", 2, { "A", "B" }, { PFloat("B weight", 0.5f, 0, 1) }, "addWeighted(A, B)." },
        { "Bitwise", "Combine", 2, { "A", "B" }, { PCombo("Operation", 0, "AND\0OR\0XOR\0") },
          "Combine two masks/images." },

        // ---- Detection ----
        { "Contours / Shapes", "Detection", 2, { "Detect", "Draw on" },
          { PLog("Min area", 500, 1, 50000), PFloat("Epsilon %", 2, 0.5f, 10), PBool("External only", true),
            PBool("Draw contour", true), PBool("Bounding box", false), PBool("Labels", true) },
          "findContours + approxPolyDP; classifies triangles, squares, circles...\n"
          "Detect: a binary mask works best (color images get Canny automatically).\n"
          "Draw on: optional image to draw the results on." },
        { "Hough Circles", "Detection", 2, { "Detect", "Draw on" },
          { PFloat("dp", 1.2f, 1, 3), PInt("Min dist", 40, 1, 300), PInt("Canny high", 100, 10, 300),
            PInt("Accumulator", 40, 5, 150), PInt("Min radius", 10, 0, 500), PInt("Max radius", 150, 0, 500) },
          "HoughCircles. Lower Accumulator = more (possibly false) circles." },
        { "Hough Lines", "Detection", 2, { "Detect", "Draw on" },
          { PInt("Votes", 80, 10, 300), PInt("Min length", 50, 1, 500), PInt("Max gap", 10, 0, 100) },
          "HoughLinesP. Detect: an edge image (color images get Canny automatically)." },
        { "Corners", "Detection", 2, { "Detect", "Draw on" },
          { PInt("Max corners", 100, 1, 500), PLog("Quality", 0.01f, 0.001f, 0.3f), PInt("Min dist", 10, 1, 100) },
          "Shi-Tomasi goodFeaturesToTrack." },
        { "Keypoints", "Detection", 2, { "Detect", "Draw on" },
          { PCombo("Method", 0, "ORB\0FAST\0SIFT\0"), PInt("Max features", 500, 10, 2000),
            PInt("FAST threshold", 20, 1, 100) },
          "Feature keypoints used for matching and tracking." },
        { "Haar Cascade", "Detection", 2, { "Detect", "Draw on" },
          { PCombo("Target", 0, kCascadeTargets), PFloat("Scale factor", 1.1f, 1.01f, 1.5f),
            PInt("Min neighbors", 5, 1, 15), PInt("Min size", 30, 10, 300) },
          "Viola-Jones object detection (faces, bodies, plates...)." },
        { "QR Code", "Detection", 2, { "Detect", "Draw on" }, {}, "Detects and decodes QR codes." },
        { "ArUco Markers", "Detection", 2, { "Detect", "Draw on" },
          { PCombo("Dictionary", 0, "4x4 (50)\0" "5x5 (100)\0" "6x6 (250)\0" "7x7 (1000)\0ArUco original\0AprilTag 36h11\0") },
          "Fiducial markers for localization. Red dot = first corner." },
        { "Motion (MOG2)", "Detection", 1, { "Image" },
          { PInt("History", 300, 10, 1000), PFloat("Sensitivity", 25, 4, 100), PBool("Shadows", false) },
          "Background subtraction -> foreground mask.\n"
          "Feed it into Contours / Shapes to get boxes around moving objects." },
    };
    IM_ASSERT((int)defs.size() == NT_COUNT);
    return defs;
}

static const char* kCategories[] = { "Input / Output", "Color", "Filter", "Threshold", "Edges",
                                      "Morphology", "Combine", "Detection" };

static ImVec4 CategoryColorV(const char* cat) {
    struct C { const char* name; unsigned rgb; };
    static const C colors[] = {
        { "Input / Output", 0xF0706A }, { "Color", 0xF5A524 },      { "Filter", 0x5B8CFF },
        { "Threshold", 0xA77BFF },      { "Edges", 0xF2C94C },      { "Morphology", 0xF472B6 },
        { "Combine", 0x94A3B8 },        { "Detection", 0x3DD68C },
    };
    for (const auto& c : colors)
        if (!strcmp(c.name, cat)) return Theme::Hex(c.rgb);
    return Theme::TextMuted;
}

static ImU32 CategoryColor(const char* cat, float alpha = 1.0f) {
    return Theme::U32(CategoryColorV(cat), alpha);
}

static const char* CategoryIcon(const char* cat) {
    struct I { const char* name; const char* icon; };
    static const I icons[] = {
        { "Input / Output", ICON_VIDEO }, { "Color", ICON_PALETTE },  { "Filter", ICON_DROPLET },
        { "Threshold", ICON_CONTRAST },   { "Edges", ICON_WAVE },     { "Morphology", ICON_SHAPES },
        { "Combine", ICON_LAYERS },       { "Detection", ICON_CROSSHAIRS },
    };
    for (const auto& i : icons)
        if (!strcmp(i.name, cat)) return i.icon;
    return ICON_CUBE;
}

static const char* NodeIcon(int type, const char* cat) {
    return type == NT_OUTPUT ? ICON_DISPLAY : CategoryIcon(cat);
}

// ============================================================
// Graph helpers
// ============================================================

struct NodeState {
    cv::Ptr<cv::BackgroundSubtractorMOG2> mog;
    int      mogHistory = -1;
    bool     mogShadows = false;
    cv::Size mogSize;
    int      mogWarmup = 0;

    cv::CascadeClassifier cascade;
    int      cascadeType = -1;
    bool     cascadeOk = false;
};

static GraphNode* FindNode(NodeGraph& g, int id) {
    for (auto& n : g.nodes) if (n->id == id) return n.get();
    return nullptr;
}

static bool IsSelected(const NodeGraph& g, int id) {
    return std::find(g.selection.begin(), g.selection.end(), id) != g.selection.end();
}

static void SelectOnly(NodeGraph& g, int id) {
    g.selection.clear();
    if (id >= 0) g.selection.push_back(id);
    g.selected = id;
}

static void ToggleSelected(NodeGraph& g, int id) {
    auto it = std::find(g.selection.begin(), g.selection.end(), id);
    if (it != g.selection.end()) {
        g.selection.erase(it);
        if (g.selected == id) g.selected = g.selection.empty() ? -1 : g.selection.back();
    } else {
        g.selection.push_back(id);
        g.selected = id;
    }
}

static int AddNode(NodeGraph& g, int type, ImVec2 pos) {
    auto n = std::make_unique<GraphNode>();
    n->id = g.nextId++;
    n->type = type;
    n->pos = pos;
    for (const auto& p : Defs()[type].params) n->params.push_back(p.def);
    n->state = std::make_shared<NodeState>();
    int id = n->id;
    g.nodes.push_back(std::move(n));
    SelectOnly(g, id);
    return id;
}

static void DeleteNode(NodeGraph& g, int id) {
    GraphNode* n = FindNode(g, id);
    if (!n || n->type == NT_OUTPUT) return; // the Output node is permanent
    for (auto& o : g.nodes)
        for (int& in : o->inputs) if (in == id) in = -1;
    g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(),
                                 [id](const std::unique_ptr<GraphNode>& p) { return p->id == id; }),
                  g.nodes.end());
    g.selection.erase(std::remove(g.selection.begin(), g.selection.end(), id), g.selection.end());
    if (g.selected == id) g.selected = g.selection.empty() ? -1 : g.selection.back();
}

static void DeleteSelection(NodeGraph& g) {
    std::vector<int> ids = g.selection;
    for (int id : ids) DeleteNode(g, id);
}

// Copies the selected nodes (with their settings). Links between copied nodes are
// re-created between the copies; links from outside keep their original source.
static void DuplicateSelection(NodeGraph& g) {
    std::vector<std::pair<int, int>> map; // old id -> new id
    for (int id : g.selection) {
        GraphNode* n = FindNode(g, id);
        if (!n || n->type == NT_OUTPUT) continue;
        int nid = AddNode(g, n->type, n->pos + ImVec2(36, 36));
        GraphNode* c = FindNode(g, nid);
        n = FindNode(g, id); // AddNode may have reallocated the vector (the nodes themselves don't move)
        c->params = n->params;
        c->inputs[0] = n->inputs[0];
        c->inputs[1] = n->inputs[1];
        map.push_back({ id, nid });
    }
    for (auto& [oldId, newId] : map) {
        GraphNode* c = FindNode(g, newId);
        for (int& in : c->inputs)
            for (auto& [o, nn] : map) if (in == o) { in = nn; break; }
    }
    g.selection.clear();
    for (auto& m : map) g.selection.push_back(m.second);
    g.selected = g.selection.empty() ? -1 : g.selection.back();
}

static const ImVec4 kGroupColors[] = {
    Theme::Hex(0x5B8CFF), Theme::Hex(0x3DD68C), Theme::Hex(0xF5A524),
    Theme::Hex(0xF472B6), Theme::Hex(0xA77BFF), Theme::Hex(0x94A3B8),
};
static const char* kGroupColorNames[] = { "Blue", "Green", "Amber", "Pink", "Violet", "Slate" };
static const int kGroupColorCount = 6;

static NodeGroup* FindGroup(NodeGraph& g, int id) {
    for (auto& gr : g.groups) if (gr.id == id) return &gr;
    return nullptr;
}

// Adds a group wrapping `members` (sized once they've been measured) or at `pos`.
static int AddGroup(NodeGraph& g, const std::string& title, std::vector<int> members, ImVec2 pos = ImVec2(0, 0)) {
    NodeGroup gr;
    gr.id = g.nextGroupId++;
    gr.title = title;
    gr.pos = pos;
    gr.color = (gr.id - 1) % kGroupColorCount;
    gr.fitTo = std::move(members);
    g.groups.push_back(gr);
    return gr.id;
}

static void DeleteGroup(NodeGraph& g, int id) {
    g.groups.erase(std::remove_if(g.groups.begin(), g.groups.end(), [id](const NodeGroup& gr) { return gr.id == id; }),
                   g.groups.end());
    if (g.renamingGroup == id) g.renamingGroup = -1;
}

// True if `target` is `from` or one of its (transitive) inputs.
static bool DependsOn(NodeGraph& g, int from, int target, int depth = 0) {
    if (from == target) return true;
    GraphNode* n = FindNode(g, from);
    if (!n || depth > 256) return false;
    for (int in : n->inputs)
        if (in >= 0 && DependsOn(g, in, target, depth + 1)) return true;
    return false;
}

static void Connect(NodeGraph& g, int src, int dst, int input) {
    GraphNode* d = FindNode(g, dst);
    if (!d || !FindNode(g, src) || input < 0 || input >= Defs()[d->type].numInputs) return;
    if (DependsOn(g, src, dst)) return; // would create a cycle
    d->inputs[input] = src;
}

void InitNodeGraph(NodeGraph& g) {
    GLuint texture = g.texture; // keep the GL texture, reset everything else
    g = NodeGraph{};
    g.texture = texture;
    int cam = AddNode(g, NT_CAMERA, ImVec2(40, 40));
    int out = AddNode(g, NT_OUTPUT, ImVec2(420, 40));
    Connect(g, cam, out, 0);
    SelectOnly(g, -1);
}

// Camera -> [Gray -> Blur -> Canny] -> [Dilate -> Contours (drawn on the camera image)] -> Output
static void LoadExample(NodeGraph& g) {
    InitNodeGraph(g);
    int cam = g.nodes[0]->id, out = g.nodes[1]->id;
    // One row, left to right (fits a typical editor at a zoom where the widgets stay usable)
    g.nodes[0]->pos = ImVec2(0, 60);
    int gray  = AddNode(g, NT_GRAY, ImVec2(264, 60));
    int blur  = AddNode(g, NT_GAUSSIAN, ImVec2(504, 60));
    int canny = AddNode(g, NT_CANNY, ImVec2(744, 60));
    int morph = AddNode(g, NT_MORPH, ImVec2(1032, 60));
    int cont  = AddNode(g, NT_CONTOURS, ImVec2(1272, 60));
    FindNode(g, morph)->params[0] = 1; // Dilate: close small gaps in the edges
    FindNode(g, out)->pos = ImVec2(1548, 60);
    Connect(g, cam, gray, 0);
    Connect(g, gray, blur, 0);
    Connect(g, blur, canny, 0);
    Connect(g, canny, morph, 0);
    Connect(g, morph, cont, 0);
    Connect(g, cam, cont, 1);
    Connect(g, cont, out, 0);
    AddGroup(g, "Edge detection", { gray, blur, canny });
    AddGroup(g, "Shape detection", { morph, cont });
    SelectOnly(g, -1);
    g.fitRequested = true;
}

void DestroyNodeGraph(NodeGraph& g) {
    if (g.texture) glDeleteTextures(1, &g.texture);
    g = NodeGraph{};
}

// ============================================================
// Evaluation
// ============================================================

static cv::Mat ToGray(const cv::Mat& m) {
    if (m.channels() == 1) return m;
    cv::Mat g;
    cv::cvtColor(m, g, m.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
    return g;
}

static cv::Mat ToBGR(const cv::Mat& m) {
    cv::Mat c;
    if (m.channels() == 3) c = m.clone();
    else cv::cvtColor(m, c, m.channels() == 4 ? cv::COLOR_BGRA2BGR : cv::COLOR_GRAY2BGR);
    return c;
}

// Make `b` the same size and channel count as `a`.
static cv::Mat MatchTo(const cv::Mat& b, const cv::Mat& a, int interp = cv::INTER_LINEAR) {
    cv::Mat r = b;
    if (r.size() != a.size()) cv::resize(r, r, a.size(), 0, 0, interp);
    if (r.channels() != a.channels()) r = a.channels() == 1 ? ToGray(r) : ToBGR(r);
    return r;
}

struct Style { int thick; double font; };

static void Label(cv::Mat& out, const std::string& text, cv::Point org, const cv::Scalar& color,
                  const Style& st, bool centered = false) {
    int base = 0;
    cv::Size ts = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, st.font, st.thick, &base);
    if (centered) org.x -= ts.width / 2;
    cv::putText(out, text, org, cv::FONT_HERSHEY_SIMPLEX, st.font, cv::Scalar(0, 0, 0), st.thick + 2);
    cv::putText(out, text, org, cv::FONT_HERSHEY_SIMPLEX, st.font, color, st.thick);
}

static std::string Plural(int n, const char* word) {
    return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
}

static std::string ClassifyShape(const std::vector<cv::Point>& c, const std::vector<cv::Point>& approx) {
    size_t v = approx.size();
    if (v == 3) return "Triangle";
    if (v == 4) {
        cv::RotatedRect rr = cv::minAreaRect(approx);
        float ar = std::max(rr.size.width, rr.size.height) /
                   std::max(1.0f, std::min(rr.size.width, rr.size.height));
        return ar < 1.15f ? "Square" : "Rectangle";
    }
    if (v == 5) return "Pentagon";
    if (v == 6) return "Hexagon";
    double area = cv::contourArea(c), peri = cv::arcLength(c, true);
    if (peri > 0 && 4.0 * CV_PI * area / (peri * peri) > 0.78) return "Circle";
    return "Polygon";
}

static cv::Mat Run(GraphNode& n, const cv::Mat in[2], const cv::Mat& frame) {
    auto F = [&](int i) { return n.params[i]; };
    auto I = [&](int i) { return (int)std::lround(n.params[i]); };
    NodeState& state = *n.state;
    cv::Mat out;

    // Detection nodes draw on "Draw on" if connected, else on the detected image itself.
    auto Canvas = [&]() {
        cv::Mat c = ToBGR(in[1].empty() ? in[0] : in[1]);
        if (c.size() != in[0].size()) cv::resize(c, c, in[0].size());
        return c;
    };
    Style st;
    st.thick = std::max(1, (int)std::lround(in[0].cols / 640.0));
    st.font = 0.5 * st.thick;

    switch (n.type) {
    case NT_CAMERA: {
        if (frame.empty()) { n.error = "No camera frame"; return {}; }
        if (F(0) < 0.999f) cv::resize(frame, out, cv::Size(), F(0), F(0), cv::INTER_AREA);
        else out = frame.clone();
        return out;
    }
    case NT_OUTPUT:
        return in[0];

    // ---- Color ----
    case NT_GRAY:
        return ToGray(in[0]);
    case NT_HSV_RANGE: {
        if (in[0].channels() == 1) { n.error = "Needs a color image"; return {}; }
        cv::Mat hsv;
        cv::cvtColor(ToBGR(in[0]), hsv, cv::COLOR_BGR2HSV);
        if (I(0) <= I(1)) {
            cv::inRange(hsv, cv::Scalar(I(0), I(2), I(4)), cv::Scalar(I(1), I(3), I(5)), out);
        } else { // hue wrap-around
            cv::Mat a, b;
            cv::inRange(hsv, cv::Scalar(I(0), I(2), I(4)), cv::Scalar(179, I(3), I(5)), a);
            cv::inRange(hsv, cv::Scalar(0, I(2), I(4)), cv::Scalar(I(1), I(3), I(5)), b);
            out = a | b;
        }
        char buf[48];
        snprintf(buf, sizeof(buf), "%.1f%% of pixels", 100.0 * cv::countNonZero(out) / out.total());
        n.info = buf;
        return out;
    }
    case NT_BRIGHTNESS: {
        in[0].convertTo(out, -1, F(1), F(0));
        if (std::fabs(F(2) - 1.0f) > 0.01f) {
            cv::Mat lut(1, 256, CV_8U);
            for (int i = 0; i < 256; ++i)
                lut.at<uchar>(i) = cv::saturate_cast<uchar>(std::pow(i / 255.0, F(2)) * 255.0);
            cv::LUT(out, lut, out);
        }
        return out;
    }
    case NT_EQUALIZE: {
        if (in[0].channels() == 1) { cv::equalizeHist(in[0], out); return out; }
        cv::Mat ycc;
        cv::cvtColor(ToBGR(in[0]), ycc, cv::COLOR_BGR2YCrCb);
        std::vector<cv::Mat> ch;
        cv::split(ycc, ch);
        cv::equalizeHist(ch[0], ch[0]);
        cv::merge(ch, ycc);
        cv::cvtColor(ycc, out, cv::COLOR_YCrCb2BGR);
        return out;
    }
    case NT_CLAHE: {
        auto clahe = cv::createCLAHE(F(0), cv::Size(I(1), I(1)));
        if (in[0].channels() == 1) { clahe->apply(in[0], out); return out; }
        cv::Mat lab;
        cv::cvtColor(ToBGR(in[0]), lab, cv::COLOR_BGR2Lab);
        std::vector<cv::Mat> ch;
        cv::split(lab, ch);
        clahe->apply(ch[0], ch[0]);
        cv::merge(ch, lab);
        cv::cvtColor(lab, out, cv::COLOR_Lab2BGR);
        return out;
    }
    case NT_INVERT:
        cv::bitwise_not(in[0], out);
        return out;

    // ---- Filter ----
    case NT_GAUSSIAN:
        cv::GaussianBlur(in[0], out, cv::Size(I(0) | 1, I(0) | 1), F(1));
        return out;
    case NT_MEDIAN:
        cv::medianBlur(in[0], out, std::max(3, I(0) | 1));
        return out;
    case NT_BILATERAL:
        cv::bilateralFilter(in[0].channels() == 4 ? ToBGR(in[0]) : in[0], out, I(0), F(1), F(2));
        return out;
    case NT_SHARPEN: {
        cv::Mat blurred;
        cv::GaussianBlur(in[0], blurred, cv::Size(0, 0), 3);
        cv::addWeighted(in[0], 1.0 + F(0), blurred, -F(0), 0, out);
        return out;
    }
    case NT_RESIZE:
        cv::resize(in[0], out, cv::Size(), F(0), F(0), F(0) < 1 ? cv::INTER_AREA : cv::INTER_LINEAR);
        return out;

    // ---- Threshold ----
    case NT_THRESHOLD: {
        static const int types[] = { cv::THRESH_BINARY, cv::THRESH_BINARY_INV,
                                     cv::THRESH_BINARY | cv::THRESH_OTSU, cv::THRESH_BINARY_INV | cv::THRESH_OTSU,
                                     cv::THRESH_TRUNC, cv::THRESH_TOZERO };
        double t = cv::threshold(ToGray(in[0]), out, I(1), 255, types[std::clamp(I(0), 0, 5)]);
        if (I(0) == 2 || I(0) == 3) n.info = "Otsu value: " + std::to_string((int)t);
        return out;
    }
    case NT_ADAPTIVE:
        cv::adaptiveThreshold(ToGray(in[0]), out, 255,
                              I(0) ? cv::ADAPTIVE_THRESH_GAUSSIAN_C : cv::ADAPTIVE_THRESH_MEAN_C,
                              I(3) ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY, std::max(3, I(1) | 1), I(2));
        return out;

    // ---- Edges ----
    case NT_CANNY: {
        static const int ap[] = { 3, 5, 7 };
        cv::Canny(ToGray(in[0]), out, I(0), I(1), ap[std::clamp(I(2), 0, 2)], I(3) != 0);
        return out;
    }
    case NT_SOBEL: {
        static const int ks[] = { 1, 3, 5, 7 };
        int k = ks[std::clamp(I(0), 0, 3)];
        cv::Mat gray = ToGray(in[0]), gx, gy;
        if (I(1) != 2) { cv::Sobel(gray, gx, CV_16S, 1, 0, k); cv::convertScaleAbs(gx, gx); }
        if (I(1) != 1) { cv::Sobel(gray, gy, CV_16S, 0, 1, k); cv::convertScaleAbs(gy, gy); }
        if (I(1) == 1) return gx;
        if (I(1) == 2) return gy;
        cv::addWeighted(gx, 0.5, gy, 0.5, 0, out);
        return out;
    }
    case NT_LAPLACIAN: {
        static const int ks[] = { 1, 3, 5, 7 };
        cv::Mat lap;
        cv::Laplacian(ToGray(in[0]), lap, CV_16S, ks[std::clamp(I(0), 0, 3)]);
        cv::convertScaleAbs(lap, out);
        return out;
    }

    // ---- Morphology ----
    case NT_MORPH: {
        static const int ops[] = { cv::MORPH_ERODE, cv::MORPH_DILATE, cv::MORPH_OPEN, cv::MORPH_CLOSE,
                                   cv::MORPH_GRADIENT, cv::MORPH_TOPHAT, cv::MORPH_BLACKHAT };
        static const int shapes[] = { cv::MORPH_RECT, cv::MORPH_CROSS, cv::MORPH_ELLIPSE };
        int k = I(2) | 1;
        cv::Mat kernel = cv::getStructuringElement(shapes[std::clamp(I(1), 0, 2)], cv::Size(k, k));
        cv::morphologyEx(in[0], out, ops[std::clamp(I(0), 0, 6)], kernel, cv::Point(-1, -1), I(3));
        return out;
    }

    // ---- Combine ----
    case NT_APPLY_MASK: {
        if (in[1].empty()) { n.error = "Connect a Mask"; return in[0]; }
        cv::Mat mask = ToGray(in[1]);
        if (mask.size() != in[0].size()) cv::resize(mask, mask, in[0].size(), 0, 0, cv::INTER_NEAREST);
        out = cv::Mat::zeros(in[0].size(), in[0].type());
        in[0].copyTo(out, mask);
        return out;
    }
    case NT_BLEND:
        if (in[1].empty()) { n.error = "Connect input B"; return in[0]; }
        cv::addWeighted(in[0], 1.0 - F(0), MatchTo(in[1], in[0]), F(0), 0, out);
        return out;
    case NT_BITWISE: {
        if (in[1].empty()) { n.error = "Connect input B"; return in[0]; }
        cv::Mat b = MatchTo(in[1], in[0], cv::INTER_NEAREST);
        if (I(0) == 0) cv::bitwise_and(in[0], b, out);
        else if (I(0) == 1) cv::bitwise_or(in[0], b, out);
        else cv::bitwise_xor(in[0], b, out);
        return out;
    }

    // ---- Detection ----
    case NT_CONTOURS: {
        cv::Mat bin;
        if (in[0].channels() != 1) { // not a mask: find edges first
            cv::Canny(ToGray(in[0]), bin, 50, 150);
            cv::dilate(bin, bin, cv::Mat());
        } else {
            cv::threshold(in[0], bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        }
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(bin, contours, I(2) ? cv::RETR_EXTERNAL : cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
        out = Canvas();
        int count = 0;
        for (const auto& c : contours) {
            if (cv::contourArea(c) < F(0)) continue;
            ++count;
            std::vector<cv::Point> approx;
            cv::approxPolyDP(c, approx, F(1) / 100.0 * cv::arcLength(c, true), true);
            std::string name = ClassifyShape(c, approx);
            cv::Rect box = cv::boundingRect(c);
            if (I(3)) cv::polylines(out, approx, true, cv::Scalar(0, 255, 0), st.thick);
            if (I(4)) cv::rectangle(out, box, cv::Scalar(255, 128, 0), st.thick);
            if (I(5)) {
                cv::Moments m = cv::moments(c);
                cv::Point center = m.m00 != 0 ? cv::Point((int)(m.m10 / m.m00), (int)(m.m01 / m.m00))
                                              : (box.tl() + box.br()) / 2;
                Label(out, name, center, cv::Scalar(0, 255, 255), st, true);
            }
        }
        n.info = Plural(count, "shape");
        return out;
    }
    case NT_HOUGH_CIRCLES: {
        cv::Mat g;
        cv::medianBlur(ToGray(in[0]), g, 5);
        std::vector<cv::Vec3f> circles;
        cv::HoughCircles(g, circles, cv::HOUGH_GRADIENT, F(0), std::max(1, I(1)), I(2), I(3), I(4), I(5));
        out = Canvas();
        for (const auto& c : circles) {
            cv::Point center(cvRound(c[0]), cvRound(c[1]));
            cv::circle(out, center, cvRound(c[2]), cv::Scalar(255, 0, 255), st.thick);
            cv::circle(out, center, st.thick * 2, cv::Scalar(0, 0, 255), cv::FILLED);
        }
        n.info = Plural((int)circles.size(), "circle");
        return out;
    }
    case NT_HOUGH_LINES: {
        cv::Mat edges;
        if (in[0].channels() != 1) cv::Canny(ToGray(in[0]), edges, 50, 150);
        else edges = in[0];
        std::vector<cv::Vec4i> lines;
        cv::HoughLinesP(edges, lines, 1, CV_PI / 180, I(0), I(1), I(2));
        out = Canvas();
        for (const auto& l : lines)
            cv::line(out, cv::Point(l[0], l[1]), cv::Point(l[2], l[3]), cv::Scalar(0, 0, 255), st.thick);
        n.info = Plural((int)lines.size(), "line");
        return out;
    }
    case NT_CORNERS: {
        std::vector<cv::Point2f> corners;
        cv::goodFeaturesToTrack(ToGray(in[0]), corners, I(0), F(1), I(2));
        out = Canvas();
        for (const auto& p : corners) cv::circle(out, p, st.thick * 3, cv::Scalar(0, 255, 255), st.thick);
        n.info = Plural((int)corners.size(), "corner");
        return out;
    }
    case NT_KEYPOINTS: {
        cv::Mat gray = ToGray(in[0]);
        std::vector<cv::KeyPoint> kps;
        if (I(0) == 0)      cv::ORB::create(I(1))->detect(gray, kps);
        else if (I(0) == 1) cv::FastFeatureDetector::create(I(2))->detect(gray, kps);
        else                cv::SIFT::create(I(1))->detect(gray, kps);
        if ((int)kps.size() > I(1)) cv::KeyPointsFilter::retainBest(kps, I(1));
        out = Canvas();
        for (const auto& k : kps) cv::circle(out, k.pt, std::max(2, st.thick * 2), cv::Scalar(0, 255, 128), 1);
        n.info = Plural((int)kps.size(), "keypoint");
        return out;
    }
    case NT_HAAR: {
        int type = std::clamp(I(0), 0, CASCADE_COUNT - 1);
        if (state.cascadeType != type) {
            state.cascadeType = type;
            std::string path = FindCascadeFile(type);
            state.cascadeOk = !path.empty() && state.cascade.load(path);
        }
        if (!state.cascadeOk) { n.error = "Cascade file not found (set OPENCV_CASCADE_DIR)"; return Canvas(); }
        cv::Mat gray;
        cv::equalizeHist(ToGray(in[0]), gray);
        std::vector<cv::Rect> objects;
        state.cascade.detectMultiScale(gray, objects, std::max(1.01f, F(1)), I(2), 0, cv::Size(I(3), I(3)));
        out = Canvas();
        static const char* names[] = { "Face", "Profile", "Smile", "Upper body", "Body", "Cat", "Plate" };
        for (const auto& r : objects) {
            cv::rectangle(out, r, cv::Scalar(0, 255, 0), st.thick);
            Label(out, names[type], cv::Point(r.x, r.y - st.thick * 3), cv::Scalar(0, 255, 0), st);
        }
        n.info = Plural((int)objects.size(), "object");
        return out;
    }
    case NT_QR: {
        static cv::QRCodeDetector qr;
        std::vector<std::string> texts;
        std::vector<cv::Point2f> pts;
        out = Canvas();
        if (qr.detectAndDecodeMulti(ToGray(in[0]), texts, pts) && !pts.empty()) {
            for (size_t i = 0; i < texts.size() && i * 4 + 3 < pts.size(); ++i) {
                std::vector<cv::Point> poly;
                for (int k = 0; k < 4; ++k) poly.emplace_back(pts[i * 4 + k]);
                cv::polylines(out, poly, true, cv::Scalar(255, 0, 0), st.thick);
                std::string text = texts[i].empty() ? "(undecoded)" : texts[i];
                if (text.size() > 40) text = text.substr(0, 37) + "...";
                Label(out, text, poly[0] - cv::Point(0, st.thick * 4), cv::Scalar(255, 128, 0), st);
                if (i == 0) n.info = text;
            }
        }
        if (n.info.empty()) n.info = "No QR code";
        return out;
    }
    case NT_ARUCO: {
#ifdef HAVE_ARUCO
        static const int dicts[] = { cv::aruco::DICT_4X4_50, cv::aruco::DICT_5X5_100, cv::aruco::DICT_6X6_250,
                                     cv::aruco::DICT_7X7_1000, cv::aruco::DICT_ARUCO_ORIGINAL,
                                     cv::aruco::DICT_APRILTAG_36h11 };
        int d = std::clamp(I(0), 0, 5);
        cv::aruco::ArucoDetector detector(cv::aruco::getPredefinedDictionary(dicts[d]),
                                          cv::aruco::DetectorParameters());
        std::vector<std::vector<cv::Point2f>> corners;
        std::vector<int> ids;
        detector.detectMarkers(ToGray(in[0]), corners, ids);
        out = Canvas();
        std::string idList;
        for (size_t i = 0; i < ids.size(); ++i) {
            std::vector<cv::Point> poly(corners[i].begin(), corners[i].end());
            cv::polylines(out, poly, true, cv::Scalar(0, 255, 0), st.thick);
            cv::circle(out, poly[0], st.thick * 3, cv::Scalar(0, 0, 255), cv::FILLED);
            cv::Point2f c = (corners[i][0] + corners[i][1] + corners[i][2] + corners[i][3]) * 0.25f;
            Label(out, "id " + std::to_string(ids[i]), c, cv::Scalar(0, 255, 255), st, true);
            idList += " " + std::to_string(ids[i]);
        }
        n.info = Plural((int)ids.size(), "marker") + (idList.empty() ? "" : ":" + idList);
        return out;
#else
        n.error = "ArUco needs OpenCV 4.7+";
        return Canvas();
#endif
    }
    case NT_MOTION: {
        if (!state.mog || state.mogHistory != I(0) || state.mogShadows != (I(2) != 0) ||
            state.mogSize != in[0].size()) {
            state.mog = cv::createBackgroundSubtractorMOG2(I(0), F(1), I(2) != 0);
            state.mogHistory = I(0);
            state.mogShadows = I(2) != 0;
            state.mogSize = in[0].size();
            state.mogWarmup = 0;
        }
        state.mog->setVarThreshold(F(1));
        cv::Mat fg;
        state.mog->apply(in[0], fg);
        if (state.mogWarmup < 15) { // an empty model flags everything as motion; let it learn first
            ++state.mogWarmup;
            n.info = "Learning background...";
            return cv::Mat::zeros(fg.size(), CV_8U);
        }
        cv::threshold(fg, out, 200, 255, cv::THRESH_BINARY); // drop shadows (127)
        cv::morphologyEx(out, out, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3)));
        char buf[48];
        snprintf(buf, sizeof(buf), "%.1f%% in motion", 100.0 * cv::countNonZero(out) / out.total());
        n.info = buf;
        return out;
    }
    }
    return {};
}

static void Evaluate(NodeGraph& g, GraphNode& n, const cv::Mat& frame) {
    if (n.evaluated) return;
    n.evaluated = true; // set first: also guards against cycles
    n.result.release();
    n.info.clear();
    n.error.clear();
    n.ms = 0.0;

    const NodeDef& d = Defs()[n.type];
    cv::Mat in[2];
    for (int i = 0; i < d.numInputs; ++i) {
        if (GraphNode* src = FindNode(g, n.inputs[i])) {
            Evaluate(g, *src, frame);
            in[i] = src->result;
        }
    }
    if (d.numInputs > 0 && in[0].empty()) {
        n.error = n.inputs[0] < 0 ? std::string("Connect ") + d.inputNames[0] : "Input has no image";
        return;
    }

    int64 t0 = cv::getTickCount();
    try {
        n.result = Run(n, in, frame);
    } catch (const cv::Exception& e) {
        n.error = e.err.empty() ? "OpenCV error" : e.err;
        n.result.release();
    }
    n.ms = (cv::getTickCount() - t0) * 1000.0 / cv::getTickFrequency();
}

void EvaluateNodeGraph(NodeGraph& g, const cv::Mat& frame) {
    int64 t0 = cv::getTickCount();
    for (auto& n : g.nodes) n->evaluated = false;

    GraphNode* output = nullptr;
    for (auto& n : g.nodes) if (n->type == NT_OUTPUT) output = n.get();
    if (output) Evaluate(g, *output, frame);

    GraphNode* sel = FindNode(g, g.selected);
    if (g.previewSelected && sel) Evaluate(g, *sel, frame);

    // Nodes that don't feed the Output aren't run (saves time)
    for (auto& n : g.nodes) {
        if (n->evaluated) continue;
        n->result.release();
        n->error.clear();
        n->info = "Not connected to Output";
        n->ms = 0.0;
    }
    g.totalMs = (cv::getTickCount() - t0) * 1000.0 / cv::getTickFrequency();

    const cv::Mat& shown = (g.previewSelected && sel && !sel->result.empty()) ? sel->result
                         : output ? output->result : cv::Mat();
    if (shown.empty()) {
        g.texW = g.texH = 0;
        return;
    }
    cv::Mat img = shown;
    if (img.depth() != CV_8U) cv::convertScaleAbs(img, img);
    UploadImageTexture(g.texture, img);
    g.texW = img.cols;
    g.texH = img.rows;
}


// ============================================================
// Shared UI helpers
// ============================================================

using namespace Theme;

static float U() { return ImGui::GetFontSize() / 15.0f; } // 1 at the default font size

static ImVec2 TextSizeF(ImFont* font, const char* text) {
    ImGui::PushFont(font);
    ImVec2 s = ImGui::CalcTextSize(text);
    ImGui::PopFont();
    return s;
}

static void DrawTextF(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 col, const char* text) {
    ImGui::PushFont(font);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, col, text);
    ImGui::PopFont();
}

static std::string Upper(const char* s) {
    std::string r(s);
    for (char& c : r) c = (char)toupper((unsigned char)c);
    return r;
}

// ============================================================
// Palette
// ============================================================

void DrawNodePalette(NodeGraph& g) {
    const float u = U();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Search field with a magnifier inside it
    static char search[64] = "";
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30 * u, 7 * u));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search nodes", search, sizeof(search));
    ImGui::PopStyleVar();
    {
        ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
        ImVec2 is = TextSizeF(Font::Small(), ICON_SEARCH);
        DrawTextF(dl, Font::Small(), ImVec2(r0.x + 10 * u, (r0.y + r1.y - is.y) * 0.5f), U32(TextFaint), ICON_SEARCH);
    }
    ImGuiTextFilter filter(search);
    ImGui::Dummy(ImVec2(0, 2 * u));

    const auto& defs = Defs();
    bool anyShown = false;
    for (const char* cat : kCategories) {
        std::vector<int> types;
        for (int t = 0; t < NT_COUNT; ++t)
            if (t != NT_OUTPUT && !strcmp(defs[t].category, cat) && filter.PassFilter(defs[t].name)) types.push_back(t);
        if (types.empty()) continue;
        anyShown = true;

        // Category header: colored icon + small caps label
        ImGui::Dummy(ImVec2(0, 4 * u));
        ImGui::PushFont(Font::Small());
        ImGui::TextColored(CategoryColorV(cat), "%s", CategoryIcon(cat));
        ImGui::SameLine(0, 6 * u);
        ImGui::TextColored(TextFaint, "%s", Upper(cat).c_str());
        ImGui::PopFont();

        for (int t : types) {
            const NodeDef& d = defs[t];
            ImGui::PushID(t);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 size(ImGui::GetContentRegionAvail().x, 28 * u);
            ImGui::InvisibleButton("##row", size);
            const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
            if (hovered || active)
                dl->AddRectFilled(p, p + size, U32(active ? SurfaceActive : SurfaceHover), 6 * u);
            dl->AddCircleFilled(ImVec2(p.x + 12 * u, p.y + size.y * 0.5f), 3.5f * u, CategoryColor(cat));
            float ty = p.y + (size.y - ImGui::GetFontSize()) * 0.5f;
            dl->AddText(ImVec2(p.x + 24 * u, ty), U32(hovered ? Text : Hex(0xC9CED6)), d.name);
            if (hovered) {
                ImVec2 ps = TextSizeF(Font::Small(), ICON_PLUS);
                DrawTextF(dl, Font::Small(), ImVec2(p.x + size.x - ps.x - 10 * u, p.y + (size.y - ps.y) * 0.5f),
                          U32(TextFaint), ICON_PLUS);
            }

            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("TULPAR_NODE", &t, sizeof(int));
                ImGui::TextColored(CategoryColorV(cat), "%s", CategoryIcon(cat));
                ImGui::SameLine();
                ImGui::TextUnformatted(d.name);
                ImGui::EndDragDropSource();
            } else if (hovered) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    AddNode(g, t, g.canvasCenter - ImVec2(110, 50));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                    ImGui::PushFont(Font::SemiBold());
                    ImGui::TextUnformatted(d.name);
                    ImGui::PopFont();
                    ImGui::TextColored(TextMuted, "%s", d.help);
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
            ImGui::PopID();
        }
    }
    if (!anyShown) ImGui::TextColored(TextMuted, "No node matches \"%s\"", search);

    ImGui::Dummy(ImVec2(0, 8 * u));
    ImGui::PushFont(Font::Small());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(TextFaint, "Drag a node into the editor, or double-click it.");
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
}

// ============================================================
// Editor
// ============================================================

// Shortcut labels: ImGui maps Cmd/Option to Ctrl/Alt on macOS
#ifdef __APPLE__
#define KEY_MOD "Cmd"
#define KEY_ALT "Option"
#else
#define KEY_MOD "Ctrl"
#define KEY_ALT "Alt"
#endif

// Node geometry in canvas units (1 canvas unit = 1 UI point at 100% zoom)
static const float kNodeContentW = 186.0f;
static const float kNodePad      = 10.0f;
static const float kPinR         = 5.0f;
static const float kGridStep     = 24.0f;
static const float kSnapStep     = 12.0f;
static const float kMinZoom      = 0.25f;
static const float kMaxZoom      = 2.0f;
static const float kLodZoom      = 0.5f;   // below this, nodes are drawn without their widgets

// Canvas <-> screen transform: screen = origin + canvas * zoom
struct View {
    ImVec2 origin;
    float  zoom;
    ImVec2 ToScreen(ImVec2 p) const { return origin + p * zoom; }
    ImVec2 ToCanvas(ImVec2 s) const { return (s - origin) / zoom; }
};

static ImVec2 OutputPin(const View& v, const GraphNode& n) { return v.ToScreen(n.pos + ImVec2(n.size.x, n.outPinY)); }
static ImVec2 InputPin(const View& v, const GraphNode& n, int i) { return v.ToScreen(n.pos + ImVec2(0, n.inPinY[i])); }

static void LinkControlPoints(ImVec2 a, ImVec2 b, float zoom, ImVec2& c1, ImVec2& c2) {
    float dx = std::max(40.0f * zoom, std::fabs(b.x - a.x) * 0.5f);
    c1 = a + ImVec2(dx, 0);
    c2 = b - ImVec2(dx, 0);
}

static void DrawLink(ImDrawList* dl, ImVec2 a, ImVec2 b, float zoom, ImU32 col, float thickness) {
    ImVec2 c1, c2;
    LinkControlPoints(a, b, zoom, c1, c2);
    dl->AddBezierCubic(a, c1, c2, b, col, thickness);
}

static float LinkDistanceSq(ImVec2 a, ImVec2 b, float zoom, ImVec2 p) {
    ImVec2 c1, c2;
    LinkControlPoints(a, b, zoom, c1, c2);
    ImVec2 q = ImBezierCubicClosestPointCasteljau(a, c1, c2, b, p, ImGui::GetStyle().CurveTessellationTol);
    return ImLengthSqr(q - p);
}

// Bounds of every node and group, in canvas units. False if the graph is empty.
static bool GraphBounds(const NodeGraph& g, ImVec2& mn, ImVec2& mx) {
    bool any = false;
    auto add = [&](ImVec2 a, ImVec2 b) {
        if (!any) { mn = a; mx = b; any = true; return; }
        mn = ImMin(mn, a);
        mx = ImMax(mx, b);
    };
    for (const auto& n : g.nodes) add(n->pos, n->pos + ImMax(n->size, ImVec2(1, 1)));
    for (const auto& gr : g.groups) if (gr.fitTo.empty()) add(gr.pos, gr.pos + gr.size);
    return any;
}

static void ZoomAround(NodeGraph& g, float newZoom, ImVec2 anchor /* relative to the canvas top-left */) {
    newZoom = std::clamp(newZoom, kMinZoom, kMaxZoom);
    ImVec2 canvasPt = (anchor - g.scroll) / g.zoom;
    g.zoom = newZoom;
    g.scroll = anchor - canvasPt * g.zoom;
}

static void FitView(NodeGraph& g, ImVec2 canvasSize) {
    ImVec2 mn, mx;
    if (!GraphBounds(g, mn, mx)) return;
    const float margin = 48.0f;
    ImVec2 ext = ImMax(mx - mn, ImVec2(1, 1));
    float z = std::min((canvasSize.x - margin * 2) / ext.x, (canvasSize.y - margin * 2) / ext.y);
    g.zoom = std::clamp(z, kMinZoom, 1.25f);
    g.scroll = canvasSize * 0.5f - (mn + mx) * 0.5f * g.zoom;
}

// Slider/checkbox/combo for one parameter, `width` wide, with the label inside the widget.
static void DrawParam(const ParamDef& p, float& v, float width) {
    std::string label;
    for (const char* c = p.label; *c; ++c) label += (*c == '%') ? "%%" : std::string(1, *c); // labels are format strings
    char fmt[96];
    ImGui::SetNextItemWidth(width);
    switch (p.kind) {
    case P_INT: {
        int x = (int)std::lround(v);
        snprintf(fmt, sizeof(fmt), "%s  %%d", label.c_str());
        if (ImGui::SliderInt("##p", &x, (int)p.min, (int)p.max, fmt)) v = (float)x;
        break;
    }
    case P_FLOAT:
        snprintf(fmt, sizeof(fmt), "%s  %%.2f", label.c_str());
        ImGui::SliderFloat("##p", &v, p.min, p.max, fmt);
        break;
    case P_LOG:
        snprintf(fmt, sizeof(fmt), p.max >= 100 ? "%s  %%.0f" : "%s  %%.3f", label.c_str());
        ImGui::SliderFloat("##p", &v, p.min, p.max, fmt, ImGuiSliderFlags_Logarithmic);
        break;
    case P_BOOL: {
        bool b = v > 0.5f;
        if (ImGui::Checkbox(p.label, &b)) v = b ? 1.0f : 0.0f;
        break;
    }
    case P_ODD: {
        int size = (int)std::lround(v) | 1;
        int k = (size - 1) / 2;
        snprintf(fmt, sizeof(fmt), "%s  %d \xC3\x97 %d", label.c_str(), size, size);
        if (ImGui::SliderInt("##p", &k, ((int)p.min - 1) / 2, ((int)p.max - 1) / 2, fmt)) v = (float)(k * 2 + 1);
        break;
    }
    case P_COMBO: {
        std::vector<const char*> items;
        for (const char* s = p.items; *s; s += strlen(s) + 1) items.push_back(s);
        int x = std::clamp((int)std::lround(v), 0, (int)items.size() - 1);
        std::string preview = std::string(p.label) + "  \xC2\xB7  " + items[x];
        if (ImGui::BeginCombo("##p", preview.c_str())) {
            for (int i = 0; i < (int)items.size(); ++i)
                if (ImGui::Selectable(items[i], i == x)) v = (float)i;
            ImGui::EndCombo();
        }
        break;
    }
    }
}

static void StartRename(NodeGraph& g, NodeGroup& gr) {
    g.renamingGroup = gr.id;
    g.renameFocus = true;
    snprintf(g.renameBuf, sizeof(g.renameBuf), "%s", gr.title.c_str());
}

static void GroupSelection(NodeGraph& g) {
    if (g.selection.empty()) return;
    int id = AddGroup(g, "Group " + std::to_string(g.nextGroupId), g.selection);
    if (NodeGroup* gr = FindGroup(g, id)) StartRename(g, *gr);
}

static void SelectAll(NodeGraph& g) {
    g.selection.clear();
    for (auto& n : g.nodes) g.selection.push_back(n->id);
    g.selected = g.selection.empty() ? -1 : g.selection.back();
}

static void MenuHeader(const char* icon, ImVec4 iconCol, const char* title) {
    ImGui::TextColored(iconCol, "%s", icon);
    ImGui::SameLine();
    ImGui::PushFont(Font::SemiBold());
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Separator();
}

static void DrawContextMenu(NodeGraph& g, ImVec2 canvasSize) {
    if (!ImGui::BeginPopup("NodeContext")) return;

    GraphNode* n = FindNode(g, g.contextNode);
    NodeGroup* gr = FindGroup(g, g.contextGroup);
    GraphNode* linkNode = FindNode(g, g.contextLinkNode);
    const bool multi = g.selection.size() > 1;

    if (n) {
        const NodeDef& d = Defs()[n->type];
        if (multi) MenuHeader(ICON_LIST, TextMuted, (std::to_string(g.selection.size()) + " nodes selected").c_str());
        else MenuHeader(NodeIcon(n->type, d.category), CategoryColorV(d.category), d.name);
        if (ImGui::MenuItem(ICON_COPY "  Duplicate", KEY_MOD "+D")) DuplicateSelection(g);
        if (ImGui::MenuItem(ICON_GROUP "  Group selection", KEY_MOD "+G")) GroupSelection(g);
        if (ImGui::MenuItem(ICON_XMARK "  Disconnect inputs"))
            for (int id : g.selection) if (GraphNode* s = FindNode(g, id)) s->inputs[0] = s->inputs[1] = -1;
        if (n->type != NT_OUTPUT || multi) {
            ImGui::PushStyleColor(ImGuiCol_Text, Danger);
            if (ImGui::MenuItem(ICON_TRASH "  Delete", "Del")) DeleteSelection(g);
            ImGui::PopStyleColor();
        }
    } else if (linkNode) {
        MenuHeader(ICON_WAVE, AccentHover, "Link");
        ImGui::PushStyleColor(ImGuiCol_Text, Danger);
        if (ImGui::MenuItem(ICON_XMARK "  Delete link", KEY_ALT "+Click")) linkNode->inputs[g.contextLinkInput] = -1;
        ImGui::PopStyleColor();
    } else if (gr) {
        MenuHeader(ICON_GROUP, kGroupColors[gr->color], gr->title.c_str());
        if (ImGui::MenuItem(ICON_PEN "  Rename", "Double-click")) StartRename(g, *gr);
        if (ImGui::BeginMenu(ICON_PALETTE "  Color")) {
            for (int i = 0; i < kGroupColorCount; ++i) {
                ImGui::PushID(i);
                ImGui::ColorButton("##c", kGroupColors[i], ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
                ImGui::SameLine();
                if (ImGui::MenuItem(kGroupColorNames[i], nullptr, gr->color == i)) gr->color = i;
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_LIST "  Select nodes inside")) {
            g.selection.clear();
            for (auto& m : g.nodes) {
                ImVec2 c = m->pos + m->size * 0.5f;
                if (c.x > gr->pos.x && c.y > gr->pos.y && c.x < gr->pos.x + gr->size.x && c.y < gr->pos.y + gr->size.y)
                    g.selection.push_back(m->id);
            }
            g.selected = g.selection.empty() ? -1 : g.selection.back();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Danger);
        if (ImGui::MenuItem(ICON_UNGROUP "  Remove group (keep nodes)")) DeleteGroup(g, gr->id);
        ImGui::PopStyleColor();
    } else {
        ImGui::PushFont(Font::Small());
        ImGui::TextColored(TextFaint, "ADD NODE");
        ImGui::PopFont();
        for (const char* cat : kCategories) {
            std::string label = std::string(CategoryIcon(cat)) + "  " + cat;
            if (!ImGui::BeginMenu(label.c_str())) continue;
            for (int t = 0; t < NT_COUNT; ++t)
                if (t != NT_OUTPUT && !strcmp(Defs()[t].category, cat) && ImGui::MenuItem(Defs()[t].name))
                    AddNode(g, t, g.contextPos);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_GROUP "  New group here")) {
            int id = AddGroup(g, "Group " + std::to_string(g.nextGroupId), {}, g.contextPos);
            if (NodeGroup* ng = FindGroup(g, id)) { ng->size = ImVec2(320, 200); StartRename(g, *ng); }
        }
        if (ImGui::MenuItem(ICON_LIST "  Select all", KEY_MOD "+A")) SelectAll(g);
        if (ImGui::MenuItem(ICON_EXPAND "  Fit view", "F")) FitView(g, canvasSize);
    }
    ImGui::EndPopup();
}

// Toolbar button; `on` draws it as an active toggle.
static bool ToolButton(const char* label, bool on = false, const char* tooltip = nullptr) {
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, Alpha(Accent, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Alpha(Accent, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Alpha(Accent, 0.36f));
        ImGui::PushStyleColor(ImGuiCol_Text, AccentHover);
    }
    bool pressed = ImGui::Button(label);
    if (on) ImGui::PopStyleColor(4);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

static float ButtonWidth(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
}

static void DrawToolbar(NodeGraph& g, ImVec2 canvasSize) {
    const float u = U();
    const float h = 44 * u;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    dl->AddLine(ImVec2(p.x, p.y + h - 1), ImVec2(p.x + w, p.y + h - 1), U32(Border));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * u, 5 * u));
    const float y = p.y + (h - ImGui::GetFrameHeight()) * 0.5f;

    // ---- Left: graph actions ----
    ImGui::SetCursorScreenPos(ImVec2(p.x + 10 * u, y));
    if (ToolButton(ICON_WAND "  Example", false, "Load an example: edge + shape detection, grouped")) LoadExample(g);
    ImGui::SameLine();
    if (ToolButton(ICON_ERASER "  Clear", false, "Start over with Camera \xE2\x86\x92 Output")) InitNodeGraph(g);
    ImGui::SameLine(0, 16 * u);
    if (ToolButton(ICON_EYE "  Preview", g.previewSelected, "Show the selected node's image in Pipeline Output"))
        g.previewSelected = !g.previewSelected;
    ImGui::SameLine();
    ImGui::BeginDisabled(g.selection.empty());
    if (ToolButton(ICON_GROUP "  Group", false, "Group the selected nodes (" KEY_MOD "+G)")) GroupSelection(g);
    ImGui::EndDisabled();
    const float leftEnd = ImGui::GetItemRectMax().x;

    // ---- Right: view controls ----
    char zoomLabel[16];
    snprintf(zoomLabel, sizeof(zoomLabel), "%d%%###zoom", (int)std::lround(g.zoom * 100));
    const char* labels[] = { "\xE2\x88\x92###zout", zoomLabel, ICON_PLUS "###zin", ICON_EXPAND "  Fit###fit",
                             ICON_MAGNET "###snap", ICON_MAP "###map" };
    float total = 0;
    for (const char* l : labels) total += ButtonWidth(l);
    total += gap * 2 + 2 * u * 3 + 16 * u * 2; // tight zoom cluster, wider gaps between clusters
    float x = p.x + w - 10 * u - total;
    if (x > leftEnd + 16 * u) {
        const ImVec2 center = canvasSize * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2 * u, 0));
        if (ToolButton(labels[0], false, "Zoom out (mouse wheel)")) ZoomAround(g, g.zoom / 1.2f, center);
        ImGui::SameLine();
        if (ToolButton(labels[1], false, "Reset zoom to 100% (" KEY_MOD "+0)")) ZoomAround(g, 1.0f, center);
        ImGui::SameLine();
        if (ToolButton(labels[2], false, "Zoom in (mouse wheel)")) ZoomAround(g, g.zoom * 1.2f, center);
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 16 * u);
        if (ToolButton(labels[3], false, "Fit the whole graph in view (F)")) FitView(g, canvasSize);
        ImGui::SameLine(0, 16 * u);
        if (ToolButton(labels[4], g.snap, "Snap nodes to the grid")) g.snap = !g.snap;
        ImGui::SameLine();
        if (ToolButton(labels[5], g.showMinimap, "Show the minimap")) g.showMinimap = !g.showMinimap;
    }
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
}

void DrawNodeEditor(NodeGraph& g) {
    ImGuiIO& io = ImGui::GetIO();
    const auto& defs = Defs();
    const float ui = U(); // unzoomed UI unit, for overlays

    // The canvas size is needed by the toolbar (fit / zoom about the center)
    const float toolbarH = 44 * ui;
    const ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, std::max(1.0f, ImGui::GetContentRegionAvail().y - toolbarH));
    DrawToolbar(g, canvasSize);

    // ---------- Canvas ----------
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Canvas);
    ImGui::BeginChild("NodeCanvas", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c0 = ImGui::GetCursorScreenPos();
    const ImVec2 csz = ImGui::GetContentRegionAvail();
    const ImVec2 c1 = c0 + csz;
    const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool editorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    // ---------- View input: fit, zoom, pan ----------
    if (g.fitRequested) {
        bool measured = !g.nodes.empty();
        for (auto& n : g.nodes) measured = measured && n->size.x > 0;
        for (auto& gr : g.groups) measured = measured && gr.fitTo.empty();
        if (measured) { FitView(g, csz); g.fitRequested = false; }
    }
    if (canvasHovered && !popupOpen && io.MouseWheel != 0.0f)
        ZoomAround(g, g.zoom * std::pow(1.12f, io.MouseWheel), io.MousePos - c0);

    if (canvasHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
        g.panning = true;
        g.panDistance = 0.0f;
    }
    if (g.panning) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            g.scroll += io.MouseDelta;
            g.panDistance += std::fabs(io.MouseDelta.x) + std::fabs(io.MouseDelta.y);
        } else {
            g.panning = false;
        }
    }

    // ---------- Drags started on earlier frames (applied before drawing, so nothing lags) ----------
    const ImVec2 delta = io.MouseDelta / g.zoom;
    const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (g.draggingNodes) {
        if (leftDown) {
            for (int id : g.selection) if (GraphNode* n = FindNode(g, id)) n->pos += delta;
        } else {
            g.draggingNodes = false;
            if (g.snap)
                for (int id : g.selection)
                    if (GraphNode* n = FindNode(g, id))
                        n->pos = ImVec2(std::round(n->pos.x / kSnapStep) * kSnapStep, std::round(n->pos.y / kSnapStep) * kSnapStep);
        }
    }
    if (g.draggingGroup >= 0) {
        NodeGroup* gr = FindGroup(g, g.draggingGroup);
        if (gr && leftDown) {
            gr->pos += delta;
            for (int id : g.groupMembers) if (GraphNode* n = FindNode(g, id)) n->pos += delta;
        } else {
            g.draggingGroup = -1;
        }
    }
    if (g.resizingGroup >= 0) {
        NodeGroup* gr = FindGroup(g, g.resizingGroup);
        if (gr && leftDown) gr->size = ImMax(gr->size + delta, ImVec2(160, 80));
        else g.resizingGroup = -1;
    }

    const View view{ c0 + g.scroll, g.zoom };
    g.canvasCenter = view.ToCanvas(c0 + csz * 0.5f);
    const ImVec2 mouseCanvas = view.ToCanvas(io.MousePos);
    const bool lod = g.zoom < kLodZoom;

    // ---------- Minimap hit area (submitted first so nodes under it can't take the click) ----------
    const ImVec2 mmSize(180 * ui, 120 * ui);
    const ImVec2 mm0 = c1 - mmSize - ImVec2(12 * ui, 12 * ui);
    ImVec2 worldMin, worldMax;
    bool haveBounds = GraphBounds(g, worldMin, worldMax);
    const bool showMinimap = g.showMinimap && haveBounds && csz.x > 420 * ui && csz.y > 260 * ui;
    float mmScale = 1.0f;
    ImVec2 mmOffset;
    if (showMinimap) {
        // World = graph + visible area, so the view rectangle is always on the map
        ImVec2 v0 = view.ToCanvas(c0), v1 = view.ToCanvas(c1);
        worldMin = ImMin(worldMin, v0) - ImVec2(40, 40);
        worldMax = ImMax(worldMax, v1) + ImVec2(40, 40);
        ImVec2 ext = worldMax - worldMin;
        mmScale = std::min((mmSize.x - 12 * ui) / ext.x, (mmSize.y - 12 * ui) / ext.y);
        mmOffset = mm0 + (mmSize - ext * mmScale) * 0.5f;
        ImGui::SetCursorScreenPos(mm0);
        ImGui::InvisibleButton("##minimap", mmSize);
        if (ImGui::IsItemActive()) { // drag on the map to move the view
            ImVec2 target = (io.MousePos - mmOffset) / mmScale + worldMin;
            g.scroll = csz * 0.5f - target * g.zoom;
        }
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("Click or drag to move the view");
    }
    g.minimapMin = mm0;
    g.minimapMax = mm0 + mmSize;
    const bool overMinimap = showMinimap && ImGui::IsMouseHoveringRect(mm0, mm0 + mmSize);

    // ---------- Grid ----------
    {
        float step = kGridStep * g.zoom;
        while (step < 12.0f * ui) step *= 2.0f; // keep the dot density sane when zoomed out
        const float dot = std::max(0.75f, 1.0f * ui * std::min(1.0f, g.zoom));
        for (float x = std::fmod(g.scroll.x, step); x < csz.x; x += step)
            for (float y = std::fmod(g.scroll.y, step); y < csz.y; y += step)
                dl->AddRectFilled(ImVec2(c0.x + x - dot, c0.y + y - dot), ImVec2(c0.x + x + dot, c0.y + y + dot),
                                  IM_COL32(255, 255, 255, 22));
    }
    // Hint along the bottom (unzoomed; drawn before the nodes so they cover it)
    {
        ImGui::PushFont(Font::Small());
        const char* hint = "Wheel zoom  \xC2\xB7  Right-drag pan  \xC2\xB7  Drag empty space to select  \xC2\xB7  "
                           KEY_MOD "+G group  \xC2\xB7  " KEY_MOD "+D duplicate  \xC2\xB7  " KEY_ALT "+click a link to cut it";
        ImVec2 hs = ImGui::CalcTextSize(hint);
        float room = csz.x - 24 * ui - (showMinimap ? mmSize.x + 12 * ui : 0.0f);
        if (hs.x < room) dl->AddText(ImVec2(c0.x + 14 * ui, c1.y - hs.y - 12 * ui), U32(TextFaint), hint);
        ImGui::PopFont();
    }

    // ---------- Everything below is drawn at the canvas zoom ----------
    ImGui::SetWindowFontScale(g.zoom);
    const float u = U(); // zoomed UI unit
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * u, 6 * u));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(6 * u, 6 * u));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * u, 4 * u));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5 * u);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 3 * u);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 1.5f * u);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Canvas);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, SurfaceHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, SurfaceActive);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, Alpha(Accent, 0.7f));

    // Topmost node under the mouse, from last frame's sizes
    int hoveredNode = -1;
    if (canvasHovered && !overMinimap)
        for (auto& n : g.nodes)
            if (mouseCanvas.x >= n->pos.x && mouseCanvas.y >= n->pos.y &&
                mouseCanvas.x <= n->pos.x + n->size.x && mouseCanvas.y <= n->pos.y + n->size.y)
                hoveredNode = n->id;

    // ---------- Groups (bottom layer) ----------
    int hoveredGroup = -1;
    for (auto& gr : g.groups) {
        if (!gr.fitTo.empty()) { // wrap the requested nodes once they've been measured
            bool ready = true, any = false;
            ImVec2 mn, mx;
            for (int id : gr.fitTo) {
                GraphNode* n = FindNode(g, id);
                if (!n) continue;
                if (n->size.x <= 0) { ready = false; break; }
                mn = any ? ImMin(mn, n->pos) : n->pos;
                mx = any ? ImMax(mx, n->pos + n->size) : n->pos + n->size;
                any = true;
            }
            if (!ready) continue;
            if (any) { gr.pos = mn - ImVec2(20, 52); gr.size = mx - mn + ImVec2(40, 72); }
            gr.fitTo.clear();
        }
        ImGui::PushID(gr.id + 100000);
        const ImVec4 col = kGroupColors[std::clamp(gr.color, 0, kGroupColorCount - 1)];
        const ImVec2 a = view.ToScreen(gr.pos), b = view.ToScreen(gr.pos + gr.size);
        const float headerH = 36 * u, r = 12 * u;
        dl->AddRectFilled(a, b, U32(col, 0.05f), r);
        dl->AddRectFilled(a, ImVec2(b.x, a.y + headerH), U32(col, 0.13f), r, ImDrawFlags_RoundCornersTop);
        dl->AddRect(a, b, U32(col, 0.35f), r, 0, 1.0f * u);

        // Header: drag to move the group and everything inside it; double-click to rename
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton("##header", ImMax(ImVec2(b.x - a.x, headerH), ImVec2(1, 1)), ImGuiButtonFlags_AllowItemOverlap);
        ImGui::SetItemAllowOverlap();
        if (ImGui::IsItemHovered() && hoveredNode < 0) hoveredGroup = gr.id;
        if (ImGui::IsItemActivated() && g.renamingGroup != gr.id) {
            g.draggingGroup = gr.id;
            g.groupMembers.clear();
            for (auto& n : g.nodes) {
                ImVec2 c = n->pos + n->size * 0.5f;
                if (c.x > gr.pos.x && c.y > gr.pos.y && c.x < gr.pos.x + gr.size.x && c.y < gr.pos.y + gr.size.y)
                    g.groupMembers.push_back(n->id);
            }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) StartRename(g, gr);

        if (g.renamingGroup == gr.id) {
            ImGui::SetCursorScreenPos(a + ImVec2(8 * u, (headerH - ImGui::GetFrameHeight()) * 0.5f));
            ImGui::SetNextItemWidth(std::max(60 * u, b.x - a.x - 16 * u));
            if (g.renameFocus) { ImGui::SetKeyboardFocusHere(); g.renameFocus = false; }
            ImGui::PushFont(Font::SemiBold());
            ImGui::InputText("##rename", g.renameBuf, sizeof(g.renameBuf), ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopFont();
            if (ImGui::IsItemDeactivated()) {
                if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && g.renameBuf[0]) gr.title = g.renameBuf;
                g.renamingGroup = -1;
            }
        } else {
            ImGui::PushFont(Font::SemiBold());
            const float fs = std::max(ImGui::GetFontSize(), 11.0f * ui); // stays readable when zoomed out
            ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, gr.title.c_str());
            dl->PushClipRect(a, ImVec2(b.x - 8 * u, std::max(a.y + headerH, a.y + ts.y + 4)), true);
            dl->AddText(ImGui::GetFont(), fs, a + ImVec2(14 * u, std::max(2.0f, (headerH - ts.y) * 0.5f)),
                        U32(ImLerp(col, ImVec4(1, 1, 1, 1), 0.35f)), gr.title.c_str());
            dl->PopClipRect();
            ImGui::PopFont();
        }

        // Resize grip (bottom-right corner)
        const float grip = 16 * u;
        ImGui::SetCursorScreenPos(b - ImVec2(grip, grip));
        ImGui::InvisibleButton("##resize", ImVec2(grip, grip), ImGuiButtonFlags_AllowItemOverlap);
        ImGui::SetItemAllowOverlap();
        if (ImGui::IsItemActivated()) g.resizingGroup = gr.id;
        bool gripHot = ImGui::IsItemHovered() || g.resizingGroup == gr.id;
        if (gripHot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        for (int i = 1; i <= 2; ++i)
            dl->AddLine(b - ImVec2(5 * u * i + 3 * u, 3 * u), b - ImVec2(3 * u, 5 * u * i + 3 * u),
                        U32(col, gripHot ? 0.9f : 0.45f), 1.5f * u);
        ImGui::PopID();
    }

    // ---------- Links (under the nodes), colored by the node they come from ----------
    const bool idle = !g.draggingNodes && g.draggingGroup < 0 && g.resizingGroup < 0 && !g.boxSelecting &&
                      g.dragFromOutput < 0 && g.dragToNode < 0;
    g.hoverLinkNode = g.hoverLinkInput = -1;
    if (canvasHovered && idle && hoveredNode < 0 && !overMinimap) {
        float best = (6 * ui) * (6 * ui);
        for (auto& up : g.nodes)
            for (int i = 0; i < defs[up->type].numInputs; ++i)
                if (const GraphNode* src = FindNode(g, up->inputs[i])) {
                    float d = LinkDistanceSq(OutputPin(view, *src), InputPin(view, *up, i), g.zoom, io.MousePos);
                    if (d < best) { best = d; g.hoverLinkNode = up->id; g.hoverLinkInput = i; }
                }
    }
    for (auto& up : g.nodes) {
        const GraphNode& n = *up;
        for (int i = 0; i < defs[n.type].numInputs; ++i) {
            const GraphNode* src = FindNode(g, n.inputs[i]);
            if (!src) continue;
            const bool hovered = g.hoverLinkNode == n.id && g.hoverLinkInput == i;
            const bool hl = hovered || IsSelected(g, n.id) || IsSelected(g, src->id);
            ImU32 col = hovered && io.KeyAlt ? U32(Danger) : CategoryColor(defs[src->type].category, hl ? 1.0f : 0.7f);
            DrawLink(dl, OutputPin(view, *src), InputPin(view, n, i), g.zoom, col,
                     (hovered ? 3.5f : (hl ? 3.0f : 2.0f)) * std::max(0.6f, u));
        }
    }
    if (g.hoverLinkNode >= 0 && io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        if (GraphNode* n = FindNode(g, g.hoverLinkNode)) n->inputs[g.hoverLinkInput] = -1;

    // ---------- Nodes ----------
    struct Pin { ImVec2 pos; int node; int input; }; // input = -1 for an output pin
    std::vector<Pin> pins;
    int toFront = -1;
    const float pad = kNodePad * u, contentW = kNodeContentW * u, pinR = std::max(3.0f, kPinR * u);

    for (auto& up : g.nodes) {
        GraphNode& n = *up;
        const NodeDef& d = defs[n.type];
        const ImVec4 catCol = CategoryColorV(d.category);
        const bool selected = IsSelected(g, n.id);
        ImGui::PushID(n.id);
        const ImVec2 p = view.ToScreen(n.pos);
        const ImVec2 size = ImMax(n.size * g.zoom, ImVec2(contentW + 2 * pad, 40 * u));
        const float r = 9 * u;

        // Whole-node button (submitted first, so the widgets on top of it get priority)
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton("##node", size, ImGuiButtonFlags_AllowItemOverlap);
        ImGui::SetItemAllowOverlap();
        if (ImGui::IsItemActivated()) {
            if (io.KeyShift || io.KeyCtrl) {
                ToggleSelected(g, n.id);
            } else {
                if (!selected) SelectOnly(g, n.id);
                else g.selected = n.id;
            }
            g.draggingNodes = IsSelected(g, n.id);
            toFront = n.id;
        }

        float titleH;
        if (lod && n.size.x > 0) { // (unmeasured nodes are always drawn in full once, to get their size)
            // Zoomed out: header + title only, keep the full-detail size and pin layout
            titleH = n.outPinY * 2.0f * g.zoom;
            const ImVec2 q = p + size;
            dl->AddRectFilled(p + ImVec2(0, 2 * u), q + ImVec2(0, 4 * u), IM_COL32(0, 0, 0, 90), r);
            dl->AddRectFilled(p, q, U32(Surface), r);
            dl->AddRectFilled(p, ImVec2(q.x, p.y + std::max(titleH, 6.0f)), U32(catCol, 0.35f), r, ImDrawFlags_RoundCornersTop);
            dl->AddRect(p, q, selected ? U32(Accent) : U32(hoveredNode == n.id ? BorderStrong : Border), r, 0,
                        selected ? 2.0f : 1.0f);
            // Title at a readable size, clipped to the node
            ImGui::PushFont(Font::SemiBold());
            const float fs = std::max(ImGui::GetFontSize(), 11.0f * ui);
            dl->PushClipRect(p, q, true);
            dl->AddText(ImGui::GetFont(), fs, p + ImVec2(pad, std::max(2.0f, (titleH - fs) * 0.5f)), U32(Text), d.name);
            dl->PopClipRect();
            ImGui::PopFont();
        } else {
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1); // foreground: widgets

            // ---- Header ----
            ImGui::SetCursorScreenPos(p + ImVec2(pad, pad * 0.75f));
            ImGui::BeginGroup();
            ImGui::TextColored(catCol, "%s", NodeIcon(n.type, d.category));
            ImGui::SameLine(0, 7 * u);
            ImGui::PushFont(Font::SemiBold());
            ImGui::TextUnformatted(d.name);
            ImGui::PopFont();
            titleH = ImGui::GetCursorScreenPos().y - p.y - ImGui::GetStyle().ItemSpacing.y + pad * 0.75f;
            ImGui::Dummy(ImVec2(0, 2 * u));

            // ---- Inputs ----
            for (int i = 0; i < d.numInputs; ++i) {
                n.inPinY[i] = (ImGui::GetCursorScreenPos().y - p.y + ImGui::GetTextLineHeight() * 0.5f) / g.zoom;
                bool optional = n.type >= NT_CONTOURS && i == 1;
                if (n.inputs[i] >= 0) ImGui::TextUnformatted(d.inputNames[i]);
                else ImGui::TextColored(TextMuted, optional ? "%s  (optional)" : "%s", d.inputNames[i]);
            }

            // ---- Parameters ----
            for (size_t k = 0; k < d.params.size(); ++k) {
                ImGui::PushID((int)k);
                DrawParam(d.params[k], n.params[k], contentW);
                ImGui::PopID();
            }

            // ---- Status line ----
            ImGui::PushFont(Font::Small());
            ImGui::PushTextWrapPos(p.x + pad + contentW);
            char buf[160];
            buf[0] = 0;
            if (!n.error.empty()) {
                ImGui::TextColored(Danger, "%s", n.error.c_str());
            } else if (n.type == NT_OUTPUT) {
                if (!n.result.empty())
                    snprintf(buf, sizeof(buf), "%d \xC3\x97 %d  \xE2\x86\x92  Pipeline Output", n.result.cols, n.result.rows);
            } else if (!n.result.empty() && n.info.empty()) {
                snprintf(buf, sizeof(buf), "%d \xC3\x97 %d  \xC2\xB7  %d ch  \xC2\xB7  %.1f ms",
                         n.result.cols, n.result.rows, n.result.channels(), n.ms);
            } else if (!n.info.empty()) {
                if (n.ms > 0) snprintf(buf, sizeof(buf), "%s  \xC2\xB7  %.1f ms", n.info.c_str(), n.ms);
                else snprintf(buf, sizeof(buf), "%s", n.info.c_str());
            }
            if (buf[0]) ImGui::TextColored(n.evaluated ? TextMuted : TextFaint, "%s", buf);
            ImGui::PopTextWrapPos();
            ImGui::PopFont();

            ImGui::Dummy(ImVec2(contentW, 0));
            ImGui::EndGroup();
            n.size = ImVec2(contentW + 2 * pad, ImGui::GetItemRectSize().y + pad * 1.75f) / g.zoom;
            n.outPinY = titleH * 0.5f / g.zoom;

            // ---- Background: shadow, body, header tint, border ----
            dl->ChannelsSetCurrent(0);
            const ImVec2 q = p + n.size * g.zoom;
            dl->AddRectFilled(p + ImVec2(0, 6 * u), q + ImVec2(0, 10 * u), IM_COL32(0, 0, 0, 50), r + 4 * u);
            dl->AddRectFilled(p + ImVec2(0, 2 * u), q + ImVec2(0, 4 * u), IM_COL32(0, 0, 0, 90), r);
            dl->AddRectFilled(p, q, U32(Surface), r);
            dl->AddRectFilled(p, ImVec2(q.x, p.y + titleH), U32(catCol, 0.13f), r, ImDrawFlags_RoundCornersTop);
            dl->AddLine(ImVec2(p.x, p.y + titleH), ImVec2(q.x, p.y + titleH), U32(Border));
            if (selected) {
                dl->AddRect(p - ImVec2(3 * u, 3 * u), q + ImVec2(3 * u, 3 * u), U32(Accent, 0.25f), r + 3 * u, 0, 3 * u);
                dl->AddRect(p, q, U32(Accent), r, 0, 1.5f * u);
            } else {
                dl->AddRect(p, q, U32(hoveredNode == n.id ? BorderStrong : Border), r, 0, 1.0f * u);
            }
            dl->ChannelsMerge();
        }

        // ---- Pins ----
        for (int i = 0; i < d.numInputs; ++i) {
            ImVec2 pc = InputPin(view, n, i);
            ImGui::PushID(100 + i);
            ImGui::SetCursorScreenPos(pc - ImVec2(pinR * 2, pinR * 2));
            ImGui::InvisibleButton("##in", ImVec2(pinR * 4, pinR * 4));
            if (ImGui::IsItemActivated()) {
                if (n.inputs[i] >= 0) { // pick up the existing link
                    g.dragFromOutput = n.inputs[i];
                    n.inputs[i] = -1;
                } else {
                    g.dragToNode = n.id;
                    g.dragToInput = i;
                }
            }
            bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            const GraphNode* src = FindNode(g, n.inputs[i]);
            float rr = pinR + (hov ? 1.5f * u : 0.0f);
            dl->AddCircleFilled(pc, rr + 2 * u, U32(Canvas));
            dl->AddCircleFilled(pc, rr, src ? CategoryColor(defs[src->type].category) : U32(Surface));
            dl->AddCircle(pc, rr, U32(src ? Canvas : (hov ? Text : TextFaint)), 0, 1.5f * u);
            pins.push_back({ pc, n.id, i });
        }
        if (n.type != NT_OUTPUT) {
            ImVec2 pc = OutputPin(view, n);
            ImGui::SetCursorScreenPos(pc - ImVec2(pinR * 2, pinR * 2));
            ImGui::InvisibleButton("##out", ImVec2(pinR * 4, pinR * 4));
            if (ImGui::IsItemActivated()) g.dragFromOutput = n.id;
            bool hov = ImGui::IsItemHovered();
            float rr = pinR + (hov ? 1.5f * u : 0.0f);
            dl->AddCircleFilled(pc, rr + 2 * u, U32(Canvas));
            dl->AddCircleFilled(pc, rr, U32(catCol));
            if (hov) dl->AddCircle(pc, rr, U32(Text), 0, 1.5f * u);
            pins.push_back({ pc, n.id, -1 });
        }
        ImGui::PopID();
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(6);

    // ---------- Link being dragged ----------
    if (g.dragFromOutput >= 0 || g.dragToNode >= 0) {
        GraphNode* from = FindNode(g, g.dragFromOutput);
        GraphNode* to = FindNode(g, g.dragToNode);
        if (from)    DrawLink(dl, OutputPin(view, *from), io.MousePos, g.zoom, U32(Accent), 2.5f * std::max(0.6f, u));
        else if (to) DrawLink(dl, io.MousePos, InputPin(view, *to, g.dragToInput), g.zoom, U32(Accent), 2.5f * std::max(0.6f, u));

        if (!leftDown) { // released: connect to the pin under the mouse
            const float hit = std::max(pinR * 3, 10 * ui);
            for (const Pin& pin : pins) {
                ImVec2 dd = pin.pos - io.MousePos;
                if (dd.x * dd.x + dd.y * dd.y > hit * hit) continue;
                if (from && pin.input >= 0)  Connect(g, from->id, pin.node, pin.input);
                if (to && pin.input < 0)     Connect(g, pin.node, to->id, g.dragToInput);
                break;
            }
            g.dragFromOutput = g.dragToNode = g.dragToInput = -1;
        }
    }

    // Bring the clicked node to the front
    if (toFront >= 0) {
        auto it = std::find_if(g.nodes.begin(), g.nodes.end(),
                               [&](const std::unique_ptr<GraphNode>& p) { return p->id == toFront; });
        if (it != g.nodes.end()) std::rotate(it, it + 1, g.nodes.end());
    }

    // ---------- Box selection (left-drag on empty canvas) ----------
    if (canvasHovered && !popupOpen && hoveredNode < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered() && !io.KeyAlt) {
        g.boxSelecting = true;
        g.boxStart = mouseCanvas;
        if (!io.KeyShift && !io.KeyCtrl) SelectOnly(g, -1);
    }
    if (g.boxSelecting) {
        const ImVec2 a = ImMin(g.boxStart, mouseCanvas), b = ImMax(g.boxStart, mouseCanvas);
        const ImVec2 sa = view.ToScreen(a), sb = view.ToScreen(b);
        dl->AddRectFilled(sa, sb, U32(Accent, 0.10f), 3 * ui);
        dl->AddRect(sa, sb, U32(Accent, 0.8f), 3 * ui, 0, 1.0f);
        if (!leftDown) {
            for (auto& n : g.nodes)
                if (n->pos.x < b.x && n->pos.y < b.y && n->pos.x + n->size.x > a.x && n->pos.y + n->size.y > a.y &&
                    !IsSelected(g, n->id)) {
                    g.selection.push_back(n->id);
                    g.selected = n->id;
                }
            g.boxSelecting = false;
        }
    }

    // ---------- Context menu ----------
    if (canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && g.panDistance < 4.0f && !overMinimap) {
        g.contextNode = hoveredNode;
        g.contextGroup = hoveredNode < 0 && g.hoverLinkNode < 0 ? hoveredGroup : -1;
        g.contextLinkNode = hoveredNode < 0 ? g.hoverLinkNode : -1;
        g.contextLinkInput = g.hoverLinkInput;
        g.contextPos = mouseCanvas;
        if (hoveredNode >= 0 && !IsSelected(g, hoveredNode)) SelectOnly(g, hoveredNode);
        ImGui::OpenPopup("NodeContext");
    }

    // ---------- Overlays and popups are drawn unzoomed ----------
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * ui, 10 * ui));
    DrawContextMenu(g, csz);
    ImGui::PopStyleVar();

    // ---------- Keyboard shortcuts ----------
    if (editorFocused && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) DeleteSelection(g);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) DuplicateSelection(g);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) SelectAll(g);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G)) GroupSelection(g);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0)) ZoomAround(g, 1.0f, csz * 0.5f);
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) FitView(g, csz);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) SelectOnly(g, -1);
    }

    // Drop target for nodes dragged from the palette
    if (ImGui::BeginDragDropTargetCustom(ImRect(c0, c1), ImGui::GetID("NodeDrop"))) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TULPAR_NODE")) {
            int type = *(const int*)payload->Data;
            if (type >= 0 && type < NT_COUNT)
                AddNode(g, type, mouseCanvas - ImVec2((kNodeContentW + 2 * kNodePad) * 0.5f, 14));
        }
        ImGui::EndDragDropTarget();
    }

    // Minimap
    if (showMinimap) {
        const ImVec2 mm1 = mm0 + mmSize;
        dl->AddRectFilled(mm0, mm1, U32(Chrome, 0.88f), 8 * ui);
        dl->AddRect(mm0, mm1, U32(Border), 8 * ui);
        dl->PushClipRect(mm0, mm1, true);
        auto toMap = [&](ImVec2 p) { return mmOffset + (p - worldMin) * mmScale; };
        for (auto& gr : g.groups)
            if (gr.fitTo.empty())
                dl->AddRectFilled(toMap(gr.pos), toMap(gr.pos + gr.size),
                                  U32(kGroupColors[std::clamp(gr.color, 0, kGroupColorCount - 1)], 0.22f), 2 * ui);
        for (auto& n : g.nodes)
            dl->AddRectFilled(toMap(n->pos), toMap(n->pos + ImMax(n->size, ImVec2(40, 20))),
                              IsSelected(g, n->id) ? U32(Accent) : CategoryColor(defs[n->type].category, 0.75f), 1.5f * ui);
        dl->AddRectFilled(toMap(view.ToCanvas(c0)), toMap(view.ToCanvas(c1)), IM_COL32(255, 255, 255, 12), 2 * ui);
        dl->AddRect(toMap(view.ToCanvas(c0)), toMap(view.ToCanvas(c1)), U32(Text, 0.55f), 2 * ui, 0, 1.0f);
        dl->PopClipRect();
    }

    // Empty state
    if (g.nodes.size() <= 2 && g.groups.empty() && csz.y > 160 * ui) {
        const char* empty = "Drag nodes in from the left, or press Example";
        ImVec2 es = TextSizeF(Font::SemiBold(), empty);
        DrawTextF(dl, Font::SemiBold(), ImVec2(c0.x + (csz.x - es.x) * 0.5f, c1.y - es.y - 48 * ui),
                  U32(TextMuted), empty);
    }

    ImGui::SetCursorScreenPos(c0);
    ImGui::EndChild();
}
