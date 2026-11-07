#include "opencv_backend.h"
#include <opencv2/core/utils/logger.hpp>
// OpenCV 5 moved contour/shape analysis (contourArea, approxPolyDP, moments...)
// into the geometry module; OpenCV 4 has them in imgproc.
#if __has_include(<opencv2/geometry.hpp>)
#include <opencv2/geometry.hpp>
#endif
// OpenCV 5 moved CascadeClassifier (Haar) to the xobjdetect contrib module.
#if __has_include(<opencv2/xobjdetect.hpp>)
#include <opencv2/xobjdetect.hpp>
#endif
// ArUco lives in objdetect since OpenCV 4.7 (before that it was in opencv_contrib).
#if __has_include(<opencv2/objdetect/aruco_detector.hpp>)
#include <opencv2/objdetect/aruco_detector.hpp>
#define HAVE_ARUCO 1
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>

#ifndef OPENCV_CASCADE_DIR
#define OPENCV_CASCADE_DIR ""
#endif

// ============================================================
// Camera + texture management
// ============================================================

std::vector<int> GetAvailableCameraIndices() {
    std::vector<int> availableIndices;
    for (int i = 0; i < 5; ++i) {
        cv::VideoCapture tempCap(i, cv::CAP_ANY);
        if (tempCap.isOpened()) {
            availableIndices.push_back(i);
            tempCap.release();
        }
    }
    return availableIndices;
}

static GLuint CreateTexture() {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

static cv::Mat g_lastFrame; // last captured (unmirrored) frame, reused while paused

bool InitializeWebcam(cv::VideoCapture& cap, CameraTextures& tex, int index) {
    if (cap.isOpened()) cap.release();
    g_lastFrame.release();

    cap.open(index, cv::CAP_ANY);
    if (!cap.isOpened()) return false;

    if (tex.input == 0) tex.input = CreateTexture();
    if (tex.output == 0) tex.output = CreateTexture();
    tex.width = tex.height = tex.outWidth = tex.outHeight = 0;
    return true;
}

static void Upload(GLuint id, cv::Mat img) {
    if (!img.isContinuous()) img = img.clone();
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // rows of 3-byte pixels aren't always 4-byte aligned
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, img.cols, img.rows, 0, GL_BGR, GL_UNSIGNED_BYTE, img.ptr());
}

void DestroyTextures(CameraTextures& tex) {
    if (tex.input) glDeleteTextures(1, &tex.input);
    if (tex.output) glDeleteTextures(1, &tex.output);
    tex = CameraTextures{};
}

// ============================================================
// Filters
// ============================================================

static const int kApertures[] = { 3, 5, 7 };
static const int kEdgeKernels[] = { 1, 3, 5, 7 };

static void ApplyGamma(cv::Mat& img, float gamma) {
    if (std::abs(gamma - 1.0f) < 1e-3f) return;
    cv::Mat lut(1, 256, CV_8U);
    for (int i = 0; i < 256; ++i)
        lut.at<uchar>(i) = cv::saturate_cast<uchar>(std::pow(i / 255.0, gamma) * 255.0);
    cv::LUT(img, lut, img);
}

static void ApplyClahe(cv::Mat& bgr, float clip) {
    static cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
    clahe->setClipLimit(clip);
    cv::Mat lab;
    cv::cvtColor(bgr, lab, cv::COLOR_BGR2Lab);
    std::vector<cv::Mat> ch;
    cv::split(lab, ch);
    clahe->apply(ch[0], ch[0]); // equalize luminance only, keep colors
    cv::merge(ch, lab);
    cv::cvtColor(lab, bgr, cv::COLOR_Lab2BGR);
}

static void ApplySharpen(cv::Mat& img, float amount) {
    if (amount <= 0.0f) return;
    cv::Mat blurred;
    cv::GaussianBlur(img, blurred, cv::Size(0, 0), 3);
    cv::addWeighted(img, 1.0 + amount, blurred, -amount, 0, img); // unsharp mask
}

static void ApplyBlur(cv::Mat& img, const EffectsSettings& s) {
    int k = s.blurSize * 2 + 1;
    switch (s.blurMethod) {
        case BLUR_GAUSSIAN:  cv::GaussianBlur(img, img, cv::Size(k, k), 0); break;
        case BLUR_BOX:       cv::blur(img, img, cv::Size(k, k)); break;
        case BLUR_MEDIAN:    cv::medianBlur(img, img, k); break;
        case BLUR_BILATERAL: {
            cv::Mat tmp;
            cv::bilateralFilter(img, tmp, k, 75, 75); // edge-preserving smoothing
            img = tmp;
            break;
        }
        default: break;
    }
}

