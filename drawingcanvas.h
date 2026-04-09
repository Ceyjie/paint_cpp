#ifndef DRAWINGCANVAS_H
#define DRAWINGCANVAS_H

#include <SDL2/SDL.h>
#include <vector>
#include <deque>
#include <string>
#include <map>

class DrawingCanvas {
public:
    DrawingCanvas(int width, int height);
    ~DrawingCanvas();

    void clear();                     // clears drawing layer only
    void resetToBlank();              // discards both layers and history
    void setColor(Uint8 r, Uint8 g, Uint8 b);
    void setSize(int size);
    void toggleEraser();
    void toggleFill();
    void toggleBackground();          // toggles background color of drawing layer
    void startStroke(int x, int y, int fingerId);
    void continueStroke(int x, int y, int fingerId);
    void endStroke(int fingerId);
    void floodFill(int x, int y);
    void drawShapeLine(int x1, int y1, int x2, int y2);
    void drawShapeRect(int x1, int y1, int x2, int y2);
    void drawShapeEllipse(int cx, int cy, int rx, int ry);
    void undo();
    void redo();
    bool save(const std::string& filename);
    bool load(const std::string& filename);

    // Composite the background and drawing layer for rendering
    void compositeToSurface(SDL_Surface* target);

    // Get the drawing layer (canvas) for editing
    SDL_Surface* getCanvas() const { return canvas; }

    int getPenSize() const { return penSize; }
    bool isEraserMode() const { return eraserMode; }
    bool isFillMode() const { return fillMode; }
    Uint32 getCurrentColor() const { return currentColor; }
    Uint32 getPixelAt(int x, int y);  // returns pixel from composite (background + canvas)

    // Dirty rect tracking for optimized texture updates
    bool hasDirtyRegion() const { return hasDirty; }
    SDL_Rect getDirtyRect() const { return dirtyRect; }
    void clearDirty() { hasDirty = false; dirtyRect = {0, 0, 0, 0}; }
    void markDirty(int x, int y);

private:
    int width, height;
    SDL_Surface* canvas;        // drawing layer (transparent background)
    SDL_Surface* background;    // loaded image (protected)
    Uint32 currentColor;
    Uint32 backgroundColor;     // used for eraser and clear (applies to canvas)
    int penSize;
    int minSize, maxSize;
    bool eraserMode, fillMode;
    std::map<int, SDL_Point> activeStrokes;
    std::deque<SDL_Surface*> undoStack;
    std::deque<SDL_Surface*> redoStack;
    int maxUndo = 5;

    SDL_Rect dirtyRect;
    bool hasDirty;

    void drawPoint(int x, int y);
    void drawCircle(int cx, int cy, int radius, Uint32 color);
    void drawCircleAA(int cx, int cy, int radius, Uint32 color);
    void blendPixel(int x, int y, Uint32 color, float alpha);
    void drawThickLine(int x1, int y1, int x2, int y2);
    void pushState();
    void restoreState(SDL_Surface* surf);
    Uint32 getPixel(int x, int y, SDL_Surface* surf);
    void setPixel(int x, int y, Uint32 color, SDL_Surface* surf);
};

#endif
