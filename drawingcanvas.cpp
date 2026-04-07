#include "drawingcanvas.h"
#include <SDL2/SDL_image.h>
#include <cstring>
#include <queue>
#include <algorithm>
#include <cmath>
#include <memory>
#include <cstdint>

DrawingCanvas::DrawingCanvas(int w, int h) : width(w), height(h) {
    canvas = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    background = nullptr;
    currentColor = SDL_MapRGB(canvas->format, 0, 0, 0);
    backgroundColor = SDL_MapRGB(canvas->format, 255, 255, 255);
    penSize = 5;
    minSize = 1;
    maxSize = 50;
    eraserMode = false;
    fillMode = false;

    // Make canvas transparent
    SDL_FillRect(canvas, nullptr, SDL_MapRGBA(canvas->format, 0,0,0,0));
    pushState();
}

DrawingCanvas::~DrawingCanvas() {
    SDL_FreeSurface(canvas);
    if (background) SDL_FreeSurface(background);
    for (auto s : undoStack) SDL_FreeSurface(s);
    for (auto s : redoStack) SDL_FreeSurface(s);
}

void DrawingCanvas::clear() {
    // Clear drawing layer to transparent
    SDL_FillRect(canvas, nullptr, SDL_MapRGBA(canvas->format, 0,0,0,0));
    pushState();
}

void DrawingCanvas::resetToBlank() {
    // Clear drawing layer to transparent
    SDL_FillRect(canvas, nullptr, SDL_MapRGBA(canvas->format, 0,0,0,0));
    // Discard background
    if (background) {
        SDL_FreeSurface(background);
        background = nullptr;
    }
    // Reset undo/redo
    for (auto s : undoStack) SDL_FreeSurface(s);
    for (auto s : redoStack) SDL_FreeSurface(s);
    undoStack.clear();
    redoStack.clear();
    pushState();
}

void DrawingCanvas::setColor(Uint8 r, Uint8 g, Uint8 b) {
    currentColor = SDL_MapRGB(canvas->format, r, g, b);
    eraserMode = false;
    fillMode = false;
}

void DrawingCanvas::setSize(int size) {
    penSize = std::max(minSize, std::min(maxSize, size));
}

void DrawingCanvas::toggleEraser() {
    eraserMode = !eraserMode;
    fillMode = false;
}

void DrawingCanvas::toggleFill() {
    fillMode = !fillMode;
    if (fillMode) eraserMode = false;
}

void DrawingCanvas::toggleBackground() {
    // This toggles the background color of the drawing layer only
    // (does not affect loaded background image)
    Uint32 oldBg = backgroundColor;
    backgroundColor = (oldBg == SDL_MapRGB(canvas->format, 255,255,255)) ?
                       SDL_MapRGB(canvas->format, 0,0,0) :
                       SDL_MapRGB(canvas->format, 255,255,255);
    // Invert all non‑transparent pixels on the drawing layer
    Uint32* pixels = (Uint32*)canvas->pixels;
    int totalPixels = width * height;
    for (int i = 0; i < totalPixels; i++) {
        Uint32 pix = pixels[i];
        // Ignore transparent pixels (alpha 0)
        if ((pix >> 24) == 0) continue;
        if (pix == oldBg) {
            pixels[i] = backgroundColor;
        } else {
            Uint8 r = (pix >> 16) & 0xFF;
            Uint8 g = (pix >> 8) & 0xFF;
            Uint8 b = pix & 0xFF;
            pixels[i] = SDL_MapRGB(canvas->format, 255 - r, 255 - g, 255 - b);
        }
    }
    pushState();
}

void DrawingCanvas::startStroke(int x, int y, int fingerId) {
    if (fillMode) {
        floodFill(x, y);
        fillMode = false;
        return;
    }
    activeStrokes.erase(fingerId);
    activeStrokes[fingerId] = {x, y};
    drawPoint(x, y);
}

void DrawingCanvas::continueStroke(int x, int y, int fingerId) {
    auto it = activeStrokes.find(fingerId);
    if (it == activeStrokes.end()) return;
    drawThickLine(it->second.x, it->second.y, x, y);
    it->second = {x, y};
}

void DrawingCanvas::endStroke(int fingerId) {
    if (activeStrokes.erase(fingerId) > 0) {
        pushState();
    }
}

void DrawingCanvas::drawPoint(int x, int y) {
    Uint32 color = eraserMode ? SDL_MapRGBA(canvas->format, 0,0,0,0) : currentColor;
    drawCircleAA(x, y, penSize, color);
}

void DrawingCanvas::drawCircle(int cx, int cy, int radius, Uint32 color) {
    int r2 = radius * radius;
    for (int y = -radius; y <= radius; y++) {
        int dx = (int)sqrt(r2 - y*y);
        int x1 = cx - dx;
        int x2 = cx + dx;
        for (int x = x1; x <= x2; x++) {
            if (x >= 0 && x < width && cy+y >= 0 && cy+y < height) {
                setPixel(x, cy+y, color, canvas);
            }
        }
    }
}