static void ApplyThreshold(const cv::Mat& gray, cv::Mat& out, const EffectsSettings& s) {
    int block = s.adaptiveBlock * 2 + 1;
    switch (s.thresholdType) {
        case TH_BINARY:     cv::threshold(gray, out, s.thresholdValue, 255, cv::THRESH_BINARY); break;
        case TH_BINARY_INV: cv::threshold(gray, out, s.thresholdValue, 255, cv::THRESH_BINARY_INV); break;
        case TH_OTSU:       cv::threshold(gray, out, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU); break;
        case TH_ADAPTIVE_MEAN:
            cv::adaptiveThreshold(gray, out, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, block, s.adaptiveC);
            break;
        case TH_ADAPTIVE_GAUSSIAN:
            cv::adaptiveThreshold(gray, out, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, block, s.adaptiveC);
            break;
        default: out = gray; break;
    }
}

static void ApplyEdges(const cv::Mat& src, cv::Mat& out, const EffectsSettings& s) {
    int ksize = kEdgeKernels[std::clamp(s.edgeKernel, 0, 3)];
    switch (s.edgeMethod) {
        case EDGE_CANNY:
            cv::Canny(src, out, s.cannyLow, s.cannyHigh, kApertures[std::clamp(s.cannyAperture, 0, 2)], s.cannyL2);
            break;
        case EDGE_SOBEL: {
            cv::Mat gx, gy, ax, ay;
            cv::Sobel(src, gx, CV_16S, 1, 0, ksize);
            cv::Sobel(src, gy, CV_16S, 0, 1, ksize);
            cv::convertScaleAbs(gx, ax);
            cv::convertScaleAbs(gy, ay);
            cv::addWeighted(ax, 0.5, ay, 0.5, 0, out);
            break;
        }
        case EDGE_LAPLACIAN: {
            cv::Mat lap;
            cv::Laplacian(src, lap, CV_16S, ksize);
            cv::convertScaleAbs(lap, out);
            break;
        }
        default: out = src; break;
    }
}

static void ApplyMorphology(cv::Mat& img, const EffectsSettings& s) {
    if (s.morphOp == MORPH_OP_NONE) return;
    static const int shapes[] = { cv::MORPH_RECT, cv::MORPH_CROSS, cv::MORPH_ELLIPSE };
    int k = s.morphSize * 2 + 1;
    cv::Mat kernel = cv::getStructuringElement(shapes[std::clamp(s.morphShape, 0, 2)], cv::Size(k, k));
    int op = cv::MORPH_ERODE;
    switch (s.morphOp) {
        case MORPH_OP_ERODE:    op = cv::MORPH_ERODE; break;
        case MORPH_OP_DILATE:   op = cv::MORPH_DILATE; break;
        case MORPH_OP_OPEN:     op = cv::MORPH_OPEN; break;
        case MORPH_OP_CLOSE:    op = cv::MORPH_CLOSE; break;
        case MORPH_OP_GRADIENT: op = cv::MORPH_GRADIENT; break;
    }
    cv::morphologyEx(img, img, op, kernel, cv::Point(-1, -1), s.morphIterations);
}

// ============================================================
// Drawing helpers
// ============================================================

struct DrawStyle { int thick; double font; };

static void DrawLabel(cv::Mat& out, const std::string& text, cv::Point org, const cv::Scalar& color,
                      const DrawStyle& st, bool centered = false) {
    int base = 0;
    cv::Size ts = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, st.font, st.thick, &base);
    if (centered) org.x -= ts.width / 2;
    cv::putText(out, text, org, cv::FONT_HERSHEY_SIMPLEX, st.font, cv::Scalar(0, 0, 0), st.thick + 2);
    cv::putText(out, text, org, cv::FONT_HERSHEY_SIMPLEX, st.font, color, st.thick);
}

// ============================================================
// Contours & shapes
// ============================================================

static std::string ClassifyShape(const std::vector<cv::Point>& contour,
                                 const std::vector<cv::Point>& approx,
                                 DetectionStats& stats) {
    size_t v = approx.size();
    if (v == 3) { stats.triangles++; return "Triangle"; }
    if (v == 4) {
        cv::RotatedRect rr = cv::minAreaRect(approx); // rotation-independent aspect ratio
        float w = rr.size.width, h = rr.size.height;
        float ar = std::max(w, h) / std::max(1.0f, std::min(w, h));
        if (ar < 1.15f) { stats.squares++; return "Square"; }
        stats.rectangles++; return "Rectangle";
    }
    if (v == 5) { stats.pentagons++; return "Pentagon"; }
    if (v == 6) { stats.hexagons++; return "Hexagon"; }

    double area = cv::contourArea(contour);
    double peri = cv::arcLength(contour, true);
    double circularity = peri > 0 ? 4.0 * CV_PI * area / (peri * peri) : 0.0;
    if (circularity > 0.78) { stats.circles++; return "Circle"; }
    stats.others++;
    return "Polygon(" + std::to_string(v) + ")";
}

