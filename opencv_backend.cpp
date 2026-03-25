#include "opencv_backend.h"

std::vector<int> GetAvailableCameraIndices() {
    std::vector<int> availableIndices;
    for (int i = 0; i < 5; ++i) {
        // We use CAP_ANY to let OpenCV decide, or CAP_MSMF for modern Windows laptops
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

void UpdateWebcamTexture(cv::VideoCapture& cap, GLuint textureID) {
    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) return;

    // Standard flip for webcams
    cv::flip(frame, frame, 1); 

    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frame.cols, frame.rows, 0, GL_BGR, GL_UNSIGNED_BYTE, frame.ptr());
}