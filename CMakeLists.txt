cmake_minimum_required(VERSION 3.12)
project(ROVER_Y)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find Packages
find_package(OpenCV REQUIRED)
# If GLFW is installed on your system, use:
# find_package(glfw3 REQUIRED) 

# 2. Glad Library
add_library(glad STATIC dependencies/glad.c)
target_include_directories(glad PUBLIC dependencies/include)

# 3. Imgui Library
add_library(imgui STATIC 
    dependencies/imgui/imgui.cpp
    dependencies/imgui/imgui_tables.cpp
    dependencies/imgui/imgui_draw.cpp
    dependencies/imgui/imgui_demo.cpp
    dependencies/imgui/imgui_widgets.cpp
    dependencies/imgui/imgui_impl_glfw.cpp
    dependencies/imgui/imgui_impl_opengl3.cpp
)
# Ensure ImGui can find its own headers and Glad/GLFW headers
target_include_directories(imgui PUBLIC dependencies/imgui dependencies/include)

# 4. Main Executable
add_executable(ROVER_Y 
    main.cpp
    opencv_backend.h
    opencv_backend.cpp
    imgui_opengl.h
    imgui_opengl.cpp)

# 5. Linking
# If using manual paths for GLFW:
target_include_directories(ROVER_Y PRIVATE dependencies/include)
target_link_directories(ROVER_Y PRIVATE dependencies/lib)

target_link_libraries(ROVER_Y PRIVATE 
    glad 
    imgui 
    ${OpenCV_LIBS} 
    glfw      # Ensure this matches the filename (e.g., glfw3 on some systems)
    ${CMAKE_DL_LIBS}
)

# Optional: Link OpenGL (usually needed for Glad/ImGui)
find_package(OpenGL REQUIRED)
target_link_libraries(ROVER_Y PRIVATE OpenGL::GL)
