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

    void clear();
    void resetToBlank();
    void setColor(Uint8 r, Uint8 g, Uint8 b);
    void setSize(int size);
    void setPressure(float pressure);
    void toggleEraser();
    void toggleFill();
    void toggleBackground();
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

    void compositeToSurface(SDL_Surface* target);
    void compositeDirtyRect(SDL_Surface* target, const SDL_Rect& dirty);
    SDL_Surface* getCanvas() const { return canvas; }

    bool needsFullUpdate() const { return dirtyRect.x < 0; }
    SDL_Rect getDirtyRect() const { return dirtyRect; }
    void clearDirtyRect() { dirtyRect = {-1, 0, 0, 0}; }
    void invalidateFull() { dirtyRect = {-1, 0, 0, 0}; }

    int getPenSize() const { return penSize; }
    bool isEraserMode() const { return eraserMode; }
    bool isFillMode() const { return fillMode; }
    Uint32 getCurrentColor() const { return currentColor; }
    Uint32 getPixelAt(int x, int y);

private:
    struct StrokePoint {
        SDL_Point pos;
        float pressure;
    };

    struct StrokeBuffer {
        std::vector<StrokePoint> points;
        SDL_Point lastDrawn;
    };

    int width, height;
    SDL_Surface* canvas;
    SDL_Surface* background;
    Uint32 currentColor;
    Uint32 backgroundColor;
    int penSize;
    int minSize, maxSize;
    float currentPressure;
    bool eraserMode, fillMode;
    std::map<int, SDL_Point> activeStrokes;
    std::map<int, StrokeBuffer> strokeBuffers;
    std::deque<SDL_Surface*> undoStack;
    std::deque<SDL_Surface*> redoStack;
    int maxUndo = 100;

    SDL_Rect dirtyRect;
    static constexpr int DIRTY_MARGIN = 50;

    void expandDirty(int x, int y, int margin = DIRTY_MARGIN);
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
