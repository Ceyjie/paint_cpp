#!/bin/bash
# For host (x86_64/aarch64) testing - use these flags
# For Orange Pi cross-compilation, use: -march=armv7-a -mfloat-abi=hard -mfpu=neon-vfpv4 -mtune=cortex-a7
CFLAGS="-O3 -flto -std=c++17 -I/usr/include/libevdev-1.0"
LDFLAGS="-flto"

g++ $CFLAGS -o pipaint drawingcanvas.cpp touchhandler.cpp main.cpp \
    `sdl2-config --cflags --libs` -lSDL2_image -lSDL2_ttf -levdev $LDFLAGS