static cv::Scalar ShapeColor(const std::string& name) {
    if (name == "Triangle")  return cv::Scalar(0, 255, 255);
    if (name == "Square")    return cv::Scalar(255, 0, 255);
    if (name == "Rectangle") return cv::Scalar(255, 128, 0);
    if (name == "Pentagon")  return cv::Scalar(0, 128, 255);
    if (name == "Hexagon")   return cv::Scalar(255, 255, 0);
    if (name == "Circle")    return cv::Scalar(0, 255, 0);
    return cv::Scalar(200, 200, 200);
}

static void DetectShapes(const cv::Mat& binary, cv::Mat& out, const EffectsSettings& s,
                         DetectionStats& stats, const DrawStyle& st, float scale) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, s.externalOnly ? cv::RETR_EXTERNAL : cv::RETR_LIST,
                     cv::CHAIN_APPROX_SIMPLE);

    // Areas are given/shown at full camera resolution
    double s2 = (double)scale * scale;
    double minArea = s.minArea * s2;

    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < minArea) continue;
        stats.contours++;

        double peri = cv::arcLength(c, true);
        std::vector<cv::Point> approx;
        cv::approxPolyDP(c, approx, (s.approxEpsilon / 100.0) * peri, true);
        std::string name = ClassifyShape(c, approx, stats);
        cv::Scalar color = ShapeColor(name);
        int thin = std::max(1, st.thick / 2);

        if (s.drawContours)
            cv::polylines(out, approx, true, color, st.thick);
        cv::Rect box = cv::boundingRect(c);
        if (s.drawBoundingBox)
            cv::rectangle(out, box, cv::Scalar(255, 255, 255), thin);
        cv::RotatedRect rr = cv::minAreaRect(c);
        if (s.drawRotatedRect) {
            cv::Point2f pts[4];
            rr.points(pts);
            for (int i = 0; i < 4; ++i) cv::line(out, pts[i], pts[(i + 1) % 4], cv::Scalar(0, 165, 255), thin);
        }
        if (s.drawMinCircle) {
            cv::Point2f cc; float r;
            cv::minEnclosingCircle(c, cc, r);
            cv::circle(out, cc, (int)r, cv::Scalar(255, 255, 0), thin);
        }
        if (s.drawHull) {
            std::vector<cv::Point> hull;
            cv::convexHull(c, hull);
            cv::polylines(out, hull, true, cv::Scalar(180, 105, 255), thin);
        }

        cv::Moments m = cv::moments(c);
        cv::Point center = m.m00 != 0 ? cv::Point((int)(m.m10 / m.m00), (int)(m.m01 / m.m00))
                                      : (box.tl() + box.br()) / 2;
        if (s.drawCentroid)
            cv::circle(out, center, st.thick * 2, cv::Scalar(0, 0, 255), cv::FILLED);
        if (s.drawLabels)
            DrawLabel(out, name, cv::Point(center.x, center.y - st.thick * 3), color, st, true);
        if (s.drawMeasurements) {
            char buf[96];
            snprintf(buf, sizeof(buf), "A=%.0f P=%.0f %.0fdeg", area / s2, peri / scale, rr.angle);
            DrawLabel(out, buf, cv::Point(center.x, center.y + st.thick * 12),
                      cv::Scalar(255, 255, 255), { st.thick, st.font * 0.8 }, true);
        }
    }
}

// ============================================================
// Cascade (Haar) detection
// ============================================================

static const char* kCascadeFiles[CASCADE_COUNT] = {
    "haarcascade_frontalface_default.xml",
    "haarcascade_profileface.xml",
    "haarcascade_smile.xml",
    "haarcascade_upperbody.xml",
    "haarcascade_fullbody.xml",
    "haarcascade_frontalcatface.xml",
    "haarcascade_russian_plate_number.xml",
};
static const char* kCascadeNames[CASCADE_COUNT] = {
    "Face", "Profile", "Smile", "Upper body", "Body", "Cat", "Plate",
};

static std::string FindCascade(const std::string& file) {
    const char* env = std::getenv("OPENCV_CASCADE_DIR");
    std::vector<std::string> dirs;
    if (env) dirs.push_back(env);
    dirs.push_back(OPENCV_CASCADE_DIR);
    dirs.insert(dirs.end(), {
        "/opt/homebrew/share/opencv5/haarcascades", "/opt/homebrew/share/opencv4/haarcascades",
        "/usr/local/share/opencv5/haarcascades", "/usr/local/share/opencv4/haarcascades",
        "/usr/share/opencv5/haarcascades", "/usr/share/opencv4/haarcascades", "haarcascades",
    });
    for (const auto& d : dirs) {
        if (d.empty()) continue;
        std::filesystem::path p = std::filesystem::path(d) / file;
        if (std::filesystem::exists(p)) return p.string();
    }
    return {};
}