void DrawingCanvas::blendPixel(int x, int y, Uint32 color, float alpha) {
    if (x < 0 || x >= width || y < 0 || y >= height || alpha <= 0.0f) return;
    Uint32* px = &((Uint32*)canvas->pixels)[y * canvas->w + x];
    if (alpha >= 1.0f) {
        *px = color;
        return;
    }

    Uint8 sr = (color >> 16) & 0xFF;
    Uint8 sg = (color >>  8) & 0xFF;
    Uint8 sb =  color        & 0xFF;
    Uint8 sa = (color >> 24) & 0xFF;

    Uint8 dr = (*px >> 16) & 0xFF;
    Uint8 dg = (*px >>  8) & 0xFF;
    Uint8 db =  *px        & 0xFF;
    Uint8 da = (*px >> 24) & 0xFF;

    // Alpha blend (over operation)
    float a = sa * alpha;
    float inv_a = 1.0f - a;
    Uint8 r = (Uint8)(sr * a + dr * inv_a);
    Uint8 g = (Uint8)(sg * a + dg * inv_a);
    Uint8 b = (Uint8)(sb * a + db * inv_a);
    Uint8 a_out = (Uint8)(sa * a + da * inv_a);
    *px = (a_out << 24) | (r << 16) | (g << 8) | b;
}

void DrawingCanvas::drawCircleAA(int cx, int cy, int radius, Uint32 color) {
    if (radius <= 1) { drawCircle(cx, cy, radius, color); return; }

    float r = (float)radius;
    int r2 = radius * radius;
    int iy0 = std::max(0, cy - radius - 1);
    int iy1 = std::min(height - 1, cy + radius + 1);
    Uint32* pixels = (Uint32*)canvas->pixels;
    int pitch = canvas->w;

    for (int y = iy0; y <= iy1; y++) {
        float dy = (float)(y - cy);
        float edgeDist = r - std::abs(dy);
        if (edgeDist < -1.0f) continue;

        float halfW = std::sqrt(std::max(0.0f, r*r - dy*dy));
        int x0 = (int)(cx - halfW);
        int x1 = (int)(cx + halfW);

        int sx0 = std::max(0, x0);
        int sx1 = std::min(width - 1, x1);
        for (int x = sx0; x <= sx1; x++)
            pixels[y * pitch + x] = color;

        float leftFrac = (cx - halfW) - (float)x0;
        if (leftFrac > 0.0f && x0 - 1 >= 0)
            blendPixel(x0 - 1, y, color, leftFrac);

        float rightFrac = (float)(x1 + 1) - (cx + halfW);
        if (rightFrac > 0.0f && x1 + 1 < width)
            blendPixel(x1 + 1, y, color, rightFrac);
    }
}

void DrawingCanvas::drawThickLine(int x1, int y1, int x2, int y2) {
    if (penSize <= 1) {
        drawPoint(x1, y1);
        return;
    }
    
    int dx = abs(x2 - x1), dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    
    int r = penSize;
    int r2 = r * r;
    
    while (true) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx*dx + dy*dy <= r2) {
                    int px = x1 + dx, py = y1 + dy;
                    if (px >= 0 && px < width && py >= 0 && py < height) {
                        setPixel(px, py, eraserMode ? SDL_MapRGBA(canvas->format, 0,0,0,0) : currentColor, canvas);
                    }
                }
            }
        }
        
        if (x1 == x2 && y1 == y2) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
}

void DrawingCanvas::floodFill(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    
    Uint32 target = getPixel(x, y, canvas);
    Uint32 fill = eraserMode ? SDL_MapRGBA(canvas->format, 0,0,0,0) : currentColor;
    if (target == fill) return;

    pushState();

    std::queue<SDL_Point> queue;
    queue.push({x, y});
    
    std::unique_ptr<uint8_t[]> visited(new uint8_t[width * height]());
    visited[y * width + x] = 1;
    
    Uint32* pixels = (Uint32*)canvas->pixels;
    int pitch = canvas->w;

    while (!queue.empty()) {
        SDL_Point p = queue.front(); queue.pop();
        pixels[p.y * pitch + p.x] = fill;

        int nx = p.x - 1;
        if (nx >= 0 && !visited[p.y * width + nx]) {
            if (pixels[p.y * pitch + nx] == target) {
                visited[p.y * width + nx] = 1;
                queue.push({nx, p.y});
            }
        }
        nx = p.x + 1;
        if (nx < width && !visited[p.y * width + nx]) {
            if (pixels[p.y * pitch + nx] == target) {
                visited[p.y * width + nx] = 1;
                queue.push({nx, p.y});
            }
        }
        int ny = p.y - 1;
        if (ny >= 0 && !visited[ny * width + p.x]) {
            if (pixels[ny * pitch + p.x] == target) {
                visited[ny * width + p.x] = 1;
                queue.push({p.x, ny});
            }
        }
        ny = p.y + 1;
        if (ny < height && !visited[ny * width + p.x]) {
            if (pixels[ny * pitch + p.x] == target) {
                visited[ny * width + p.x] = 1;
                queue.push({p.x, ny});
            }
        }
    }
}

