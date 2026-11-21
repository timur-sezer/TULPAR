#ifndef OPENCV_BACKEND_H
#define OPENCV_BACKEND_H

#include "../dependencies/include/glad/glad.h"
#include <opencv2/opencv.hpp>
#include <array>
#include <string>
#include <vector>

// ---- Option enums (values match the combo boxes in the UI) ----
enum BlurMethod      { BLUR_NONE = 0, BLUR_GAUSSIAN, BLUR_BOX, BLUR_MEDIAN, BLUR_BILATERAL };
enum ColorMode       { COLOR_MODE_BGR = 0, COLOR_MODE_GRAY, COLOR_MODE_HSV };
enum ThresholdType   { TH_NONE = 0, TH_BINARY, TH_BINARY_INV, TH_OTSU, TH_ADAPTIVE_MEAN, TH_ADAPTIVE_GAUSSIAN };
enum EdgeMethod      { EDGE_NONE = 0, EDGE_CANNY, EDGE_SOBEL, EDGE_LAPLACIAN };
enum MorphOp         { MORPH_OP_NONE = 0, MORPH_OP_ERODE, MORPH_OP_DILATE, MORPH_OP_OPEN, MORPH_OP_CLOSE, MORPH_OP_GRADIENT };
enum OutputView      { VIEW_PROCESSED = 0, VIEW_BINARY, VIEW_ORIGINAL, VIEW_MOTION };
enum CascadeType     { CASCADE_FACE = 0, CASCADE_PROFILE_FACE, CASCADE_SMILE, CASCADE_UPPER_BODY,
                       CASCADE_FULL_BODY, CASCADE_CAT_FACE, CASCADE_PLATE, CASCADE_COUNT };
enum FeatureMethod   { FEAT_NONE = 0, FEAT_ORB, FEAT_FAST, FEAT_SIFT };

struct EffectsSettings {
    // Output
    int   outputView = VIEW_PROCESSED;   // which image is shown in the Output Feed
    float scale = 0.5f;                  // processing resolution (lower = faster)
    bool  mirror = true;                 // flip horizontally (selfie view)

    // Pre-processing
    float brightness = 0.0f;             // added to every pixel (-100..100)
    float contrast = 1.0f;               // multiplied with every pixel (0..3)
    float gamma = 1.0f;                  // <1 brightens shadows, >1 darkens
    float sharpen = 0.0f;                // unsharp-mask amount
    bool  clahe = false;                 // adaptive histogram equalization (on luminance)
    float claheClip = 2.0f;
    bool  equalizeHist = false;          // global histogram equalization on grayscale
    bool  invert = false;
    int   blurMethod = BLUR_NONE;
    int   blurSize = 1;                  // kernel = 2*blurSize+1

    // Color
    int   colorMode = COLOR_MODE_BGR;

    // HSV color mask (OpenCV ranges: H 0..179, S/V 0..255)
    bool  colorMask = false;
    int   hMin = 0, hMax = 179, sMin = 0, sMax = 255, vMin = 0, vMax = 255;

    // Threshold
    int   thresholdType = TH_NONE;
    int   thresholdValue = 127;
    int   adaptiveBlock = 5;             // block = 2*adaptiveBlock+1
    int   adaptiveC = 2;

    // Edge detection
    int   edgeMethod = EDGE_NONE;
    int   cannyLow = 50, cannyHigh = 150;
    int   cannyAperture = 0;             // index into {3,5,7}
    bool  cannyL2 = false;
    int   edgeKernel = 1;                // index into {1,3,5,7} for Sobel/Laplacian

    // Morphology
    int   morphOp = MORPH_OP_NONE;
    int   morphShape = 0;                // 0 rect, 1 cross, 2 ellipse
    int   morphSize = 1;                 // kernel = 2*morphSize+1
    int   morphIterations = 1;

    // Contours & shapes
    bool  detectContours = false;
    bool  externalOnly = true;
    float minArea = 500.0f;
    float approxEpsilon = 2.0f;          // % of contour perimeter
    bool  drawContours = true;
    bool  drawBoundingBox = false;
    bool  drawRotatedRect = false;
    bool  drawMinCircle = false;
    bool  drawHull = false;
    bool  drawLabels = true;
    bool  drawMeasurements = false;      // area / perimeter / angle under the label
    bool  drawCentroid = false;

    // Hough circles
    bool  houghCircles = false;
    float circleDp = 1.2f;
    int   circleMinDist = 40;
    int   circleParam1 = 100;
    int   circleParam2 = 40;
    int   circleMinRadius = 10;
    int   circleMaxRadius = 150;

    // Hough lines (probabilistic)
    bool  houghLines = false;
    int   lineThreshold = 80;
    int   lineMinLength = 50;
    int   lineMaxGap = 10;

    // Corners (Shi-Tomasi)
    bool  detectCorners = false;
    int   maxCorners = 100;
    float cornerQuality = 0.01f;
    int   cornerMinDistance = 10;

    // Feature keypoints
    int   featureMethod = FEAT_NONE;
    int   maxFeatures = 500;
    int   fastThreshold = 20;
    bool  richKeypoints = false;         // draw size + orientation

    // Cascade (Haar) object detection
    bool  detectCascade = false;
    int   cascadeType = CASCADE_FACE;
    bool  detectEyes = false;            // eyes inside detected faces
    float cascadeScaleFactor = 1.1f;
    int   cascadeMinNeighbors = 5;
    int   cascadeMinSize = 40;           // px at full resolution

    // QR codes
    bool  detectQR = false;

