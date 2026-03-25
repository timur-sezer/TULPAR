#include "dependencies/include/glad/glad.h"
#include <GLFW/glfw3.h>
#include <opencv2/core/utils/logger.hpp>
#include "opencv_backend.h"
#include "imgui_opengl.h"
#include "dependencies/imgui/imgui.h"
#include "dependencies/imgui/imgui_impl_opengl3.h"

int main(void) {
    // 1. Environment and Logging
#ifdef _WIN32
    _putenv("OPENCV_VIDEOIO_PRIORITY_MSMF=0");
    _putenv("OPENCV_VIDEOIO_PRIORITY_OBSENSOR=0");
#endif
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);

    // 2. Setup GLFW/GLAD
    if (!glfwInit()) return 1;
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1024, 768, "Laptop Webcam Switcher", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;

    // 3. Initialize Modules
    InitImGui(window, glsl_version);
    
    cv::VideoCapture cap;
    GLuint textureID = 0;
    int currentCameraIndex = -1;
    std::vector<int> availableCameras = GetAvailableCameraIndices();

    if (!availableCameras.empty()) {
        currentCameraIndex = availableCameras[0];
        InitializeWebcam(cap, textureID, currentCameraIndex);
    }


    EffectsSettings settings;
    // 4. Main Loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        if (cap.isOpened()) {
            UpdateWebcamTexture(cap, textureID, settings);
        }

        RenderUI(cap, textureID, currentCameraIndex, availableCameras, settings);

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // 5. Cleanup
    cap.release();
    if (textureID != 0) glDeleteTextures(1, &textureID);
    CleanupImGui();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}