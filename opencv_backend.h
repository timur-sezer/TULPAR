#ifndef OPENCV_BACKEND_H
#define OPENCV_BACKEND_H

#include "dependencies/include/glad/glad.h"
#include <opencv2/opencv.hpp>
#include <vector>

// Camera logic
enum BlurMethod { NONE = 0, GAUSSIAN, BOX, MEDIAN };

struct EffectsSettings {
    int blurMethod = NONE;
    int blurSize = 1;     // Must be odd for Gaussian
    bool showHSV = false;
    bool showRGB = true;
    float brightness = 1.0f;
};

std::vector<int> GetAvailableCameraIndices();
bool InitializeWebcam(cv::VideoCapture& cap, GLuint& textureID, int index);
void UpdateWebcamTexture(cv::VideoCapture& cap, GLuint textureID, EffectsSettings& settings);
#endif