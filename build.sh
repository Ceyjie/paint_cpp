#!/bin/bash
# For Orange Pi One (ARM Cortex-A7) cross-compilation
# Install toolchain: sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
# Then compile with: arm-linux-gnueabihf-g++ ...

# Check if cross-compiling
if [ -n "$CROSS_COMPILE" ]; then
    # Cross-compilation for Orange Pi One
    CFLAGS="-O3 -march=armv7-a -mfloat-abi=hard -mfpu=neon-vfpv4 -mtune=cortex-a7 -std=c++17"
    arm-linux-gnueabihf-g++ $CFLAGS -o pipaint drawingcanvas.cpp touchhandler.cpp main.cpp \
        -I/usr/include/arm-linux-gnueabihf/SDL2 -lSDL2_image -lSDL2_ttf -levdev
else
    # Host compilation (x86_64/aarch64)
    CFLAGS="-O3 -flto -std=c++17 -I/usr/include/libevdev-1.0"
    LDFLAGS="-flto"

    g++ $CFLAGS -o pipaint drawingcanvas.cpp touchhandler.cpp main.cpp \
        `sdl2-config --cflags --libs` -lSDL2_image -lSDL2_ttf -levdev $LDFLAGS
fi