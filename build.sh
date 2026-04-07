#!/bin/bash
g++ -std=c++17 -O2 -I/usr/include/libevdev-1.0 -o pipaint \
    drawingcanvas.cpp touchhandler.cpp main.cpp \
    $(pkg-config --cflags --libs sdl2) \
    -lSDL2_image -lSDL2_ttf -levdev -lpthread