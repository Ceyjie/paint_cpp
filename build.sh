#!/bin/bash
ARCH=$(uname -m)
if [ "$ARCH" = "armv7l" ] || [ "$ARCH" = "armv6l" ] || [ "$ARCH" = "aarch64" ]; then
    # Orange Pi / ARM optimizations
    CXXFLAGS="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -ffast-math"
fi

g++ -std=c++17 -O2 $CXXFLAGS \
    -I/usr/include/libevdev-1.0 -o pipaint \
    drawingcanvas.cpp touchhandler.cpp main.cpp \
    $(pkg-config --cflags --libs sdl2) \
    -lSDL2_image -lSDL2_ttf -levdev -lpthread
