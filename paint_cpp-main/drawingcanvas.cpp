#include "drawingcanvas.h"
#include <SDL2/SDL_image.h>
#include <cstring>
#include <queue>
#include <algorithm>
#include <cmath>

DrawingCanvas::DrawingCanvas(int w, int h) : width(w), height(h), dirtyRect{-1, 0, 0, 0} {
    canvas = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    background = nullptr;
    currentColor = SDL_MapRGB(canvas->format, 0, 0, 0);
    backgroundColor = SDL_MapRGB(canvas->format, 255, 255, 255);
    penSize = 5;
    minSize = 1;
    maxSize = 50;
    currentPressure = 1.0f;
    eraserMode = false;
    fillMode = false;

    SDL_FillRect(canvas, nullptr, SDL_MapRGBA(canvas->format, 0,0,0,0));
    pushState();

    for (int i = 0; i < 10; i++) {
        strokeBuffers[i].points.reserve(500);
    }
}

DrawingCanvas::~DrawingCanvas() {
    SDL_FreeSurface(canvas);
    if (background) SDL_FreeSurface(background);
    for (auto s : undoStack) SDL_FreeSurface(s);
    for (auto s : redoStack) SDL_FreeSurface(s);
}

void DrawingCanvas::clear() {
    dirtyRect = {-1, 0, 0, 0};
    SDL_FillRect(canvas, nullptr, SDL_MapRGBA(canvas->format, 0,0,0,0));
    pushState();
}