static bool LoadCascade(cv::CascadeClassifier& cc, int& loadedType, int type, std::string& error) {
    if (loadedType == type && !cc.empty()) return true;
    loadedType = type;
    std::string path = FindCascade(kCascadeFiles[type]);
    if (path.empty() || !cc.load(path)) {
        error = std::string("Cascade not found: ") + kCascadeFiles[type] +
                " (set OPENCV_CASCADE_DIR)";
        return false;
    }
    return true;
}

static void DetectCascade(const cv::Mat& gray, cv::Mat& out, const EffectsSettings& s,
                          DetectionStats& stats, const DrawStyle& st, float scale) {
    static cv::CascadeClassifier cascade, eyeCascade;
    static int loadedType = -1, eyeLoaded = -1;
    int type = std::clamp(s.cascadeType, 0, CASCADE_COUNT - 1);
    if (!LoadCascade(cascade, loadedType, type, stats.cascadeError)) return;

    int minSize = std::max(8, (int)(s.cascadeMinSize * scale));
    std::vector<cv::Rect> objects;
    cascade.detectMultiScale(gray, objects, std::max(1.01f, s.cascadeScaleFactor),
                             s.cascadeMinNeighbors, 0, cv::Size(minSize, minSize));
    stats.objects = (int)objects.size();

    bool wantEyes = s.detectEyes && type == CASCADE_FACE;
    if (wantEyes) {
        // eye cascade: always the same file, loaded once
        if (eyeLoaded != 0) {
            std::string p = FindCascade("haarcascade_eye_tree_eyeglasses.xml");
            eyeLoaded = (!p.empty() && eyeCascade.load(p)) ? 0 : 1;
        }
        if (eyeLoaded != 0) wantEyes = false;
    }

    for (const auto& r : objects) {
        cv::rectangle(out, r, cv::Scalar(0, 255, 0), st.thick);
        DrawLabel(out, kCascadeNames[type], cv::Point(r.x, r.y - st.thick * 3), cv::Scalar(0, 255, 0), st);
        if (wantEyes) {
            cv::Mat roi = gray(cv::Rect(r.x, r.y, r.width, r.height / 2 + r.height / 8)); // upper face
            std::vector<cv::Rect> eyes;
            eyeCascade.detectMultiScale(roi, eyes, 1.1, 4, 0, cv::Size(r.width / 8, r.width / 8));
            for (const auto& e : eyes) {
                cv::Point c(r.x + e.x + e.width / 2, r.y + e.y + e.height / 2);
                cv::circle(out, c, (e.width + e.height) / 4, cv::Scalar(255, 200, 0), st.thick);
            }
            stats.eyes += (int)eyes.size();
        }
    }
}

// ============================================================
// QR codes
// ============================================================

static void DetectQR(const cv::Mat& gray, cv::Mat& out, DetectionStats& stats, const DrawStyle& st) {
    static cv::QRCodeDetector qr;
    std::vector<std::string> texts;
    std::vector<cv::Point2f> pts;
    if (!qr.detectAndDecodeMulti(gray, texts, pts) || pts.empty()) return;
    for (size_t i = 0; i < texts.size() && (i * 4 + 3) < pts.size(); ++i) {
        std::vector<cv::Point> poly;
        for (int k = 0; k < 4; ++k) poly.emplace_back(pts[i * 4 + k]);
        cv::polylines(out, poly, true, cv::Scalar(255, 0, 0), st.thick);
        std::string label = texts[i].empty() ? "QR (undecoded)" : texts[i];
        if (label.size() > 40) label = label.substr(0, 37) + "...";
        DrawLabel(out, label, poly[0] - cv::Point(0, st.thick * 4), cv::Scalar(255, 128, 0), st);
        stats.qrCodes.push_back(texts[i].empty() ? "(undecoded)" : texts[i]);
    }
}

// ============================================================
// ArUco / AprilTag markers
// ============================================================

