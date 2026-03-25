#ifndef IMGUI_OPENGL_H
#define IMGUI_OPENGL_H

#include "dependencies/include/glad/glad.h" 
#include <GLFW/glfw3.h>
#include "opencv_backend.h"

// UI logic
void InitImGui(GLFWwindow* window, const char* glsl_version);
void RenderUI(cv::VideoCapture& cap, GLuint& textureID, int& currentCameraIndex, std::vector<int>& availableCameras, EffectsSettings& settings);
void CleanupImGui();

#endif