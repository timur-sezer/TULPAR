# Linux build of TULPAR (Ubuntu 22.04, OpenCV 4.5, GLFW 3.3).
#
#   docker build -t tulpar .
#
# Run with access to the host X server and webcam:
#   xhost +local:docker
#   docker run --rm -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \
#       --device /dev/video0 tulpar
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        libopencv-dev \
        opencv-data \
        libgl1-mesa-dev \
        libglfw3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app/

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j"$(nproc)"

CMD ["./build/TULPAR"]
