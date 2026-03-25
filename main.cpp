#include "dependencies/include/glad/glad.h"
#include "dependencies/imgui/imgui.h"
#include "dependencies/imgui/imgui_impl_glfw.h"
#include "dependencies/imgui/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <opencv2/opencv.hpp>
#include "opencv_backend.h"
#include <vector>
#include <string>



int main(void) {
    
    setenv("OPENCV_VIDEOIO_PRIORITY_MSMF", "0", 1); // For Windows if needed
    setenv("OPENCV_VIDEOIO_PRIORITY_OBSENSOR", "0", 1); // This kills the error you see
    
    // 2. Standard OpenCV logging silence
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);

    
    if (!glfwInit()) return 1;

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1024, 768, "Laptop Webcam Switcher", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Camera Variables
    cv::VideoCapture cap;
    GLuint textureID = 0;
    int currentCameraIndex = -1;

    // --- FIX: Only declare availableCameras once ---
    std::vector<int> availableCameras = GetAvailableCameraIndices();

    if (!availableCameras.empty()) {
        currentCameraIndex = availableCameras[0];
        bool success = InitializeWebcam(cap, textureID, currentCameraIndex);
        if(!success) printf("Failed to initialize camera index %d\n", currentCameraIndex);
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (cap.isOpened()) {
            UpdateWebcamTexture(cap, textureID);
        }

        ImGui::Begin("Camera Settings");
        if (ImGui::Button("Refresh Device List")) {
            availableCameras = GetAvailableCameraIndices();
        }
        ImGui::Separator();

        if (availableCameras.empty()) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "No cameras detected!");
            ImGui::TextWrapped("Tip: Check Windows Privacy Settings for Camera Access.");
        } else {
            std::string comboLabel = "Port: " + std::to_string(currentCameraIndex);
            if (ImGui::BeginCombo("Select Camera", comboLabel.c_str())) {
                for (int i : availableCameras) {
                    bool isSelected = (currentCameraIndex == i);
                    std::string itemLabel = "Camera Index " + std::to_string(i);
                    if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                        currentCameraIndex = i;
                        InitializeWebcam(cap, textureID, currentCameraIndex);
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::End();

        ImGui::Begin("Webcam Feed");
        if (cap.isOpened() && textureID != 0) {
            float width = ImGui::GetContentRegionAvail().x;
            float height = width * 0.75f; 
            ImGui::Image((void*)(intptr_t)textureID, ImVec2(width, height));
        } else {
            ImGui::Text("No active camera feed.");
        }
        ImGui::End();
        ImGui::ShowDemoWindow();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    cap.release();
    if (textureID != 0) glDeleteTextures(1, &textureID);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}