void DrawingCanvas::resetToBlank() {
    dirtyRect = {-1, 0, 0, 0};
    SDL_FillRect(canvas, nullptr, SDL_MapRGBA(canvas->format, 0,0,0,0));
    if (background) {
        SDL_FreeSurface(background);
        background = nullptr;
    }
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

void DrawingCanvas::setPressure(float pressure) {
    currentPressure = std::max(0.1f, std::min(1.0f, pressure));
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
    dirtyRect = {-1, 0, 0, 0};
    Uint32 oldBg = backgroundColor;
    backgroundColor = (oldBg == SDL_MapRGB(canvas->format, 255,255,255)) ?
                       SDL_MapRGB(canvas->format, 0,0,0) :
                       SDL_MapRGB(canvas->format, 255,255,255);
    Uint32* pixels = (Uint32*)canvas->pixels;
    int totalPixels = width * height;
    for (int i = 0; i < totalPixels; i++) {
        Uint32 pix = pixels[i];
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

void DrawingCanvas::expandDirty(int x, int y, int margin) {
    dirtyRect = {-1, 0, 0, 0};
}

void DrawingCanvas::startStroke(int x, int y, int fingerId) {
    if (fillMode) {
        floodFill(x, y);
        fillMode = false;
        return;
    }

    StrokeBuffer& buf = strokeBuffers[fingerId];
    buf.points.clear();
    buf.points.push_back({{x, y}, currentPressure});
    buf.lastDrawn = {x, y};

    activeStrokes[fingerId] = {x, y};
    pushState();
    drawPoint(x, y);
}

void DrawingCanvas::continueStroke(int x, int y, int fingerId) {
    StrokeBuffer& buf = strokeBuffers[fingerId];
    if (buf.points.empty()) {
        startStroke(x, y, fingerId);
        return;
    }

    SDL_Point prev = buf.points.back().pos;
    drawThickLine(prev.x, prev.y, x, y);
    
    buf.points.clear();
    buf.points.push_back({{x, y}, currentPressure});
    buf.lastDrawn = {x, y};

    activeStrokes[fingerId] = {x, y};
}

void DrawingCanvas::endStroke(int fingerId) {
    strokeBuffers[fingerId].points.clear();

    if (activeStrokes.erase(fingerId) > 0) {
        dirtyRect = {-1, 0, 0, 0};
    }
}

void DrawingCanvas::drawPoint(int x, int y) {
    int effectiveSize = penSize;
    if (currentPressure > 0.1f && currentPressure < 1.0f) {
        effectiveSize = (int)(penSize * currentPressure * 1.5f);
        effectiveSize = std::max(1, effectiveSize);
    }
    Uint32 color = eraserMode ? SDL_MapRGBA(canvas->format, 0,0,0,0) : currentColor;
    drawCircleAA(x, y, effectiveSize, color);
    expandDirty(x, y, effectiveSize + 2);
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
    int iy0 = std::max(0, cy - radius - 1);
    int iy1 = std::min(height - 1, cy + radius + 1);

    for (int y = iy0; y <= iy1; y++) {
        float dy = (float)(y - cy);
        float edgeDist = r - std::abs(dy);
        if (edgeDist < -1.0f) continue;

        float halfW = std::sqrt(std::max(0.0f, r*r - dy*dy));
        int x0 = (int)(cx - halfW);
        int x1 = (int)(cx + halfW);

        for (int x = std::max(0, x0); x <= std::min(width-1, x1); x++)
            setPixel(x, y, color, canvas);

        float leftFrac = (cx - halfW) - (float)x0;
        if (leftFrac > 0.0f)
            blendPixel(x0 - 1, y, color, leftFrac);

        float rightFrac = (float)(x1 + 1) - (cx + halfW);
        if (rightFrac > 0.0f)
            blendPixel(x1 + 1, y, color, rightFrac);
    }
}

void DrawingCanvas::drawThickLine(int x1, int y1, int x2, int y2) {
    int dx = abs(x2 - x1), dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        drawPoint(x1, y1);
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
    dirtyRect = {-1, 0, 0, 0};

    static std::vector<uint8_t> visited;
    int totalSize = width * height;
    if ((int)visited.size() < totalSize) {
        visited.resize(totalSize);
    }
    std::fill(visited.begin(), visited.begin() + totalSize, 0);

    int minX = x, maxX = x, minY = y, maxY = y;

    std::queue<int> q;
    int startIdx = y * width + x;
    q.push(startIdx);
    visited[startIdx] = 1;

    while (!q.empty()) {
        int idx = q.front();
        q.pop();
        int px = idx % width;
        int py = idx / width;
        setPixel(px, py, fill, canvas);
        minX = std::min(minX, px);
        maxX = std::max(maxX, px);
        minY = std::min(minY, py);
        maxY = std::max(maxY, py);

        if (px > 0 && !visited[idx - 1] && getPixel(px - 1, py, canvas) == target) {
            visited[idx - 1] = 1;
            q.push(idx - 1);
        }
        if (px < width - 1 && !visited[idx + 1] && getPixel(px + 1, py, canvas) == target) {
            visited[idx + 1] = 1;
            q.push(idx + 1);
        }
        if (py > 0 && !visited[idx - width] && getPixel(px, py - 1, canvas) == target) {
            visited[idx - width] = 1;
            q.push(idx - width);
        }
        if (py < height - 1 && !visited[idx + width] && getPixel(px, py + 1, canvas) == target) {
            visited[idx + width] = 1;
            q.push(idx + width);
        }
    }

    expandDirty((minX + maxX) / 2, (minY + maxY) / 2, std::max(maxX - minX, maxY - minY) + 5);
}

void DrawingCanvas::drawShapeLine(int x1, int y1, int x2, int y2) {
    pushState();
    drawThickLine(x1, y1, x2, y2);
}

void DrawingCanvas::drawShapeRect(int x1, int y1, int x2, int y2) {
    pushState();
    int left   = std::min(x1, x2);
    int right  = std::max(x1, x2);
    int top    = std::min(y1, y2);
    int bottom = std::max(y1, y2);
    drawThickLine(left,  top,    right, top);
    drawThickLine(right, top,    right, bottom);
    drawThickLine(right, bottom, left,  bottom);
    drawThickLine(left,  bottom, left,  top);
}

void DrawingCanvas::drawShapeEllipse(int cx, int cy, int rx, int ry) {
    if (rx <= 0 || ry <= 0) return;
    pushState();
    Uint32 color = eraserMode ? SDL_MapRGBA(canvas->format, 0,0,0,0) : currentColor;
    int steps = 2 * (rx + ry);
    for (int i = 0; i <= steps; i++) {
        float angle = 2.0f * M_PI * i / steps;
        int px = cx + (int)(rx * cosf(angle));
        int py = cy + (int)(ry * sinf(angle));
        drawCircleAA(px, py, penSize, color);
    }
}

void DrawingCanvas::undo() {
    if (undoStack.size() > 1) {
        redoStack.push_front(undoStack.front());
        undoStack.pop_front();
        restoreState(undoStack.front());
        dirtyRect = {-1, 0, 0, 0};
    }
}

void DrawingCanvas::redo() {
    if (!redoStack.empty()) {
        undoStack.push_front(redoStack.front());
        redoStack.pop_front();
        restoreState(undoStack.front());
        dirtyRect = {-1, 0, 0, 0};
    }
}

bool DrawingCanvas::save(const std::string& filename) {
    SDL_Surface* composite = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!composite) return false;

    if (background) {
        SDL_BlitSurface(background, nullptr, composite, nullptr);
    } else {
        SDL_FillRect(composite, nullptr, backgroundColor);
    }
    SDL_BlitSurface(canvas, nullptr, composite, nullptr);

    bool result = IMG_SavePNG(composite, filename.c_str()) == 0;
    SDL_FreeSurface(composite);
    return result;
}

bool DrawingCanvas::load(const std::string& filename) {
    SDL_Surface* img = IMG_Load(filename.c_str());
    if (!img) return false;

    SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!scaled) {
        SDL_FreeSurface(img);
        return false;
    }
    SDL_BlitScaled(img, nullptr, scaled, nullptr);
    SDL_FreeSurface(img);

    if (background) SDL_FreeSurface(background);
    background = scaled;

    SDL_FillRect(canvas, nullptr, SDL_MapRGBA(canvas->format, 0,0,0,0));

    for (auto s : undoStack) SDL_FreeSurface(s);
    for (auto s : redoStack) SDL_FreeSurface(s);
    undoStack.clear();
    redoStack.clear();
    pushState();

    return true;
}

void DrawingCanvas::compositeToSurface(SDL_Surface* target) {
    if (dirtyRect.x < 0) {
        if (background) {
            SDL_BlitSurface(background, nullptr, target, nullptr);
        } else {
            SDL_FillRect(target, nullptr, backgroundColor);
        }
        SDL_BlitSurface(canvas, nullptr, target, nullptr);
    } else {
        if (background) {
            SDL_BlitSurface(background, nullptr, target, nullptr);
        }
        SDL_BlitSurface(canvas, nullptr, target, &dirtyRect);
    }
}

void DrawingCanvas::compositeDirtyRect(SDL_Surface* target, const SDL_Rect& clip) {
    if (background) {
        SDL_BlitSurface(background, nullptr, target, nullptr);
    }
    if (dirtyRect.x >= 0) {
        SDL_Rect updateRect = dirtyRect;
        if (SDL_IntersectRect(&dirtyRect, &clip, &updateRect)) {
            SDL_BlitSurface(canvas, &updateRect, target, &updateRect);
        }
    } else {
        SDL_BlitSurface(canvas, nullptr, target, nullptr);
    }
}

Uint32 DrawingCanvas::getPixel(int x, int y, SDL_Surface* surf) {
    Uint32* pixels = (Uint32*)surf->pixels;
    return pixels[y * surf->w + x];
}

Uint32 DrawingCanvas::getPixelAt(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) return 0;
    if (background) {
        Uint32 bg = getPixel(x, y, background);
        Uint32 fg = getPixel(x, y, canvas);
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
    SDL_BlitSurface(surf, nullptr, canvas, nullptr);
}
