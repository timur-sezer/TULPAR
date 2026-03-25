#include "imgui_opengl.h"
#include "dependencies/imgui/imgui.h"
#include "dependencies/imgui/imgui_impl_glfw.h"
#include "dependencies/imgui/imgui_impl_opengl3.h"
#include <string>

void InitImGui(GLFWwindow* window, const char* glsl_version) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
}

void RenderUI(cv::VideoCapture& cap, GLuint& textureID, int& currentIdx, 
              std::vector<int>& cams, EffectsSettings& settings) {
    
    // --- 1. START NEW FRAME (Required to fix the crash) ---
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // --- 2. CAMERA SETTINGS WINDOW ---
    ImGui::Begin("Camera Settings");
    if (ImGui::Button("Refresh Device List")) {
        cams = GetAvailableCameraIndices();
    }
    ImGui::Separator();

    if (cams.empty()) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "No cameras detected!");
    } else {
        std::string comboLabel = "Port: " + std::to_string(currentIdx);
        if (ImGui::BeginCombo("Select Camera", comboLabel.c_str())) {
            for (int i : cams) {
                bool isSelected = (currentIdx == i);
                std::string itemLabel = "Camera Index " + std::to_string(i);
                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                    currentIdx = i;
                    InitializeWebcam(cap, textureID, currentIdx);
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::End();

    // --- 3. IMAGE PROCESSING WINDOW ---
    ImGui::Begin("Image Processing");
    
    ImGui::Text("Blur Settings");
    const char* blurNames[] = { "None", "Gaussian", "Box", "Median" };
    ImGui::Combo("Method", &settings.blurMethod, blurNames, IM_ARRAYSIZE(blurNames));
    ImGui::SliderInt("Blur Strength", &settings.blurSize, 1, 15);
    
    ImGui::Separator();
    ImGui::Text("Color Filters");
    if (ImGui::Checkbox("HSV Mode", &settings.showHSV)) {
        if (settings.showHSV) settings.showRGB = false;
    }
    if (ImGui::Checkbox("RGB Mode", &settings.showRGB)) {
        if (settings.showRGB) settings.showHSV = false;
    }

    ImGui::SliderFloat("Brightness", &settings.brightness, 0.0f, 3.0f);
    ImGui::End();

    // --- 4. WEBCAM FEED WINDOW ---
    ImGui::Begin("Webcam Feed");
    if (cap.isOpened() && textureID != 0) {
        float width = ImGui::GetContentRegionAvail().x;
        float height = width * 0.75f; 
        ImGui::Image((void*)(intptr_t)textureID, ImVec2(width, height));
    } else {
        ImGui::Text("No active camera feed.");
    }
    ImGui::End();
}

void CleanupImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}