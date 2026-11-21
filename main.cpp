#include "dependencies/include/glad/glad.h"
#include <GLFW/glfw3.h>
#include <opencv2/core/utils/logger.hpp>
#include "Windows/opencv_backend.h"
#include "Windows/imgui_opengl.h"
#include "dependencies/imgui/imgui.h"
#include "dependencies/imgui/imgui_impl_opengl3.h"
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef __APPLE__
bool RequestCameraAccess(); // macos/camera_permission.mm
#endif

int main(void) {
    // 1. Environment and Logging
#ifdef _WIN32
    _putenv("OPENCV_VIDEOIO_PRIORITY_MSMF=0");
    _putenv("OPENCV_VIDEOIO_PRIORITY_OBSENSOR=0");
#else
    // If any locale variable is set to bare "UTF-8" (or empty), libxkbcommon fails
    // to find a compose table on Wayland, aborting GLFW initialization.
    for (const char* var : { "LC_ALL", "LC_CTYPE", "LANG", "LC_MESSAGES" }) {
        const char* val = std::getenv(var);
        if (val && (!strcmp(val, "UTF-8") || !strcmp(val, "utf8") || !strcmp(val, ""))) {
            unsetenv(var);
        }
    }
    setenv("LC_ALL", "C.UTF-8", 0);
    setenv("LANG", "C.UTF-8", 0);

    if (!std::getenv("GST_DEBUG")) {
        setenv("GST_DEBUG", "1", 0); // warnings/criticals only, no info spam
    }
#endif
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);
#ifdef __APPLE__
    RequestCameraAccess(); // shows the macOS permission prompt and waits for the answer
#endif

    // 2. Setup GLFW/GLAD
    glfwSetErrorCallback([](int error, const char* description) {
        std::fprintf(stderr, "[GLFW Error %d]: %s\n", error, description ? description : "unknown");
    });

    if (!glfwInit()) {
        // If Wayland initialization failed (e.g. XKB compose table error in libglfw),
        // fall back to X11 (via XWayland) which works universally on Raspberry Pi.
        if (std::getenv("WAYLAND_DISPLAY")) {
            std::fprintf(stderr, "Native Wayland init failed; falling back to X11/XWayland...\n");
            unsetenv("WAYLAND_DISPLAY");
            if (!glfwInit()) {
                std::fprintf(stderr, "Failed to initialize GLFW (tried Wayland and X11)\n");
                return 1;
            }
        } else {
            std::fprintf(stderr, "Failed to initialize GLFW\n");
            return 1;
        }
    }
#ifdef __APPLE__
    // macOS only provides 3.2+ core, forward-compatible contexts
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1400, 800, "TULPAR", NULL, NULL);
    if (!window) {
        // Fallback to smaller resolutions for small screens / Pi displays
        window = glfwCreateWindow(1280, 720, "TULPAR", NULL, NULL);
    }
    if (!window) {
        window = glfwCreateWindow(1024, 600, "TULPAR", NULL, NULL);
    }
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "Failed to initialize GLAD\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // 3. Initialize Modules
    InitImGui(window, glsl_version);
    
    cv::VideoCapture cap;
    CameraTextures textures;
    int currentCameraIndex = -1;
    std::vector<int> availableCameras = GetAvailableCameraIndices();

    if (!availableCameras.empty()) {
        currentCameraIndex = availableCameras[0];
        InitializeWebcam(cap, textures, currentCameraIndex);
    }


    EffectsSettings settings;
    DetectionStats stats;
    RuntimeControl control;
    // 4. Main Loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        if (cap.isOpened()) {
            UpdateWebcamTextures(cap, textures, settings, control, stats);
        }

        RenderUI(cap, textures, currentCameraIndex, availableCameras, settings, control, stats);

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
    DestroyTextures(textures);
    CleanupImGui();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}