static void DetectAruco(const cv::Mat& gray, cv::Mat& out, const EffectsSettings& s,
                        DetectionStats& stats, const DrawStyle& st) {
#ifdef HAVE_ARUCO
    static const int dicts[] = {
        cv::aruco::DICT_4X4_50, cv::aruco::DICT_5X5_100, cv::aruco::DICT_6X6_250,
        cv::aruco::DICT_7X7_1000, cv::aruco::DICT_ARUCO_ORIGINAL, cv::aruco::DICT_APRILTAG_36h11,
    };
    static std::unique_ptr<cv::aruco::ArucoDetector> detector;
    static int loadedDict = -1;
    int d = std::clamp(s.arucoDict, 0, (int)(sizeof(dicts) / sizeof(dicts[0])) - 1);
    if (!detector || loadedDict != d) {
        detector = std::make_unique<cv::aruco::ArucoDetector>(
            cv::aruco::getPredefinedDictionary(dicts[d]), cv::aruco::DetectorParameters());
        loadedDict = d;
    }
    std::vector<std::vector<cv::Point2f>> corners;
    std::vector<int> ids;
    detector->detectMarkers(gray, corners, ids);
    for (size_t i = 0; i < ids.size(); ++i) {
        std::vector<cv::Point> poly(corners[i].begin(), corners[i].end());
        cv::polylines(out, poly, true, cv::Scalar(0, 255, 0), st.thick);
        cv::circle(out, poly[0], st.thick * 3, cv::Scalar(0, 0, 255), cv::FILLED); // top-left corner
        cv::Point2f c = (corners[i][0] + corners[i][1] + corners[i][2] + corners[i][3]) * 0.25f;
        DrawLabel(out, "id " + std::to_string(ids[i]), c, cv::Scalar(0, 255, 255), st, true);
    }
    stats.arucoIds = ids;
#else
    (void)gray; (void)out; (void)s; (void)st;
    stats.arucoError = "ArUco needs OpenCV 4.7+";
#endif
}

// ============================================================
// Color tracking
// ============================================================

static void TrackColor(const cv::Mat& mask, cv::Mat& out, const EffectsSettings& s,
                       DetectionStats& stats, const DrawStyle& st, float scale) {
    static std::deque<cv::Point2f> trail; // normalized 0..1 positions
    cv::Mat clean;
    cv::Mat k = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(mask, clean, cv::MORPH_OPEN, k);
    cv::morphologyEx(clean, clean, cv::MORPH_CLOSE, k);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(clean, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    double bestArea = s.trackMinArea * scale * scale;
    int best = -1;
    for (int i = 0; i < (int)contours.size(); ++i) {
        double a = cv::contourArea(contours[i]);
        if (a > bestArea) { bestArea = a; best = i; }
    }

    cv::Point imgCenter(out.cols / 2, out.rows / 2);
    int cross = std::max(6, out.cols / 60);
    cv::line(out, imgCenter - cv::Point(cross, 0), imgCenter + cv::Point(cross, 0), cv::Scalar(200, 200, 200), 1);
    cv::line(out, imgCenter - cv::Point(0, cross), imgCenter + cv::Point(0, cross), cv::Scalar(200, 200, 200), 1);

    if (best >= 0) {
        cv::Point2f c; float r;
        cv::minEnclosingCircle(contours[best], c, r);
        cv::Moments m = cv::moments(contours[best]);
        if (m.m00 > 0) c = cv::Point2f((float)(m.m10 / m.m00), (float)(m.m01 / m.m00));

        cv::circle(out, c, (int)r, cv::Scalar(0, 255, 255), st.thick);
        cv::circle(out, c, st.thick * 3, cv::Scalar(0, 0, 255), cv::FILLED);
        cv::line(out, imgCenter, c, cv::Scalar(0, 255, 255), std::max(1, st.thick / 2));

        stats.tracked = true;
        stats.trackX = c.x / scale;
        stats.trackY = c.y / scale;
        stats.trackErrX = (c.x - imgCenter.x) / (float)imgCenter.x;
        stats.trackErrY = (c.y - imgCenter.y) / (float)imgCenter.y;
        stats.trackArea = (float)(bestArea / (scale * scale));

        char buf[64];
        snprintf(buf, sizeof(buf), "dx %+.2f dy %+.2f", stats.trackErrX, stats.trackErrY);
        DrawLabel(out, buf, cv::Point((int)c.x, (int)(c.y - r) - st.thick * 3), cv::Scalar(0, 255, 255), st, true);

        trail.emplace_back(c.x / out.cols, c.y / out.rows);
    } else if (!trail.empty()) {
        trail.pop_front(); // fade out when lost
    }
    while ((int)trail.size() > std::max(0, s.trailLength)) trail.pop_front();

    for (size_t i = 1; i < trail.size(); ++i) {
        int t = std::max(1, (int)(st.thick * 2.0 * i / trail.size()));
        cv::line(out, cv::Point2f(trail[i - 1].x * out.cols, trail[i - 1].y * out.rows),
                 cv::Point2f(trail[i].x * out.cols, trail[i].y * out.rows), cv::Scalar(0, 140, 255), t);
    }
}

// ============================================================
// Motion detection (MOG2 background subtraction)
// ============================================================

// Returns the foreground mask; motion regions are appended to `boxes`.
static cv::Mat DetectMotion(const cv::Mat& work, const EffectsSettings& s, DetectionStats& stats,
                            std::vector<cv::Rect>& boxes, float scale) {
    static cv::Ptr<cv::BackgroundSubtractorMOG2> mog;
    static int history = -1;
    static bool shadows = false;
    static cv::Size size;
    static int warmup = 0; // frames since the model was (re)created
    if (!mog || history != s.motionHistory || shadows != s.motionShadows || size != work.size()) {
        mog = cv::createBackgroundSubtractorMOG2(s.motionHistory, s.motionVarThreshold, s.motionShadows);
        history = s.motionHistory;
        shadows = s.motionShadows;
        size = work.size();
        warmup = 0;
    }
    mog->setVarThreshold(s.motionVarThreshold);

    cv::Mat fg;
    mog->apply(work, fg, s.motionLearningRate);
    if (warmup < 15) { // an empty model flags everything as motion; let it learn first
        ++warmup;
        return cv::Mat::zeros(fg.size(), CV_8U);
    }
    cv::threshold(fg, fg, 200, 255, cv::THRESH_BINARY); // drop shadows (127)
    cv::morphologyEx(fg, fg, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3)));
    cv::dilate(fg, fg, cv::Mat(), cv::Point(-1, -1), 2);

    stats.motionPercent = 100.0f * cv::countNonZero(fg) / (float)fg.total();

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(fg, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    double minArea = s.motionMinArea * scale * scale;
    for (const auto& c : contours)
        if (cv::contourArea(c) >= minArea) boxes.push_back(cv::boundingRect(c));
    stats.motionRegions = (int)boxes.size();
    return fg;
}

