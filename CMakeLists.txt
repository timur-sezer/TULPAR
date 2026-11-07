cmake_minimum_required(VERSION 3.16...3.31)
project(TULPAR LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------
# OpenCV 4.x or 5.x. On Windows point CMake at it with -DOpenCV_DIR=<opencv>/build
find_package(OpenCV REQUIRED)
message(STATUS "OpenCV ${OpenCV_VERSION}")

set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# GLFW, in order of preference:
#   1. Windows + MSVC x64: the prebuilt dependencies/glfw/lib/glfw3.lib (GLFW 3.3.8, /MD)
#   2. A system GLFW (brew install glfw / apt install libglfw3-dev / vcpkg)
#   3. Download and build GLFW from source (needs internet at configure time)
option(ROVER_USE_BUNDLED_GLFW "Use the prebuilt glfw3.lib on Windows/MSVC" ON)
set(GLFW_TARGET "")
if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND ROVER_USE_BUNDLED_GLFW
   AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/glfw/lib/glfw3.lib)
    add_library(glfw_bundled STATIC IMPORTED)
    set_target_properties(glfw_bundled PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/glfw/lib/glfw3.lib
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/glfw/include
        INTERFACE_LINK_LIBRARIES "gdi32;user32;shell32")
    set(GLFW_TARGET glfw_bundled)
    message(STATUS "GLFW: bundled glfw3.lib")
else()
    find_package(glfw3 3.3 QUIET)
    if(glfw3_FOUND)
        set(GLFW_TARGET glfw)
        message(STATUS "GLFW: system package ${glfw3_VERSION}")
    else()
        message(STATUS "GLFW: not found on the system, fetching 3.4 from GitHub")
        include(FetchContent)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        # X11 only by default (works on Wayland desktops via XWayland) so no
        # Wayland dev packages are needed. Override with -DGLFW_BUILD_WAYLAND=ON.
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Build GLFW Wayland support")
        FetchContent_Declare(glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(glfw)
        set(GLFW_TARGET glfw)
    endif()
endif()

# ------------------------------------------------------------------
# Third-party libraries (vendored sources)
# ------------------------------------------------------------------
add_library(glad STATIC dependencies/glad.c)
target_include_directories(glad PUBLIC dependencies/include)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})

add_library(imgui STATIC
    dependencies/imgui/imgui.cpp
    dependencies/imgui/imgui_tables.cpp
    dependencies/imgui/imgui_draw.cpp
    dependencies/imgui/imgui_widgets.cpp
    dependencies/imgui/imgui_impl_glfw.cpp
    dependencies/imgui/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC dependencies/imgui)
target_link_libraries(imgui PUBLIC ${GLFW_TARGET} OpenGL::GL)

# ------------------------------------------------------------------
# Application
# ------------------------------------------------------------------
add_executable(TULPAR
    main.cpp
    Windows/opencv_backend.h
    Windows/opencv_backend.cpp
    Windows/imgui_opengl.h
    Windows/imgui_opengl.cpp)

target_include_directories(TULPAR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(TULPAR PRIVATE glad imgui ${GLFW_TARGET} OpenGL::GL ${OpenCV_LIBS})

# Location of OpenCV's Haar cascade XML files (used by the object detector).
find_path(OPENCV_CASCADE_DIR haarcascade_frontalface_default.xml
    HINTS ${OpenCV_INSTALL_PATH}/share ${OpenCV_DIR}/../../../share ${OpenCV_DIR}/etc ${OpenCV_DIR}/../etc
    PATH_SUFFIXES opencv5/haarcascades opencv4/haarcascades opencv/haarcascades haarcascades
    PATHS /usr/share /usr/local/share /opt/homebrew/share)
if(OPENCV_CASCADE_DIR)
    message(STATUS "Haar cascades: ${OPENCV_CASCADE_DIR}")
    target_compile_definitions(TULPAR PRIVATE OPENCV_CASCADE_DIR="${OPENCV_CASCADE_DIR}")
else()
    message(WARNING "Haar cascades not found; object detection needs OPENCV_CASCADE_DIR at runtime")
endif()

# ------------------------------------------------------------------
# Platform specifics
# ------------------------------------------------------------------
if(MSVC)
    target_compile_definitions(TULPAR PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(TULPAR PRIVATE /W3 /utf-8)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT TULPAR)
endif()

if(WIN32)
    # Copy the OpenCV DLLs next to the .exe so it runs without editing PATH.
    if(NOT CMAKE_VERSION VERSION_LESS 3.21)
        add_custom_command(TARGET TULPAR POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:TULPAR> $<TARGET_FILE_DIR:TULPAR>
            COMMAND_EXPAND_LISTS)
    endif()
endif()

if(APPLE)
    target_compile_definitions(TULPAR PRIVATE GL_SILENCE_DEPRECATION)
    target_compile_definitions(imgui PRIVATE GL_SILENCE_DEPRECATION)

    # macOS only shows the camera permission prompt for an app bundle whose
    # Info.plist has NSCameraUsageDescription, so build TULPAR as TULPAR.app.
    enable_language(OBJCXX)
    target_sources(TULPAR PRIVATE macos/camera_permission.mm)
    target_link_libraries(TULPAR PRIVATE "-framework AVFoundation" "-framework Foundation")
    set_target_properties(TULPAR PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER com.tulpar.vision
        MACOSX_BUNDLE_BUNDLE_NAME TULPAR)
    # Ad-hoc sign the whole bundle so the Info.plist is sealed into the signature
    # (macOS identifies the app for camera permission by its signature).
    add_custom_command(TARGET TULPAR POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:TULPAR>
        VERBATIM)
endif()
