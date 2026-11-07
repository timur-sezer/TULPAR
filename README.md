# TULPAR

A real-time webcam vision tool built with **OpenCV** and **Dear ImGui** (OpenGL + GLFW).
It shows the live camera feed next to a processed version. You can chain filters and detectors
and tune every parameter with sliders, then watch the result change on the next frame.

It's meant for experimenting with computer vision for a robot or rover: finding shapes,
following a coloured object, reading markers and QR codes, and detecting motion.

```
┌──────────────────┬─────────────────────┬─────────────────────┐
│ Camera Settings  │                     │                     │
├──────────────────┤     Input Feed      │     Output Feed     │
│ Image Processing │  (raw camera image) │ (processed + drawn  │
│  Filters│Detect  │                     │   detections)       │
│  Objects│Tracking│                     │                     │
└──────────────────┴─────────────────────┴─────────────────────┘
```

All panels are docked when the app starts. You can drag the tabs to rearrange them, and
the next launch goes back to this layout.

---

## Features

### Camera Settings
- Choose between the connected cameras and refresh the list.
- **Pause** freezes the current frame. Settings still apply to it, so you can tune them on a still image.
- **Snapshot** saves the input and output images to `~/Pictures/TULPAR/`.
- Mirror toggle, plus camera and processing resolution.

### Image Processing
A **Results** section at the top shows processing time, FPS and counts for everything that's
detected, plus an optional live colour histogram. The **Output View** setting chooses what
the Output Feed shows:

| Output View | Shows |
|---|---|
| Processed | The colour image after the filters |
| Binary / Edges | The black-and-white result of the mask, threshold, edge and morphology steps |
| Original + Overlay | The untouched frame with detections drawn on top |
| Motion Mask | The moving areas found by motion detection |

**Processing Scale** runs the pipeline at a lower resolution. It's the main way to speed things up.

#### Filters tab
| Section | Options |
|---|---|
| Pre-processing | Brightness, contrast, gamma, sharpen, CLAHE (evens out uneven lighting), histogram equalisation, invert, blur (Gaussian, box, median, bilateral) |
| Color | BGR, grayscale or HSV view; an **HSV colour mask** with colour presets |
| Threshold | Binary, binary inverted, Otsu (automatic), adaptive mean and adaptive Gaussian |
| Edge Detection | Canny, Sobel, Laplacian |
| Morphology | Erode, dilate, open, close, gradient, with a choice of kernel shape and size |

#### Detect tab
| Section | Options |
|---|---|
| Contours & Shapes | Labels shapes as **triangle, square, rectangle, pentagon, hexagon or circle**. Can also draw bounding and rotated boxes, the smallest enclosing circle, the convex hull, size/perimeter/angle labels and centres |
| Hough Circles / Lines | Finds circles and straight line segments |
| Corners | Shi-Tomasi corner detection |
| Feature Keypoints | ORB, FAST or SIFT |

#### Objects tab
| Section | Options |
|---|---|
| Cascade Detector | Haar detectors: face (with eyes), profile face, smile, upper body, full body, cat face, licence plate |
| QR Codes | Detects several codes at once and decodes their text |
| ArUco / AprilTag | Finds fiducial markers and shows their IDs (needs OpenCV 4.7+) |

#### Tracking tab
| Section | Options |
|---|---|
| Color Tracking | Follows the largest blob of a chosen colour and draws a trail. Reports the blob's **offset from the image centre (dx/dy, -1 to 1)**, which you can use to steer a rover |
| Motion Detection | Background subtraction (MOG2) with adjustable sensitivity, learning rate and minimum region size |

Hover over any **(?)** in the app to see what that setting does.

### Processing order
Each frame goes through these steps in order:

```
camera → scale → brightness/contrast/gamma/CLAHE/sharpen/invert → blur → grayscale
       → [HSV mask] → [threshold] → [edges] → [morphology]      (black-and-white image)
       → detections drawn on the Output Feed
```

If you turn on shape detection without a mask, threshold or edge step, Canny edge detection is used automatically.

---

## Quick start: detect shapes

1. Open **Detect → Contours & Shape Detection** and tick **Detect Shapes**.
2. Hold a printed shape in front of the camera.
3. If the result is noisy, open the **Filters** tab and set **Threshold → Otsu** and **Morphology → Close**.
4. Set **Output View → Binary / Edges** to see what the detector is working on.

To follow a coloured object: **Tracking → Track Color**, click a colour preset, then
adjust the Hue, Saturation and Value ranges until only the object is picked up.

---

## Building