void DrawingCanvas::drawShapeLine(int x1, int y1, int x2, int y2) {
    drawThickLine(x1, y1, x2, y2);
    pushState();
}

void DrawingCanvas::drawShapeRect(int x1, int y1, int x2, int y2) {
    int left   = std::min(x1, x2);
    int right  = std::max(x1, x2);
    int top    = std::min(y1, y2);
    int bottom = std::max(y1, y2);
    drawThickLine(left,  top,    right, top);
    drawThickLine(right, top,    right, bottom);
    drawThickLine(right, bottom, left,  bottom);
    drawThickLine(left,  bottom, left,  top);
    pushState();
}

void DrawingCanvas::drawShapeEllipse(int cx, int cy, int rx, int ry) {
    if (rx <= 0 || ry <= 0) return;
    Uint32 color = eraserMode ? SDL_MapRGBA(canvas->format, 0,0,0,0) : currentColor;
    int steps = 2 * (rx + ry);
    for (int i = 0; i <= steps; i++) {
        float angle = 2.0f * M_PI * i / steps;
        int px = cx + (int)(rx * cosf(angle));
        int py = cy + (int)(ry * sinf(angle));
        drawCircleAA(px, py, penSize, color);
    }
    pushState();
}

void DrawingCanvas::undo() {
    if (undoStack.size() > 1) {
        redoStack.push_front(undoStack.front());
        undoStack.pop_front();
        restoreState(undoStack.front());
    }
}

void DrawingCanvas::redo() {
    if (!redoStack.empty()) {
        undoStack.push_front(redoStack.front());
        redoStack.pop_front();
        restoreState(undoStack.front());
    }
}

bool DrawingCanvas::save(const std::string& filename) {
    // Composite background and drawing layer
    SDL_Surface* composite = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!composite) return false;

    // Fill composite with background color if no background image
    if (background) {
        SDL_BlitSurface(background, nullptr, composite, nullptr);
    } else {
        SDL_FillRect(composite, nullptr, backgroundColor);
    }
    // Blend drawing layer over composite
    SDL_BlitSurface(canvas, nullptr, composite, nullptr);

    bool result = IMG_SavePNG(composite, filename.c_str()) == 0;
    SDL_FreeSurface(composite);
    return result;
}

bool DrawingCanvas::load(const std::string& filename) {
    SDL_Surface* img = IMG_Load(filename.c_str());
    if (!img) return false;

    // Scale to canvas size
    SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!scaled) {
        SDL_FreeSurface(img);
        return false;
    }
    SDL_BlitScaled(img, nullptr, scaled, nullptr);
    SDL_FreeSurface(img);

    // Replace background (discard old)
    if (background) SDL_FreeSurface(background);
    background = scaled;

    // Clear drawing layer to transparent
    SDL_FillRect(canvas, nullptr, SDL_MapRGBA(canvas->format, 0,0,0,0));

    // Reset undo/redo history
    for (auto s : undoStack) SDL_FreeSurface(s);
    for (auto s : redoStack) SDL_FreeSurface(s);
    undoStack.clear();
    redoStack.clear();
    pushState();

    return true;
}

void DrawingCanvas::compositeToSurface(SDL_Surface* target) {
    // Fill target with background (or background color if no background)
    if (background) {
        SDL_BlitSurface(background, nullptr, target, nullptr);
    } else {
        SDL_FillRect(target, nullptr, backgroundColor);
    }
    // Overlay drawing layer
    SDL_BlitSurface(canvas, nullptr, target, nullptr);
}

Uint32 DrawingCanvas::getPixel(int x, int y, SDL_Surface* surf) {
    Uint32* pixels = (Uint32*)surf->pixels;
    return pixels[y * surf->w + x];
}

Uint32 DrawingCanvas::getPixelAt(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) return 0;
    // Return composite pixel (background + drawing)
    if (background) {
        Uint32 bg = getPixel(x, y, background);
        Uint32 fg = getPixel(x, y, canvas);
        // Simple blend: if fg has alpha, we need to composite
        // For simplicity, just return fg if opaque, else bg
        if ((fg >> 24) == 0) return bg;
        return fg;
    } else {
        return getPixel(x, y, canvas);
    }
}

void DrawingCanvas::setPixel(int x, int y, Uint32 color, SDL_Surface* surf) {
    Uint32* pixels = (Uint32*)surf->pixels;
    pixels[y * surf->w + x] = color;
}

void DrawingCanvas::pushState() {
    SDL_Surface* copy = SDL_ConvertSurface(canvas, canvas->format, 0);
    undoStack.push_front(copy);
    if (undoStack.size() > maxUndo) {
        SDL_FreeSurface(undoStack.back());
        undoStack.pop_back();
    }
    while (!redoStack.empty()) {
        SDL_FreeSurface(redoStack.back());
        redoStack.pop_back();
    }
}

void DrawingCanvas::restoreState(SDL_Surface* surf) {
    SDL_FillRect(canvas, nullptr, SDL_MapRGBA(canvas->format, 0,0,0,0));
    SDL_BlitSurface(surf, nullptr, canvas, nullptr);
}
