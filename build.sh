#!/bin/bash
ARCH=$(uname -m)
if [ "$ARCH" = "armv7l" ] || [ "$ARCH" = "armv6l" ] || [ "$ARCH" = "aarch64" ]; then
    # Orange Pi / ARM optimizations - Cortex-A7 specific NEON
    CXXFLAGS="-march=armv7-a+neon-vfpv4 -mtune=cortex-a7 -mfloat-abi=hard -mfpu=neon-vfpv4 -ffast-math"
fi

g++ -std=c++17 -O2 $CXXFLAGS \
    -I/usr/include/libevdev-1.0 -o pipaint \
    drawingcanvas.cpp touchhandler.cpp main.cpp \
    $(pkg-config --cflags --libs sdl2) \
    -lSDL2_image -lSDL2_ttf -levdev -lpthread