// ============================================================
// Feature keypoints
// ============================================================

static void DetectFeatures(const cv::Mat& gray, cv::Mat& out, const EffectsSettings& s,
                           DetectionStats& stats, const DrawStyle& st) {
    std::vector<cv::KeyPoint> kps;
    switch (s.featureMethod) {
        case FEAT_ORB:  cv::ORB::create(s.maxFeatures)->detect(gray, kps); break;
        case FEAT_FAST: cv::FastFeatureDetector::create(s.fastThreshold)->detect(gray, kps); break;
        case FEAT_SIFT: cv::SIFT::create(s.maxFeatures)->detect(gray, kps); break;
        default: return;
    }
    if (s.featureMethod == FEAT_FAST && (int)kps.size() > s.maxFeatures) {
        cv::KeyPointsFilter::retainBest(kps, s.maxFeatures);
    }
    for (const auto& k : kps) {
        if (s.richKeypoints) {
            int r = std::max(2, (int)(k.size / 2));
            cv::circle(out, k.pt, r, cv::Scalar(0, 255, 128), 1);
            if (k.angle >= 0) {
                float a = k.angle * (float)CV_PI / 180.0f;
                cv::line(out, k.pt, k.pt + cv::Point2f(std::cos(a) * r, std::sin(a) * r), cv::Scalar(0, 255, 128), 1);
            }
        } else {
            cv::circle(out, k.pt, std::max(2, st.thick * 2), cv::Scalar(0, 255, 128), 1);
        }
    }
    stats.keypoints = (int)kps.size();
}

// ============================================================
// Histogram
// ============================================================

static void ComputeHistogram(const cv::Mat& img, DetectionStats& stats) {
    std::vector<cv::Mat> ch;
    if (img.channels() == 1) ch = { img };
    else cv::split(img, ch);
    stats.histChannels = (int)std::min<size_t>(3, ch.size());
    float maxVal = 1.0f;
    for (int c = 0; c < stats.histChannels; ++c) {
        cv::Mat h;
        int histSize = 256;
        float range[] = { 0, 256 };
        const float* ranges[] = { range };
        cv::calcHist(&ch[c], 1, nullptr, cv::Mat(), h, 1, &histSize, ranges);
        for (int i = 0; i < 256; ++i) {
            stats.hist[c][i] = h.at<float>(i);
            maxVal = std::max(maxVal, stats.hist[c][i]);
        }
    }
    for (int c = 0; c < stats.histChannels; ++c)
        for (float& v : stats.hist[c]) v /= maxVal;
    stats.histValid = true;
}

// ============================================================
// Snapshot
// ============================================================

static void SaveSnapshot(const cv::Mat& input, const cv::Mat& output, RuntimeControl& control) {
    control.snapshotRequested = false;
    const char* home = std::getenv("HOME");
    std::filesystem::path dir = std::filesystem::path(home ? home : ".") / "Pictures" / "TULPAR";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&now));
    std::string base = (dir / stamp).string();
    bool ok = cv::imwrite(base + "_input.png", input) && cv::imwrite(base + "_output.png", output);
    control.lastSnapshot = ok ? base + "_*.png" : "Failed to save to " + dir.string();
}

