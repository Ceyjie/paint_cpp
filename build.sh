#!/bin/bash
ARCH=$(uname -m)

if [ "$ARCH" = "armv7l" ] || [ "$ARCH" = "armv6l" ] || [ "$ARCH" = "aarch64" ]; then
    CXXFLAGS="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -ffast-math"
    CFLAGS="-I/usr/include/libevdev-1.0"
    LIBS="-levdev"
else
    CXXFLAGS=""
    CFLAGS=""
    LIBS=""
fi

# Compile drawingcanvas and main
g++ -std=c++17 -O2 $CXXFLAGS $CFLAGS -c drawingcanvas.cpp
g++ -std=c++17 -O2 $CXXFLAGS $CFLAGS -c main.cpp

# Try to compile touchhandler (may fail on non-ARM platforms)
if [ "$ARCH" = "armv7l" ] || [ "$ARCH" = "armv6l" ] || [ "$ARCH" = "aarch64" ]; then
    g++ -std=c++17 -O2 $CXXFLAGS $CFLAGS -c touchhandler.cpp
fi

# Link
if [ -f touchhandler.o ]; then
    g++ -std=c++17 -O2 drawingcanvas.o touchhandler.o main.o \
        $(pkg-config --cflags --libs sdl2) \
        -lSDL2_image -lSDL2_ttf $LIBS -lpthread -o pipaint
else
    # Create stub touch handler for desktop
    cat > touchhandler_stub.cpp << 'EOF'
#include "touchhandler.h"
TouchHandler::TouchHandler(int, int) {}
TouchHandler::~TouchHandler() {}
bool TouchHandler::init() { return false; }
void TouchHandler::processEvents(std::vector<SDL_Event>&) {}
EOF
    g++ -std=c++17 -O2 -c touchhandler_stub.cpp
    g++ -std=c++17 -O2 drawingcanvas.o touchhandler_stub.o main.o \
        $(pkg-config --cflags --libs sdl2) \
        -lSDL2_image -lSDL2_ttf -lpthread -o pipaint
    rm -f touchhandler_stub.cpp touchhandler_stub.o
fi

echo "Build complete: ./pipaint"
