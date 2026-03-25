#include "opencv_backend.h"
#include <opencv2/core/utils/logger.hpp>

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

bool InitializeWebcam(cv::VideoCapture& cap, GLuint& textureID, int index) {
    if (cap.isOpened()) cap.release();
    
    cap.open(index, cv::CAP_ANY); 
    if (!cap.isOpened()) return false;

    static bool textureGenerated = false;
    if (!textureGenerated) {
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        textureGenerated = true;
    }
    return true;
}

// ... other functions ...

void UpdateWebcamTexture(cv::VideoCapture& cap, GLuint textureID, EffectsSettings& settings) {
    cv::Mat frame, processed;
    cap >> frame;
    if (frame.empty()) return;

    cv::flip(frame, frame, 1);
    processed = frame.clone();

    // 1. Handle Blur
    int ksize = settings.blurSize * 2 + 1; 
    if (settings.blurMethod == GAUSSIAN) {
        cv::GaussianBlur(processed, processed, cv::Size(ksize, ksize), 0);
    } else if (settings.blurMethod == BOX) {
        cv::blur(processed, processed, cv::Size(ksize, ksize));
    } else if (settings.blurMethod == MEDIAN) {
        cv::medianBlur(processed, processed, ksize);
    }

    // 2. Color Conversions
    if (settings.showHSV) {
        cv::cvtColor(processed, processed, cv::COLOR_BGR2HSV);
    } else if (!settings.showRGB) {
        cv::cvtColor(processed, processed, cv::COLOR_BGR2GRAY);
        cv::cvtColor(processed, processed, cv::COLOR_GRAY2BGR); 
    }

    // 3. Brightness
    processed.convertTo(processed, -1, settings.brightness, 0);

    // 4. Send to OpenGL
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, processed.cols, processed.rows, 0, GL_BGR, GL_UNSIGNED_BYTE, processed.ptr());
}