// ============================================================
// Main pipeline
//   frame -> scale -> brightness/contrast/gamma/CLAHE/sharpen/invert -> blur -> gray
//         -> [HSV mask] -> [threshold] -> [edges] -> [morphology]  ("binary" stage)
//         -> detections drawn on the output image
// ============================================================

void UpdateWebcamTextures(cv::VideoCapture& cap, CameraTextures& tex, const EffectsSettings& s,
                          RuntimeControl& control, DetectionStats& stats) {
    if (!control.paused || g_lastFrame.empty()) {
        cv::Mat grabbed;
        cap >> grabbed;
        if (grabbed.empty()) return;
        g_lastFrame = grabbed;
    }
    cv::Mat frame;
    if (s.mirror) cv::flip(g_lastFrame, frame, 1);
    else frame = g_lastFrame.clone();

    int64 t0 = cv::getTickCount();
    stats = DetectionStats{};
    cv::Mat out;

    try {
        // 1. Scale down for speed
        cv::Mat base;
        float scale = std::clamp(s.scale, 0.1f, 1.0f);
        if (scale < 0.999f) cv::resize(frame, base, cv::Size(), scale, scale, cv::INTER_AREA);
        else base = frame.clone();

        // 2. Pre-processing
        cv::Mat work = base.clone();
        if (s.contrast != 1.0f || s.brightness != 0.0f)
            work.convertTo(work, -1, s.contrast, s.brightness);
        ApplyGamma(work, s.gamma);
        if (s.clahe) ApplyClahe(work, s.claheClip);
        ApplySharpen(work, s.sharpen);
        if (s.invert) cv::bitwise_not(work, work);
        ApplyBlur(work, s);

        // 3. Grayscale (+ optional histogram equalization)
        cv::Mat gray;
        cv::cvtColor(work, gray, cv::COLOR_BGR2GRAY);
        if (s.equalizeHist) cv::equalizeHist(gray, gray);

        // 4. HSV mask (used by the binary stage and by color tracking)
        cv::Mat mask;
        if (s.colorMask || s.trackColor) {
            cv::Mat hsv;
            cv::cvtColor(work, hsv, cv::COLOR_BGR2HSV);
            if (s.hMin <= s.hMax) {
                cv::inRange(hsv, cv::Scalar(s.hMin, s.sMin, s.vMin), cv::Scalar(s.hMax, s.sMax, s.vMax), mask);
            } else { // hue wrap-around (e.g. red: 170..10)
                cv::Mat a, b;
                cv::inRange(hsv, cv::Scalar(s.hMin, s.sMin, s.vMin), cv::Scalar(179, s.sMax, s.vMax), a);
                cv::inRange(hsv, cv::Scalar(0, s.sMin, s.vMin), cv::Scalar(s.hMax, s.sMax, s.vMax), b);
                mask = a | b;
            }
        }

        // 5. Binary stage: mask -> threshold -> edges -> morphology
        cv::Mat binary;
        bool hasBinary = false;
        if (s.colorMask) { binary = mask.clone(); hasBinary = true; }
        if (s.thresholdType != TH_NONE) {
            cv::Mat th;
            ApplyThreshold(gray, th, s);
            binary = hasBinary ? (binary & th) : th;
            hasBinary = true;
        }
        if (s.edgeMethod != EDGE_NONE) {
            cv::Mat edges;
            ApplyEdges(hasBinary ? binary : gray, edges, s);
            binary = edges;
            hasBinary = true;
        }
        if (hasBinary) ApplyMorphology(binary, s);

        // 6. Base image for the Output Feed
        switch (s.outputView) {
            case VIEW_BINARY:
                cv::cvtColor(hasBinary ? binary : gray, out, cv::COLOR_GRAY2BGR);
                break;
            case VIEW_ORIGINAL:
            case VIEW_MOTION: // replaced by the motion mask below if motion is on
                out = base.clone();
                break;
            default:
                if (s.colorMode == COLOR_MODE_GRAY)     cv::cvtColor(gray, out, cv::COLOR_GRAY2BGR);
                else if (s.colorMode == COLOR_MODE_HSV) cv::cvtColor(work, out, cv::COLOR_BGR2HSV);
                else if (s.colorMask)                   work.copyTo(out, mask); // only pixels inside the HSV range
                else                                    out = work.clone();
                break;
        }

        DrawStyle st;
        st.thick = std::max(1, (int)std::lround(out.cols / 640.0));
        st.font = 0.5 * st.thick;

        if (s.showHistogram)
            ComputeHistogram(s.colorMode == COLOR_MODE_GRAY ? gray : work, stats);

        // 7. Motion (first, so the motion view can replace the base image)
        if (s.detectMotion) {
            std::vector<cv::Rect> boxes;
            cv::Mat fg = DetectMotion(work, s, stats, boxes, scale);
            if (s.outputView == VIEW_MOTION) cv::cvtColor(fg, out, cv::COLOR_GRAY2BGR);
            for (const auto& b : boxes) cv::rectangle(out, b, cv::Scalar(0, 0, 255), st.thick);
            if (!boxes.empty())
                DrawLabel(out, "MOTION", cv::Point(st.thick * 8, st.thick * 20), cv::Scalar(0, 0, 255), st);
        }

        // 8. Contours & shape classification
        if (s.detectContours) {
            cv::Mat contourInput;
            if (!hasBinary) {
                cv::Canny(gray, contourInput, s.cannyLow, s.cannyHigh); // sensible default
                cv::dilate(contourInput, contourInput, cv::Mat());      // close small gaps
            } else if (s.edgeMethod == EDGE_SOBEL || s.edgeMethod == EDGE_LAPLACIAN) {
                cv::threshold(binary, contourInput, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
            } else {
                contourInput = binary.clone();
            }
            DetectShapes(contourInput, out, s, stats, st, scale);
        }

        // 9. Hough circles (on the smoothed grayscale image)
        if (s.houghCircles) {
            std::vector<cv::Vec3f> circles;
            cv::Mat g;
            cv::medianBlur(gray, g, 5);
            cv::HoughCircles(g, circles, cv::HOUGH_GRADIENT, s.circleDp, std::max(1.0f, s.circleMinDist * scale),
                             s.circleParam1, s.circleParam2,
                             (int)(s.circleMinRadius * scale), (int)(s.circleMaxRadius * scale));
            for (const auto& c : circles) {
                cv::Point center(cvRound(c[0]), cvRound(c[1]));
                cv::circle(out, center, cvRound(c[2]), cv::Scalar(255, 0, 255), st.thick);
                cv::circle(out, center, st.thick * 2, cv::Scalar(0, 0, 255), cv::FILLED);
            }
            stats.houghCircles = (int)circles.size();
        }

        // 10. Hough lines (on an edge image)
        if (s.houghLines) {
            cv::Mat edges;
            if (s.edgeMethod == EDGE_CANNY && hasBinary) edges = binary;
            else cv::Canny(gray, edges, s.cannyLow, s.cannyHigh);
            std::vector<cv::Vec4i> lines;
            cv::HoughLinesP(edges, lines, 1, CV_PI / 180, s.lineThreshold,
                            s.lineMinLength * scale, s.lineMaxGap * scale);
            for (const auto& l : lines)
                cv::line(out, cv::Point(l[0], l[1]), cv::Point(l[2], l[3]), cv::Scalar(0, 0, 255), st.thick);
            stats.lines = (int)lines.size();
        }

        // 11. Shi-Tomasi corners
        if (s.detectCorners) {
            std::vector<cv::Point2f> corners;
            cv::goodFeaturesToTrack(gray, corners, s.maxCorners, s.cornerQuality,
                                    std::max(1.0f, s.cornerMinDistance * scale));
            for (const auto& p : corners)
                cv::circle(out, p, st.thick * 3, cv::Scalar(0, 255, 255), st.thick);
            stats.corners = (int)corners.size();
        }

        // 12. Feature keypoints
        if (s.featureMethod != FEAT_NONE) DetectFeatures(gray, out, s, stats, st);

        // 13. Object detection
        if (s.detectCascade) DetectCascade(gray, out, s, stats, st, scale);
        if (s.detectQR)      DetectQR(gray, out, stats, st);
        if (s.detectAruco)   DetectAruco(gray, out, s, stats, st);

        // 14. Color tracking
        if (s.trackColor && !mask.empty()) TrackColor(mask, out, s, stats, st, scale);
    } catch (const cv::Exception& e) {
        // A bad parameter combination should never crash the app: show the frame and the error.
        out = frame.clone();
        cv::putText(out, "OpenCV error: " + e.err, cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX,
                    1.0, cv::Scalar(0, 0, 255), 2);
    }

    stats.processingMs = (cv::getTickCount() - t0) * 1000.0 / cv::getTickFrequency();

    if (control.snapshotRequested) SaveSnapshot(frame, out, control);

    // Upload
    Upload(tex.input, frame);
    Upload(tex.output, out);
    tex.width = frame.cols;
    tex.height = frame.rows;
    tex.outWidth = out.cols;
    tex.outHeight = out.rows;
}