    // ArUco / AprilTag markers
    bool  detectAruco = false;
    int   arucoDict = 0;                 // index into the dictionary list in the UI

    // Color tracking (largest blob inside the HSV range)
    bool  trackColor = false;
    int   trailLength = 30;
    float trackMinArea = 300.0f;

    // Motion detection (background subtraction)
    bool  detectMotion = false;
    int   motionHistory = 300;
    float motionVarThreshold = 25.0f;
    float motionLearningRate = -1.0f;    // -1 = automatic
    float motionMinArea = 800.0f;
    bool  motionShadows = false;

    // Histogram
    bool  showHistogram = false;
};

// Runtime controls that are not "settings" (not touched by Reset All).
struct RuntimeControl {
    bool paused = false;
    bool snapshotRequested = false;
    std::string lastSnapshot;            // path of the last saved snapshot (or error)
    bool showCameraSettings = false;     // opens the camera / sensor settings modal
};

enum CameraType {
    CAM_TYPE_V4L2 = 0,         // Standard V4L2 / UVC webcam / Arducam USB
    CAM_TYPE_LIBCAMERA,        // Raspberry Pi Camera / Arducam CSI via libcamerasrc GStreamer
    CAM_TYPE_GSTREAMER,        // Custom GStreamer pipeline
    CAM_TYPE_CUSTOM_PATH       // Direct device path (/dev/videoX) or stream URL
};

struct CameraDeviceInfo {
    int id = 0;                         // Unique ID used in availableCameras
    int v4l2Index = -1;                 // System video index e.g. 0 for /dev/video0, or -1
    std::string name;                   // Human-readable name e.g. "Arducam 16MP AF (IMX519)"
    std::string description;            // e.g. "/dev/video0 · MJPG/YUYV", "libcamera / CSI"
    std::string pathOrPipeline;         // Device path or GStreamer pipeline string
    CameraType type = CAM_TYPE_V4L2;
    bool isArducam = false;             // True if identified as Arducam (CSI or USB)
    bool isRaspberryPi = false;         // True if identified as Raspberry Pi camera
    bool supportsAutofocus = false;     // True if hardware supports autofocus (e.g. IMX519, 64MP, IMX708, USB AF)
};

struct CameraConfig {
    int deviceId = 0;                   // Active camera ID from CameraDeviceInfo
    CameraType type = CAM_TYPE_V4L2;
    int width = 1280;                   // Requested capture width (0 = camera default)
    int height = 720;                   // Requested capture height (0 = camera default)
    int fps = 30;                       // Requested framerate (0 = camera default)
    int fourcc = 0;                     // 0 = Auto, 1 = MJPG, 2 = YUYV
    bool autoFocus = true;              // Hardware autofocus toggle (if supported)
    int manualFocus = 500;              // Manual focus distance value (0..1023)
    std::string customPipeline;         // User-specified GStreamer pipeline or path
    std::string cameraName;             // Active camera display name
    bool isArducam = false;
    bool isRaspberryPi = false;
    bool supportsAutofocus = false;
};

struct DetectionStats {
    int contours = 0;
    int triangles = 0, squares = 0, rectangles = 0, pentagons = 0, hexagons = 0, circles = 0, others = 0;
    int houghCircles = 0, lines = 0, corners = 0, keypoints = 0;
    int objects = 0, eyes = 0;
    std::string cascadeError;
    std::vector<std::string> qrCodes;
    std::vector<int> arucoIds;
    std::string arucoError;
    bool  tracked = false;
    float trackX = 0, trackY = 0;        // position at full camera resolution
    float trackErrX = 0, trackErrY = 0;  // offset from image center, -1..1 (useful for steering)
    float trackArea = 0;
    int   motionRegions = 0;
    float motionPercent = 0;             // % of pixels in motion
    bool  histValid = false;
    int   histChannels = 0;              // 1 = gray, 3 = B,G,R
    std::array<std::array<float, 256>, 3> hist{};
    double processingMs = 0.0;
};

// Input = raw camera frame, Output = frame after the processing pipeline.
struct CameraTextures {
    GLuint input = 0;
    GLuint output = 0;
    int width = 0, height = 0;           // input size
    int outWidth = 0, outHeight = 0;     // output size (depends on scale)
};

// Camera enumeration & control
std::vector<int> GetAvailableCameraIndices();
const std::vector<CameraDeviceInfo>& GetAvailableCameraDevices();
void RescanCameraDevices();
const CameraDeviceInfo* GetCameraDeviceInfo(int id);
CameraConfig& GetCameraConfig();

bool InitializeWebcam(cv::VideoCapture& cap, CameraTextures& tex, int index);
bool InitializeCameraWithConfig(cv::VideoCapture& cap, CameraTextures& tex, const CameraConfig& config);
bool SetCameraFocus(cv::VideoCapture& cap, bool autoFocus, int manualVal);
bool TriggerCameraAutoFocus(cv::VideoCapture& cap);
std::string BuildLibcameraPipeline(int width, int height, int fps, const std::string& cameraName = "");
void UpdateWebcamTextures(cv::VideoCapture& cap, CameraTextures& tex, const EffectsSettings& settings,
                          RuntimeControl& control, DetectionStats& stats);
void DestroyTextures(CameraTextures& tex);

// Helpers shared with the node editor
const cv::Mat& GetLastFrame();                          // last captured (unmirrored) camera frame
void UploadImageTexture(GLuint& id, const cv::Mat& img); // creates the texture if id == 0; 1/3/4 channels
std::string FindCascadeFile(int cascadeType);           // full path of a Haar cascade, empty if not found
#endif