### Requirements
- CMake 3.16 or newer
- A C++17 compiler: MSVC 2019+, GCC 9+ or Clang 10+
- OpenCV 4.x or 5.x. ArUco markers need 4.7 or newer; on older versions everything else still works.
- GLFW 3.3+ is optional:
  - On Windows with MSVC, the bundled `glfw3.lib` is used.
  - Otherwise an installed GLFW is used.
  - If none is installed, GLFW is downloaded and built automatically. That needs internet the first time you run CMake.

### macOS

```sh
brew install cmake opencv glfw
cmake -S . -B build
cmake --build build -j
open build/TULPAR.app
```

On macOS the app is built as `TULPAR.app`. Start it with `open` or from Finder so macOS
shows the **camera permission** prompt. If you click Don't Allow, turn it back on under
*System Settings → Privacy & Security → Camera*.

> The app is signed with a temporary signature that changes on every build, so macOS may
> ask for camera permission again after each rebuild.

### Linux (Ubuntu / Debian)

```sh
sudo apt install build-essential cmake libopencv-dev opencv-data libgl1-mesa-dev libglfw3-dev
cmake -S . -B build
cmake --build build -j
./build/TULPAR
```

`opencv-data` provides the Haar files used by the face and object detectors. Other
distributions need the same packages under their own names (OpenCV, GLFW, OpenGL/Mesa
development files).

#### Docker

```sh
docker build -t tulpar .

# run with the host display and webcam
xhost +local:docker
docker run --rm -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \
    --device /dev/video0 tulpar
```

### Windows (Visual Studio 2019/2022, x64)

1. Install OpenCV. Any of these works:
   - `choco install opencv` (installs to `C:\tools\opencv`)
   - the official Windows release from opencv.org
   - `vcpkg install opencv4`
2. Configure and build, pointing CMake at the folder that contains `OpenCVConfig.cmake`:

```bat
cmake -S . -B build -A x64 -DOpenCV_DIR=C:/tools/opencv/build
cmake --build build --config Release
build\Release\TULPAR.exe
```

With vcpkg, pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake`
instead of `OpenCV_DIR`. CMake 3.21+ copies the OpenCV DLLs next to `TULPAR.exe`
automatically. With an older CMake, add OpenCV's `bin` folder to `PATH`.

### Build options

| Option | Default | Meaning |
|---|---|---|
| `-DROVER_USE_BUNDLED_GLFW=OFF` | `ON` | On Windows/MSVC, don't use the bundled `glfw3.lib`; use an installed or downloaded GLFW instead |
| `-DGLFW_BUILD_WAYLAND=ON` | `OFF` | Build the downloaded GLFW with Wayland support. The default X11 build also runs on Wayland desktops through XWayland |

If the Haar files aren't found while building, set the `OPENCV_CASCADE_DIR` environment
variable to the `haarcascades` folder before running the app.

### Continuous integration
`.github/workflows/build.yml` builds the project on Linux, macOS and Windows on every push.

---

## Troubleshooting

| Problem | Fix |
|---|---|
| "No cameras detected!" on macOS | Allow camera access for TULPAR in *System Settings → Privacy & Security → Camera*, then click **Refresh Devices** |
| "No cameras detected!" on Linux | Make sure your user can open `/dev/video*` (usually by joining the `video` group) |
| "Cascade not found" | Install `opencv-data` (Linux) or set `OPENCV_CASCADE_DIR` |
| "ArUco needs OpenCV 4.7+" | Upgrade OpenCV; every other feature still works |
| Low FPS | Lower **Processing Scale** and turn off detectors you aren't using. Bilateral blur and SIFT are the slowest |
| `TULPAR.exe` fails to start with a missing `opencv_world*.dll` | Use CMake 3.21+ or add OpenCV's `bin` folder to `PATH` |

---

## Project layout

```
main.cpp                    entry point: window, OpenGL context, main loop
Windows/                    app code (cross-platform, despite the name)
  opencv_backend.h/.cpp     camera capture and the processing/detection steps
  imgui_opengl.h/.cpp       docked ImGui interface
macos/
  Info.plist.in             app bundle settings (camera permission text)
  camera_permission.mm      asks for camera access before startup
dependencies/
  glad.c, include/          OpenGL loader (glad)
  imgui/                    Dear ImGui (docking branch) with GLFW/OpenGL3 backends
  glfw/                     prebuilt GLFW 3.3.8 for Windows/MSVC
Dockerfile                  Linux build image
.github/workflows/build.yml CI for Linux, macOS and Windows
```

## Built with
- [OpenCV](https://opencv.org/): computer vision
- [Dear ImGui](https://github.com/ocornut/imgui) (docking branch): user interface
- [GLFW](https://www.glfw.org/): window and input
- [glad](https://github.com/Dav1dde/glad): OpenGL loader
