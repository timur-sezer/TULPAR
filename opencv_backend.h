#ifndef OPENCV_BACKEND_H
#define OPENCV_BACKEND_H
#include "dependencies/include/glad/glad.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

std::vector<int> GetAvailableCameraIndices();
bool InitializeWebcam(cv::VideoCapture& cap, GLuint& textureID, int index);
void UpdateWebcamTexture(cv::VideoCapture& cap, GLuint textureID);


#endif