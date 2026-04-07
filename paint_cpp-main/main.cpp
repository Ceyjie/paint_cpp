#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <iostream>
#include <cstdlib>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include "drawingcanvas.h"
#include "touchhandler.h"

namespace fs = std::filesystem;

// ---------- forward declarations ----------
static void hoverHighlight(SDL_Renderer* r, SDL_Rect rect, SDL_Point hover, SDL_Point tap, Uint32 tapTime, Uint32 flashMs);
static const Uint32 TAP_FLASH_MS = 180;

// ---------- helper functions (placed early so they are visible) ----------
static void renderText(SDL_Renderer* r, TTF_Font* font, const std::string& text,
                       SDL_Color col, int x, int y) {
    if (text.empty()) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), col);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect dst = {x, y, s->w, s->h};
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_FreeSurface(s);
    SDL_DestroyTexture(t);
}

static void fillRoundRect(SDL_Renderer* r, SDL_Rect rect, int radius,
                           Uint8 R, Uint8 G, Uint8 B, Uint8 A) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, R, G, B, A);
    SDL_Rect inner = {rect.x + radius, rect.y, rect.w - 2*radius, rect.h};
    SDL_RenderFillRect(r, &inner);
    SDL_Rect left  = {rect.x, rect.y + radius, radius, rect.h - 2*radius};
    SDL_Rect right = {rect.x + rect.w - radius, rect.y + radius, radius, rect.h - 2*radius};
    SDL_RenderFillRect(r, &left);
    SDL_RenderFillRect(r, &right);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void drawScrollbar(SDL_Renderer* r, int x, int y, int h,
                           int total, int visible, int& scroll,
                           TTF_Font* font, SDL_Color dark) {
    int btnH = 40;
    bool canUp = (scroll > 0), canDown = (scroll < total - visible);
    SDL_SetRenderDrawColor(r, canUp?210:238, canUp?210:238, canUp?215:240, 255);
    { SDL_Rect _r={x,y,36,btnH}; SDL_RenderFillRect(r,&_r); }
    SDL_SetRenderDrawColor(r,180,180,185,255);
    { SDL_Rect _r={x,y,36,btnH}; SDL_RenderDrawRect(r,&_r); }
    renderText(r, font, "^", dark, x+11, y+10);
    int downY = y + h - btnH;
    SDL_SetRenderDrawColor(r, canDown?210:238, canDown?210:238, canDown?215:240, 255);
    { SDL_Rect _r={x,downY,36,btnH}; SDL_RenderFillRect(r,&_r); }
    SDL_SetRenderDrawColor(r,180,180,185,255);
    { SDL_Rect _r={x,downY,36,btnH}; SDL_RenderDrawRect(r,&_r); }
    renderText(r, font, "v", dark, x+11, downY+10);
    int trackY = y + btnH, trackH = h - btnH*2;
    SDL_SetRenderDrawColor(r,225,225,230,255);
    { SDL_Rect _r={x+10,trackY,16,trackH}; SDL_RenderFillRect(r,&_r); }
    if (total > visible && trackH > 0) {
        int thumbH = std::max(24, trackH * visible / total);
        int thumbY = trackY + (trackH - thumbH) * scroll / (total - visible);
        SDL_SetRenderDrawColor(r,160,160,170,255);
        { SDL_Rect _r={x+10,thumbY,16,thumbH}; SDL_RenderFillRect(r,&_r); }
    }
}

static void hoverHighlight(SDL_Renderer* r, SDL_Rect rect,
                            SDL_Point hover, SDL_Point tap,
                            Uint32 tapTime, Uint32 flashMs) {
    Uint32 now = SDL_GetTicks();
    bool isHover = (hover.x >= 0 &&
                    hover.x >= rect.x && hover.x <= rect.x+rect.w &&
                    hover.y >= rect.y && hover.y <= rect.y+rect.h);
    bool isTap   = (tap.x >= 0 &&
                    tap.x >= rect.x && tap.x <= rect.x+rect.w &&
                    tap.y >= rect.y && tap.y <= rect.y+rect.h &&
                    now - tapTime < flashMs);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (isTap) {
        float t = 1.0f - (float)(now - tapTime) / flashMs;
        Uint8 alpha = (Uint8)(t * 220);
        SDL_SetRenderDrawColor(r, 255, 255, 255, alpha);
        SDL_RenderFillRect(r, &rect);
    } else if (isHover) {
        SDL_SetRenderDrawColor(r, 255, 160, 40, 60);
        SDL_RenderFillRect(r, &rect);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// ---------- colour helpers ----------
static void rgbToHsv(Uint8 r, Uint8 g, Uint8 b, float &h, float &s, float &v) {
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float max = std::max({rf, gf, bf});
    float min = std::min({rf, gf, bf});
    v = max;
    float delta = max - min;
    if (delta < 0.0001f) {
        h = 0; s = 0; return;
    }
    s = delta / max;
    if (max == rf) h = (gf - bf) / delta;
    else if (max == gf) h = 2.0f + (bf - rf) / delta;
    else h = 4.0f + (rf - gf) / delta;
    h *= 60.0f;
    if (h < 0) h += 360.0f;
    h /= 360.0f;
}

static void hsvToRgb(float h, float s, float v, Uint8 &r, Uint8 &g, Uint8 &b) {
    float hh = h * 360.0f;
    int i = (int)floor(hh / 60.0f) % 6;
    float f = hh / 60.0f - floor(hh / 60.0f);
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    float rr, gg, bb;
    switch (i) {
        case 0: rr = v; gg = t; bb = p; break;
        case 1: rr = q; gg = v; bb = p; break;
        case 2: rr = p; gg = v; bb = t; break;
        case 3: rr = p; gg = q; bb = v; break;
        case 4: rr = t; gg = p; bb = v; break;
        default: rr = v; gg = p; bb = q; break;
    }
    r = (Uint8)(rr * 255);
    g = (Uint8)(gg * 255);
    b = (Uint8)(bb * 255);
}

// ---------- colour definitions ----------
namespace {
    SDL_PixelFormat* _fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
    inline Uint32 _rgb(Uint8 r, Uint8 g, Uint8 b) { return SDL_MapRGB(_fmt, r, g, b); }
}

const Uint32 COLOR_WHITE      = _rgb(255, 255, 255);
const Uint32 COLOR_BLACK      = _rgb(  0,   0,   0);
const Uint32 COLOR_RED        = _rgb(255,  59,  48);
const Uint32 COLOR_GREEN      = _rgb( 40, 205,  65);
const Uint32 COLOR_BLUE       = _rgb(  0, 122, 255);
const Uint32 COLOR_YELLOW     = _rgb(255, 204,   0);
const Uint32 COLOR_PURPLE     = _rgb(175,  82, 222);
const Uint32 COLOR_ORANGE     = _rgb(255, 149,   0);
const Uint32 COLOR_PINK       = _rgb(255,  45,  85);
const Uint32 COLOR_CYAN       = _rgb( 90, 200, 250);
const Uint32 COLOR_LIGHT_GRAY = _rgb(229, 229, 234);
const Uint32 COLOR_DARK_GRAY  = _rgb( 44,  44,  46);
const Uint32 COLOR_TOOLBAR_BG = _rgb(240, 240, 245);
const Uint32 COLOR_BORDER     = _rgb(180, 180, 185);
const Uint32 COLOR_HIGHLIGHT  = _rgb(180, 200, 255);

// ---------- Button structure ----------
struct Button {
    SDL_Rect rect;
    std::string type;
    int index;
};

// ---------- PiPaint class definition ----------
class PiPaint {
public:
    PiPaint();
    ~PiPaint();
    void run();

private:
    int width, height;
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* canvasTexture;
    TTF_Font* fontTiny;
    TTF_Font* fontSmall;
    TTF_Font* fontMedium;
    TTF_Font* fontLarge;

    DrawingCanvas canvas;
    TouchHandler touch;

    int toolbarHeight = 80;
    std::vector<Button> toolbarButtons;
    int penSize = 5;
    bool fillArmed = false;

    enum class ShapeMode { NONE, LINE, RECT, ELLIPSE };
    ShapeMode shapeMode = ShapeMode::NONE;
    SDL_Point shapeStart = {0, 0};
    SDL_Point shapeCurrent = {0, 0};
    bool shapeDragging = false;

    bool showOverlay = false;
    std::string overlayType;
    std::vector<std::string> overlayFiles;
    int overlayScroll = 0;
    int selectedIndex = -1;
    std::string filenameInput;
    int cursorPos = 0;
    bool cursorVisible = true;
    Uint32 cursorTimer = 0;

    bool browsingFolder = false;
    std::string currentBrowsePath;
    std::vector<std::string> subdirs;
    int browseScroll = 0;
    int selectedSubdir = -1;

    std::set<int>           activeFingers;
    std::map<int,SDL_Point> activeFingerPos;
    int shapeOwnerFinger = -1;
    SDL_Point lastTouchPos = {-1, -1};
    SDL_Point lastTapPos   = {-1, -1};
    Uint32    lastTapTime  = 0;
    bool multiTouchMode = false;

    SDL_Surface* compositeSurface = nullptr;
    bool needsRender = true;

    // Color wheel
    SDL_Texture* colorWheelTexture = nullptr;
    int wheelTexW = 0, wheelTexH = 0;
    float currentHue = 0.0f, currentSat = 1.0f, currentVal = 1.0f;
    void generateColorWheelTexture();
    void showColorWheel();
    void drawColorWheelOverlay();
    bool handleColorWheelClick(int x, int y);

    void createToolbar();
    void updateCanvasTexture();
    void drawToolbar();
    void drawOverlay();
    void drawGhostShape();
    void commitShape(int x1, int y1, int x2, int y2);
    void drawVirtualKeyboard(int x, int y, int w);
    bool handleVKTap(int tx, int ty, int panelX, int panelY, int panelW);

    bool vkShift = false;
    void handleTouchDown(int fingerId, int x, int y);
    void handleTouchMove(int fingerId, int x, int y);
    void handleTouchUp(int fingerId);
    void handleMouseButtonDown(SDL_MouseButtonEvent& ev);
    void handleMouseMotion(SDL_MouseMotionEvent& ev);
    void handleMouseButtonUp(SDL_MouseButtonEvent& ev);
    void handleKeyboard(SDL_KeyboardEvent& ev);
    void executeToolAction(const std::string& type, int index = -1);

    void newCanvas();
    void showSaveOverlay();
    void showLoadOverlay();
    void refreshFileList();
    std::string generateRandomFilename();
    void saveCurrentDrawing();
    void loadSelectedDrawing();

    void enterFolderBrowser();
    void refreshSubdirs();
    void goUp();
    void goHome();
    void goMedia();
    void selectCurrentFolder();

    void calibrate();
};

// ---------- PiPaint member function implementations ----------

PiPaint::PiPaint() : canvas(1920, 1080), touch(1920, 1080) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    IMG_Init(IMG_INIT_PNG);
    TTF_Init();

    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    width = dm.w;
    height = dm.h;
    window = SDL_CreateWindow("Pi Paint", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               width, height, SDL_WINDOW_FULLSCREEN_DESKTOP);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    canvasTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    compositeSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    fontTiny   = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13);
    fontSmall  = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20);
    fontMedium = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 28);
    fontLarge  = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 40);
    if (!fontTiny)  fontTiny  = TTF_OpenFont("DejaVuSans.ttf", 13);
    if (!fontSmall) fontSmall = TTF_OpenFont("DejaVuSans.ttf", 20);

    touch.init();
    touch.startInputThread();
    createToolbar();

    system("mkdir -p ~/pi-paint/drawings");
    currentBrowsePath = std::string(getenv("HOME")) + "/pi-paint/drawings";

    canvas.clear();
    needsRender = true;
}

PiPaint::~PiPaint() {
    touch.stopInputThread();
    TTF_CloseFont(fontTiny);
    TTF_CloseFont(fontSmall);
    TTF_CloseFont(fontMedium);
    TTF_CloseFont(fontLarge);
    SDL_DestroyTexture(canvasTexture);
    SDL_FreeSurface(compositeSurface);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (colorWheelTexture) SDL_DestroyTexture(colorWheelTexture);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

void PiPaint::createToolbar() {
    int colorSize = 40, margin = 8;
    int y = (toolbarHeight - colorSize) / 2;
    int x = 10;
    const Uint32 colors[10] = {COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW,
                               COLOR_PURPLE, COLOR_ORANGE, COLOR_PINK, COLOR_CYAN, COLOR_WHITE};
    for (int i = 0; i < 10; i++) {
        Button btn;
        btn.rect = {x, y, colorSize, colorSize};
        btn.type = "color";
        btn.index = i;
        toolbarButtons.push_back(btn);
        x += colorSize + margin;
    }

    // Color wheel button
    Button wheelBtn;
    wheelBtn.rect = {x, y, colorSize, colorSize};
    wheelBtn.type = "color_wheel";
    toolbarButtons.push_back(wheelBtn);
    x += colorSize + margin;

    int touchBtnW = 55, touchBtnH = 35;
    margin = 4;
    y = (toolbarHeight - touchBtnH) / 2;
    
    Button singleTouchBtn;
    singleTouchBtn.rect = {x, y, touchBtnW, touchBtnH};
    singleTouchBtn.type = "single_touch";
    toolbarButtons.push_back(singleTouchBtn);
    x += touchBtnW + margin;
    
    Button multiTouchBtn;
    multiTouchBtn.rect = {x, y, touchBtnW, touchBtnH};
    multiTouchBtn.type = "multi_touch";
    toolbarButtons.push_back(multiTouchBtn);
    x += touchBtnW + margin + 10;

    int btnW = 60, btnH = 35;
    margin = 6;
    y = (toolbarHeight - btnH) / 2;
    struct { std::string label; std::string type; } textBtns[] = {
        {"Eraser",   "eraser"},
        {"Fill",     "fill"},
        {"Bg",       "bg"},
        {"Line",     "shape_line"},
        {"Rect",     "shape_rect"},
        {"Ellipse",  "shape_ellipse"},
        {"Undo",     "undo"},
        {"Redo",     "redo"},
        {"New",      "new"},
        {"Clear",    "clear"},
        {"Save",     "save"},
        {"Load",     "load"}
    };
    for (auto& tb : textBtns) {
        Button btn;
        btn.rect = {x, y, btnW, btnH};
        btn.type = tb.type;
        toolbarButtons.push_back(btn);
        x += btnW + margin;
    }

    int sizeW = 35, sizeH = 35;
    margin = 5;
    y = (toolbarHeight - sizeH) / 2;

    Button minusBtn;
    minusBtn.rect = {x, y, sizeW, sizeH};
    minusBtn.type = "size_down";
    toolbarButtons.push_back(minusBtn);
    x += sizeW + margin;

    Button sizeLabel;
    sizeLabel.rect = {x, y, 45, sizeH};
    sizeLabel.type = "size_label";
    toolbarButtons.push_back(sizeLabel);
    x += 45 + margin;

    Button plusBtn;
    plusBtn.rect = {x, y, sizeW, sizeH};
    plusBtn.type = "size_up";
    toolbarButtons.push_back(plusBtn);
}

void PiPaint::updateCanvasTexture() {
    if (compositeSurface) {
        canvas.compositeToSurface(compositeSurface);
        SDL_UpdateTexture(canvasTexture, nullptr, compositeSurface->pixels, compositeSurface->pitch);
    }
    canvas.clearDirtyRect();
}

void PiPaint::drawToolbar() {
    SDL_Rect toolbarBg = {0, 0, width, toolbarHeight};
    SDL_SetRenderDrawColor(renderer, 240, 240, 245, 255);
    SDL_RenderFillRect(renderer, &toolbarBg);
    SDL_SetRenderDrawColor(renderer, 180, 180, 185, 255);
    SDL_RenderDrawLine(renderer, 0, toolbarHeight, width, toolbarHeight);

    for (auto& btn : toolbarButtons) {
        if (btn.type == "color") {
            Uint32 col = 0;
            switch (btn.index) {
                case 0: col = COLOR_BLACK; break;
                case 1: col = COLOR_RED; break;
                case 2: col = COLOR_GREEN; break;
                case 3: col = COLOR_BLUE; break;
                case 4: col = COLOR_YELLOW; break;
                case 5: col = COLOR_PURPLE; break;
                case 6: col = COLOR_ORANGE; break;
                case 7: col = COLOR_PINK; break;
                case 8: col = COLOR_CYAN; break;
                case 9: col = COLOR_WHITE; break;
            }
            SDL_SetRenderDrawColor(renderer, (col>>16)&0xFF, (col>>8)&0xFF, col&0xFF, 255);
            SDL_RenderFillRect(renderer, &btn.rect);
            SDL_SetRenderDrawColor(renderer, 44,44,46,255);
            SDL_RenderDrawRect(renderer, &btn.rect);
            if (col == canvas.getCurrentColor() && !canvas.isEraserMode()) {
                SDL_Rect highlight = {btn.rect.x-3, btn.rect.y-3, btn.rect.w+6, btn.rect.h+6};
                SDL_SetRenderDrawColor(renderer, 0, 122, 255, 255);
                SDL_RenderDrawRect(renderer, &highlight);
            }
            hoverHighlight(renderer, btn.rect, lastTouchPos, lastTapPos, lastTapTime, TAP_FLASH_MS);
        } else if (btn.type == "color_wheel") {
            SDL_SetRenderDrawColor(renderer, 255,255,255,255);
            SDL_RenderFillRect(renderer, &btn.rect);
            SDL_SetRenderDrawColor(renderer, 44,44,46,255);
            SDL_RenderDrawRect(renderer, &btn.rect);
            int cx = btn.rect.x + btn.rect.w/2;
            int cy = btn.rect.y + btn.rect.h/2;
            int r = btn.rect.w/2 - 2;
            for (int i = 0; i < 360; i++) {
                float rad = i * M_PI / 180.0f;
                int x1 = cx + r * cos(rad);
                int y1 = cy + r * sin(rad);
                SDL_SetRenderDrawColor(renderer, 
                    (int)((sin(rad)+1)/2*255), 
                    (int)((sin(rad+2.094)+1)/2*255), 
                    (int)((sin(rad+4.188)+1)/2*255), 255);
                SDL_RenderDrawPoint(renderer, x1, y1);
            }
            hoverHighlight(renderer, btn.rect, lastTouchPos, lastTapPos, lastTapTime, TAP_FLASH_MS);
        } else if (btn.type == "size_label") {
            char text[10];
            snprintf(text, sizeof(text), "%dpx", penSize);
            SDL_Surface* surf = TTF_RenderUTF8_Blended(fontTiny, text, {44,44,46,0});
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect dst = {btn.rect.x + (btn.rect.w - surf->w)/2,
                            btn.rect.y + (btn.rect.h - surf->h)/2,
                            surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
            SDL_FreeSurface(surf);
            SDL_DestroyTexture(tex);
        } else if (btn.type == "size_up") {
            SDL_SetRenderDrawColor(renderer, 200,200,200,255);
            SDL_RenderFillRect(renderer, &btn.rect);
            SDL_SetRenderDrawColor(renderer, 44,44,46,255);
            SDL_RenderDrawRect(renderer, &btn.rect);
            SDL_RenderDrawLine(renderer, btn.rect.x+btn.rect.w/2, btn.rect.y+10,
                               btn.rect.x+btn.rect.w/2, btn.rect.y+btn.rect.h-10);
            SDL_RenderDrawLine(renderer, btn.rect.x+10, btn.rect.y+btn.rect.h/2,
                               btn.rect.x+btn.rect.w-10, btn.rect.y+btn.rect.h/2);
            hoverHighlight(renderer, btn.rect, lastTouchPos, lastTapPos, lastTapTime, TAP_FLASH_MS);
        } else if (btn.type == "size_down") {
            SDL_SetRenderDrawColor(renderer, 200,200,200,255);
            SDL_RenderFillRect(renderer, &btn.rect);
            SDL_SetRenderDrawColor(renderer, 44,44,46,255);
            SDL_RenderDrawRect(renderer, &btn.rect);
            SDL_RenderDrawLine(renderer, btn.rect.x+10, btn.rect.y+btn.rect.h/2,
                               btn.rect.x+btn.rect.w-10, btn.rect.y+btn.rect.h/2);
            hoverHighlight(renderer, btn.rect, lastTouchPos, lastTapPos, lastTapTime, TAP_FLASH_MS);
        } else if (btn.type == "single_touch") {
            Uint32 bg = multiTouchMode ? COLOR_LIGHT_GRAY : _rgb(70, 180, 70);
            SDL_SetRenderDrawColor(renderer, (bg>>16)&0xFF, (bg>>8)&0xFF, bg&0xFF, 255);
            SDL_RenderFillRect(renderer, &btn.rect);
            SDL_SetRenderDrawColor(renderer, 44,44,46,255);
            SDL_RenderDrawRect(renderer, &btn.rect);
            hoverHighlight(renderer, btn.rect, lastTouchPos, lastTapPos, lastTapTime, TAP_FLASH_MS);
            SDL_Surface* surf = TTF_RenderUTF8_Blended(fontTiny, "1Touch", {255,255,255,0});
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect dst = {btn.rect.x + (btn.rect.w - surf->w)/2,
                            btn.rect.y + (btn.rect.h - surf->h)/2,
                            surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
            SDL_FreeSurface(surf);
            SDL_DestroyTexture(tex);
        } else if (btn.type == "multi_touch") {
            Uint32 bg = multiTouchMode ? _rgb(70, 140, 220) : COLOR_LIGHT_GRAY;
            SDL_SetRenderDrawColor(renderer, (bg>>16)&0xFF, (bg>>8)&0xFF, bg&0xFF, 255);
            SDL_RenderFillRect(renderer, &btn.rect);
            SDL_SetRenderDrawColor(renderer, 44,44,46,255);
            SDL_RenderDrawRect(renderer, &btn.rect);
            hoverHighlight(renderer, btn.rect, lastTouchPos, lastTapPos, lastTapTime, TAP_FLASH_MS);
            SDL_Surface* surf = TTF_RenderUTF8_Blended(fontTiny, "MTouch", {255,255,255,0});
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect dst = {btn.rect.x + (btn.rect.w - surf->w)/2,
                            btn.rect.y + (btn.rect.h - surf->h)/2,
                            surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
            SDL_FreeSurface(surf);
            SDL_DestroyTexture(tex);
        } else {
            const char* label = "";
            if (btn.type == "eraser") label = "Eraser";
            else if (btn.type == "fill") label = "Fill";
            else if (btn.type == "bg") label = "Bg";
            else if (btn.type == "undo") label = "Undo";
            else if (btn.type == "redo") label = "Redo";
            else if (btn.type == "new") label = "New";
            else if (btn.type == "clear") label = "Clear";
            else if (btn.type == "save") label = "Save";
            else if (btn.type == "load") label = "Load";
            else if (btn.type == "shape_line")    label = "Line";
            else if (btn.type == "shape_rect")    label = "Rect";
            else if (btn.type == "shape_ellipse") label = "Ellipse";

            Uint32 bg = COLOR_LIGHT_GRAY;
            if (btn.type == "eraser" && canvas.isEraserMode()) bg = _rgb(200, 220, 255);
            if (btn.type == "fill"   && fillArmed)             bg = _rgb(200, 255, 200);
            if (btn.type == "shape_line"    && shapeMode == ShapeMode::LINE)    bg = _rgb(220, 200, 255);
            if (btn.type == "shape_rect"    && shapeMode == ShapeMode::RECT)    bg = _rgb(220, 200, 255);
            if (btn.type == "shape_ellipse" && shapeMode == ShapeMode::ELLIPSE) bg = _rgb(220, 200, 255);
            SDL_SetRenderDrawColor(renderer, (bg>>16)&0xFF, (bg>>8)&0xFF, bg&0xFF, 255);
            SDL_RenderFillRect(renderer, &btn.rect);
            SDL_SetRenderDrawColor(renderer, 44, 44, 46, 255);
            SDL_RenderDrawRect(renderer, &btn.rect);
            hoverHighlight(renderer, btn.rect, lastTouchPos, lastTapPos, lastTapTime, TAP_FLASH_MS);

            SDL_Surface* surf = TTF_RenderUTF8_Blended(fontTiny, label, {44,44,46,0});
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect dst = {btn.rect.x + (btn.rect.w - surf->w)/2,
                            btn.rect.y + (btn.rect.h - surf->h)/2,
                            surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
            SDL_FreeSurface(surf);
            SDL_DestroyTexture(tex);
        }
    }
}

void PiPaint::handleTouchDown(int fingerId, int x, int y) {
    lastTouchPos = {x, y};
    lastTapPos   = {x, y};
    lastTapTime  = SDL_GetTicks();

    if (!multiTouchMode) {
        if (!activeFingers.empty()) {
            int oldId = *activeFingers.begin();
            canvas.endStroke(oldId);
            activeFingers.erase(oldId);
            activeFingerPos.erase(oldId);
        }
        activeFingers.clear();
        activeFingerPos.clear();
        fingerId = 0;
    }

    if (showOverlay) {
        if (overlayType == "color_wheel") {
            if (handleColorWheelClick(x, y)) return;
            return;
        }

        bool isSave = (overlayType == "save");

        const int PAD   = 20;
        const int GAP   = 5;
        const int MAXN  = 12;
        const int BTN_H = 48;
        const int ROW_H = 52;
        const int SB_W  = 40;

        int panelW   = std::min(900, width - 40);
        int kbW      = panelW - PAD*2;
        int keyW     = (kbW - GAP * (MAXN-1)) / MAXN;
        int keyH     = keyW;
        int kbTotalH = 5 * (keyH + GAP);

        int titleH  = 34 + 8;
        int pathH   = 18 + 10;
        int divH    = 1  + 12;
        int listRows = isSave ? 4 : 7;
        int listH   = listRows * ROW_H;
        int inputH  = isSave ? (20 + 8 + 48 + 12) : 0;
        int kbH     = isSave ? (kbTotalH + 12) : 0;
        int btnRowH = BTN_H + PAD;

        int panelH = PAD + titleH + pathH + divH + listH + divH + inputH + kbH + btnRowH;
        if (panelH > height - 20) panelH = height - 20;

        int panelX = (width  - panelW) / 2;
        int panelY = (height - panelH) / 2;
        int listW  = panelW - PAD*2 - SB_W - 4;
        int py = panelY + PAD + titleH + pathH + divH;

        if (browsingFolder) {
            py += 40;
            int fbListH = panelH - (py - panelY) - BTN_H - PAD*2 - 16;
            fbListH = std::max(fbListH, ROW_H * 3);
            int visItems = fbListH / ROW_H;

            int sbX = panelX+PAD+listW+4;
            if (x >= sbX && x < sbX+36) {
                if (y >= py && y < py+40) { if (browseScroll>0) browseScroll--; return; }
                if (y >= py+fbListH-40 && y < py+fbListH) {
                    if (browseScroll < (int)subdirs.size()-visItems) browseScroll++;
                    return;
                }
            }
            if (x >= panelX+PAD && x < panelX+PAD+listW && y >= py && y < py+fbListH) {
                int idx = (y-py)/ROW_H + browseScroll;
                if (idx>=0 && idx<(int)subdirs.size()) {
                    if (selectedSubdir==idx) {
                        currentBrowsePath += "/" + subdirs[idx];
                        refreshSubdirs(); refreshFileList(); selectedSubdir=-1;
                    } else selectedSubdir=idx;
                }
                return;
            }
            int by = py + fbListH + 16;
            struct { const char* action; int bw; } fbBtns[]=
                {{"up",105},{"home",105},{"media",105},{"select",130},{"cancel",120}};
            int bx = panelX+PAD;
            for (auto& b : fbBtns) {
                if (x>=bx && x<=bx+b.bw && y>=by && y<=by+BTN_H) {
                    std::string a=b.action;
                    if (a=="up") goUp();
                    else if (a=="home") goHome();
                    else if (a=="media") goMedia();
                    else if (a=="select") selectCurrentFolder();
                    else showOverlay=false;
                    return;
                }
                bx+=b.bw+8;
            }
            return;
        }

        int listY = py;
        int visItems = listH / ROW_H;

        int sbX = panelX+PAD+listW+4;
        if (x>=sbX && x<sbX+36) {
            if (y>=listY && y<listY+40) { if (overlayScroll>0) overlayScroll--; return; }
            if (y>=listY+listH-40 && y<listY+listH) {
                if (overlayScroll<(int)overlayFiles.size()-visItems) overlayScroll++;
                return;
            }
        }
        if (x>=panelX+PAD && x<panelX+PAD+listW && y>=listY && y<listY+listH) {
            int idx=(y-listY)/ROW_H+overlayScroll;
            if (idx>=0 && idx<(int)overlayFiles.size()) {
                if (selectedIndex==idx && overlayType=="load") loadSelectedDrawing();
                else { selectedIndex=idx; if (isSave) filenameInput=overlayFiles[idx]; }
            }
            return;
        }

        py = listY + listH + divH;

        if (isSave) {
            int inputY = py + 28;
            if (x>=panelX+PAD && x<=panelX+panelW-PAD && y>=inputY && y<=inputY+48) {
                cursorPos=(int)filenameInput.size(); return;
            }
            py = inputY + 60;
            int kbX=panelX+PAD, kbY=py;
            if (handleVKTap(x, y, kbX, kbY, kbW)) return;
            py += kbTotalH + 12;
        }

        struct { const char* action; int bw; } aBtns[]=
            {{"browse",150},{isSave?"save":"load",150},{"cancel",120}};
        int bx=panelX+PAD;
        for (auto& b : aBtns) {
            if (x>=bx && x<=bx+b.bw && y>=py && y<=py+BTN_H) {
                std::string a=b.action;
                if (a=="browse") enterFolderBrowser();
                else if (a=="save") saveCurrentDrawing();
                else if (a=="load") loadSelectedDrawing();
                else showOverlay=false;
                return;
            }
            bx+=b.bw+12;
        }
        return;
    }

    activeFingerPos[fingerId] = {x, y};
    lastTouchPos = {x, y};

    if (y <= toolbarHeight) {
        if (activeFingers.empty()) {
            for (auto& btn : toolbarButtons) {
                if (x >= btn.rect.x && x <= btn.rect.x+btn.rect.w &&
                    y >= btn.rect.y && y <= btn.rect.y+btn.rect.h) {
                    executeToolAction(btn.type, btn.index);
                    return;
                }
            }
        }
        return;
    }

    if (fillArmed) {
        if (activeFingers.empty()) { canvas.floodFill(x, y); fillArmed = false; }
    } else if (shapeMode != ShapeMode::NONE) {
        if (shapeOwnerFinger == -1) {
            shapeOwnerFinger = fingerId;
            shapeStart = shapeCurrent = {x, y};
            shapeDragging = true;
            activeFingers.insert(fingerId);
        }
    } else {
        activeFingers.insert(fingerId);
        canvas.startStroke(x, y, fingerId);
    }
}

void PiPaint::handleTouchMove(int fingerId, int x, int y) {
    if (!multiTouchMode) fingerId = 0;
    
    lastTouchPos = {x, y};
    activeFingerPos[fingerId] = {x, y};

    if (!activeFingers.count(fingerId)) return;
    if (y <= toolbarHeight) return;

    if (shapeDragging && fingerId == shapeOwnerFinger) {
        shapeCurrent = {x, y};
    } else if (!shapeDragging) {
        canvas.continueStroke(x, y, fingerId);
    }
}

void PiPaint::handleTouchUp(int fingerId) {
    if (!multiTouchMode) fingerId = 0;
    
    activeFingers.erase(fingerId);
    activeFingerPos.erase(fingerId);
    if (activeFingers.empty()) lastTouchPos = {-1, -1};

    if (shapeDragging && fingerId == shapeOwnerFinger) {
        commitShape(shapeStart.x, shapeStart.y, shapeCurrent.x, shapeCurrent.y);
        shapeDragging = false;
        shapeOwnerFinger = -1;
    } else {
        canvas.endStroke(fingerId);
    }
}

void PiPaint::handleMouseButtonDown(SDL_MouseButtonEvent& ev) {
    handleTouchDown(0, ev.x, ev.y);
}

void PiPaint::handleMouseMotion(SDL_MouseMotionEvent& ev) {
    lastTouchPos = {ev.x, ev.y};
    activeFingerPos[0] = {ev.x, ev.y};
    if (!activeFingers.count(0) || ev.y <= toolbarHeight) return;
    if (shapeDragging) { shapeCurrent = {ev.x, ev.y}; }
    else { canvas.continueStroke(ev.x, ev.y, 0); }
}

void PiPaint::handleMouseButtonUp(SDL_MouseButtonEvent& ev) {
    lastTouchPos = {-1, -1};
    handleTouchUp(0);
}

void PiPaint::handleKeyboard(SDL_KeyboardEvent& ev) {
    if (ev.type == SDL_KEYDOWN) {
        if (showOverlay && overlayType == "save" && !browsingFolder) {
            if (ev.keysym.sym == SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_RETURN) ||
                ev.keysym.sym == SDLK_KP_ENTER) {
                saveCurrentDrawing();
                return;
            }
            if (ev.keysym.sym == SDLK_ESCAPE) { showOverlay = false; return; }
            if (ev.keysym.sym == SDLK_BACKSPACE && cursorPos > 0) {
                filenameInput.erase(cursorPos-1, 1);
                cursorPos--;
                cursorVisible = true; cursorTimer = SDL_GetTicks();
                return;
            }
            if (ev.keysym.sym == SDLK_DELETE && cursorPos < (int)filenameInput.size()) {
                filenameInput.erase(cursorPos, 1);
                cursorVisible = true; cursorTimer = SDL_GetTicks();
                return;
            }
            if (ev.keysym.sym == SDLK_LEFT  && cursorPos > 0) { cursorPos--; return; }
            if (ev.keysym.sym == SDLK_RIGHT && cursorPos < (int)filenameInput.size()) { cursorPos++; return; }
            if (ev.keysym.sym == SDLK_HOME) { cursorPos = 0; return; }
            if (ev.keysym.sym == SDLK_END)  { cursorPos = (int)filenameInput.size(); return; }
            SDL_Keycode k = ev.keysym.sym;
            bool shift = (ev.keysym.mod & KMOD_SHIFT) != 0;
            char c = 0;
            if (k >= SDLK_a && k <= SDLK_z) c = shift ? ('A' + k - SDLK_a) : ('a' + k - SDLK_a);
            else if (k >= SDLK_0 && k <= SDLK_9) {
                const char* nums   = "0123456789";
                const char* shNums = ")!@#$%^&*(";
                c = shift ? shNums[k - SDLK_0] : nums[k - SDLK_0];
            }
            else if (k == SDLK_SPACE)      c = ' ';
            else if (k == SDLK_MINUS)      c = shift ? '_' : '-';
            else if (k == SDLK_PERIOD)     c = '.';
            else if (k == SDLK_UNDERSCORE) c = '_';
            if (c && filenameInput.size() < 64) {
                filenameInput.insert(cursorPos, 1, c);
                cursorPos++;
                cursorVisible = true; cursorTimer = SDL_GetTicks();
            }
            return;
        }
        if (showOverlay && overlayType == "load" && !browsingFolder) {
            if (ev.keysym.sym == SDLK_RETURN || ev.keysym.sym == SDLK_KP_ENTER) {
                loadSelectedDrawing(); return;
            }
            if (ev.keysym.sym == SDLK_ESCAPE) { showOverlay = false; return; }
            if (ev.keysym.sym == SDLK_UP && overlayScroll > 0) { overlayScroll--; return; }
            if (ev.keysym.sym == SDLK_DOWN && overlayScroll < (int)overlayFiles.size()-1) { overlayScroll++; return; }
            return;
        }
        if (showOverlay && browsingFolder) {
            if (ev.keysym.sym == SDLK_ESCAPE) { browsingFolder = false; return; }
            if (ev.keysym.sym == SDLK_UP && browseScroll > 0) { browseScroll--; return; }
            if (ev.keysym.sym == SDLK_DOWN) { browseScroll++; return; }
            if (ev.keysym.sym == SDLK_RETURN) { selectCurrentFolder(); return; }
            return;
        }
        if (showOverlay && overlayType == "color_wheel") {
            if (ev.keysym.sym == SDLK_ESCAPE) { showOverlay = false; return; }
            return;
        }

        if (ev.keysym.sym >= SDLK_1 && ev.keysym.sym <= SDLK_9) {
            int idx = ev.keysym.sym - SDLK_1;
            executeToolAction("color", idx);
        } else if (ev.keysym.sym == SDLK_0) {
            executeToolAction("color", 9);
        } else if (ev.keysym.sym == SDLK_e && (ev.keysym.mod & KMOD_CTRL)) {
            executeToolAction("eraser");
        } else if (ev.keysym.sym == SDLK_f && (ev.keysym.mod & KMOD_CTRL)) {
            executeToolAction("fill");
        } else if (ev.keysym.sym == SDLK_b && (ev.keysym.mod & KMOD_CTRL)) {
            executeToolAction("bg");
        } else if (ev.keysym.sym == SDLK_l && (ev.keysym.mod & KMOD_CTRL)) {
            executeToolAction("clear");
        } else if (ev.keysym.sym == SDLK_n && (ev.keysym.mod & KMOD_CTRL)) {
            newCanvas();
        } else if (ev.keysym.sym == SDLK_z && (ev.keysym.mod & KMOD_CTRL)) {
            canvas.undo();
        } else if (ev.keysym.sym == SDLK_y && (ev.keysym.mod & KMOD_CTRL)) {
            canvas.redo();
        } else if (ev.keysym.sym == SDLK_s && (ev.keysym.mod & KMOD_CTRL)) {
            showSaveOverlay();
        } else if (ev.keysym.sym == SDLK_o && (ev.keysym.mod & KMOD_CTRL)) {
            showLoadOverlay();
        } else if (ev.keysym.sym == SDLK_UP) {
            executeToolAction("size_up");
        } else if (ev.keysym.sym == SDLK_DOWN) {
            executeToolAction("size_down");
        } else if (ev.keysym.sym == SDLK_ESCAPE) {
            if (showOverlay) showOverlay = false;
        }
    }
}

void PiPaint::executeToolAction(const std::string& type, int index) {
    if (type == "color") {
        switch (index) {
            case 0: canvas.setColor(0,0,0); break;
            case 1: canvas.setColor(255,59,48); break;
            case 2: canvas.setColor(40,205,65); break;
            case 3: canvas.setColor(0,122,255); break;
            case 4: canvas.setColor(255,204,0); break;
            case 5: canvas.setColor(175,82,222); break;
            case 6: canvas.setColor(255,149,0); break;
            case 7: canvas.setColor(255,45,85); break;
            case 8: canvas.setColor(90,200,250); break;
            case 9: canvas.setColor(255,255,255); break;
        }
    } else if (type == "color_wheel") {
        showColorWheel();
    } else if (type == "eraser") {
        canvas.toggleEraser();
        shapeMode = ShapeMode::NONE;
    } else if (type == "fill") {
        canvas.toggleFill();
        fillArmed = canvas.isFillMode();
        shapeMode = ShapeMode::NONE;
    } else if (type == "shape_line") {
        shapeMode = (shapeMode == ShapeMode::LINE) ? ShapeMode::NONE : ShapeMode::LINE;
        fillArmed = false;
        if (canvas.isEraserMode()) canvas.toggleEraser();
    } else if (type == "shape_rect") {
        shapeMode = (shapeMode == ShapeMode::RECT) ? ShapeMode::NONE : ShapeMode::RECT;
        fillArmed = false;
        if (canvas.isEraserMode()) canvas.toggleEraser();
    } else if (type == "shape_ellipse") {
        shapeMode = (shapeMode == ShapeMode::ELLIPSE) ? ShapeMode::NONE : ShapeMode::ELLIPSE;
        fillArmed = false;
        if (canvas.isEraserMode()) canvas.toggleEraser();
    } else if (type == "bg") {
        canvas.toggleBackground();
    } else if (type == "undo") {
        canvas.undo();
        needsRender = true;
    } else if (type == "redo") {
        canvas.redo();
        needsRender = true;
    } else if (type == "clear") {
        canvas.clear();
    } else if (type == "new") {
        newCanvas();
    } else if (type == "save") {
        showSaveOverlay();
    } else if (type == "load") {
        showLoadOverlay();
    } else if (type == "size_up") {
        penSize = std::min(50, penSize + 1);
        canvas.setSize(penSize);
    } else if (type == "size_down") {
        penSize = std::max(1, penSize - 1);
        canvas.setSize(penSize);
    } else if (type == "single_touch") {
        multiTouchMode = false;
    } else if (type == "multi_touch") {
        multiTouchMode = true;
    }
}

void PiPaint::newCanvas() {
    canvas.resetToBlank();
    shapeMode = ShapeMode::NONE;
    shapeDragging = false;
    shapeOwnerFinger = -1;
}

void PiPaint::showColorWheel() {
    overlayType = "color_wheel";
    showOverlay = true;
    browsingFolder = false;
    Uint32 col = canvas.getCurrentColor();
    Uint8 r = (col >> 16) & 0xFF;
    Uint8 g = (col >> 8) & 0xFF;
    Uint8 b = col & 0xFF;
    rgbToHsv(r, g, b, currentHue, currentSat, currentVal);
    if (colorWheelTexture == nullptr) generateColorWheelTexture();
}

void PiPaint::generateColorWheelTexture() {
    if (colorWheelTexture) SDL_DestroyTexture(colorWheelTexture);
    int size = 320;
    wheelTexW = wheelTexH = size;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return;
    
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float sat = (float)x / size;
            float val = 1.0f - (float)y / size;
            Uint8 rr, gg, bb;
            hsvToRgb(currentHue, sat, val, rr, gg, bb);
            ((Uint32*)surf->pixels)[y*size + x] = SDL_MapRGBA(surf->format, rr, gg, bb, 255);
        }
    }
    colorWheelTexture = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
}

void PiPaint::drawColorWheelOverlay() {
    const int MARGIN = 30;
    const int hueH = 30;
    const int pickerSize = 280;
    const int previewH = 40;
    const int panelW = pickerSize + MARGIN * 2;
    const int panelH = 40 + hueH + 10 + pickerSize + 10 + previewH + 50;
    const int panelX = (width - panelW) / 2;
    const int panelY = (height - panelH) / 2;

    SDL_SetRenderDrawColor(renderer, 250, 250, 255, 255);
    SDL_Rect panel = {panelX, panelY, panelW, panelH};
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 180, 180, 185, 255);
    SDL_RenderDrawRect(renderer, &panel);

    int contentX = panelX + MARGIN;
    int titleY = panelY + 15;
    renderText(renderer, fontMedium, "Color", {44, 44, 46, 255}, contentX, titleY);

    int hueY = panelY + 50;
    SDL_Rect hueRect = {contentX, hueY, pickerSize, hueH};
    for (int i = 0; i < pickerSize; i++) {
        float hue = (float)i / pickerSize;
        Uint8 r, g, b;
        hsvToRgb(hue, 1.0f, 1.0f, r, g, b);
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        SDL_RenderDrawLine(renderer, contentX + i, hueY, contentX + i, hueY + hueH);
    }
    SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
    SDL_RenderDrawRect(renderer, &hueRect);

    int hueMarkerX = contentX + currentHue * pickerSize;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawLine(renderer, hueMarkerX, hueY - 2, hueMarkerX, hueY + hueH + 2);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawLine(renderer, hueMarkerX - 1, hueY - 2, hueMarkerX - 1, hueY + hueH + 2);
    SDL_RenderDrawLine(renderer, hueMarkerX + 1, hueY - 2, hueMarkerX + 1, hueY + hueH + 2);

    generateColorWheelTexture();
    int pickerY = hueY + hueH + 10;
    SDL_Rect pickerRect = {contentX, pickerY, pickerSize, pickerSize};
    SDL_RenderCopy(renderer, colorWheelTexture, nullptr, &pickerRect);
    SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
    SDL_RenderDrawRect(renderer, &pickerRect);

    int markerX = contentX + currentSat * pickerSize;
    int markerY = pickerY + (1.0f - currentVal) * pickerSize;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_Rect marker = {markerX - 5, markerY - 5, 10, 10};
    SDL_RenderDrawRect(renderer, &marker);

    int previewY = pickerY + pickerSize + 10;
    Uint8 cr, cg, cb;
    hsvToRgb(currentHue, currentSat, currentVal, cr, cg, cb);
    
    SDL_Rect currentPrev = {contentX, previewY, 60, 25};
    SDL_SetRenderDrawColor(renderer, cr, cg, cb, 255);
    SDL_RenderFillRect(renderer, &currentPrev);
    SDL_SetRenderDrawColor(renderer, 150, 150, 155, 255);
    SDL_RenderDrawRect(renderer, &currentPrev);

    char rgbText[32];
    snprintf(rgbText, sizeof(rgbText), "R:%d G:%d B:%d", cr, cg, cb);
    renderText(renderer, fontSmall, rgbText, {60, 60, 65, 255}, contentX + 70, previewY + 5);

    int btnW = 100, btnH = 40;
    int btnY = previewY + 35;
    int btnGap = 20;
    int cancelX = panelX + MARGIN;
    int okX = panelX + panelW - MARGIN - btnW;

    SDL_SetRenderDrawColor(renderer, 210, 210, 215, 255);
    SDL_Rect cancelBtn = {cancelX, btnY, btnW, btnH};
    SDL_RenderFillRect(renderer, &cancelBtn);
    SDL_SetRenderDrawColor(renderer, 160, 160, 165, 255);
    SDL_RenderDrawRect(renderer, &cancelBtn);
    hoverHighlight(renderer, cancelBtn, lastTouchPos, lastTapPos, lastTapTime, TAP_FLASH_MS);
    renderText(renderer, fontSmall, "Cancel", {60, 60, 65, 255}, cancelX + 28, btnY + 10);

    SDL_SetRenderDrawColor(renderer, 0, 180, 100, 255);
    SDL_Rect okBtn = {okX, btnY, btnW, btnH};
    SDL_RenderFillRect(renderer, &okBtn);
    SDL_SetRenderDrawColor(renderer, 0, 140, 80, 255);
    SDL_RenderDrawRect(renderer, &okBtn);
    hoverHighlight(renderer, okBtn, lastTouchPos, lastTapPos, lastTapTime, TAP_FLASH_MS);
    renderText(renderer, fontSmall, "OK", {255, 255, 255, 255}, okX + 40, btnY + 10);
}

bool PiPaint::handleColorWheelClick(int x, int y) {
    const int MARGIN = 30;
    const int hueH = 30;
    const int pickerSize = 280;
    const int previewH = 40;
    const int panelW = pickerSize + MARGIN * 2;
    const int panelH = 40 + hueH + 10 + pickerSize + 10 + previewH + 50;
    const int panelX = (width - panelW) / 2;
    const int panelY = (height - panelH) / 2;

    if (x < panelX || x > panelX + panelW || y < panelY || y > panelY + panelH) return false;

    int contentX = panelX + MARGIN;
    int hueY = panelY + 50;

    if (y >= hueY && y <= hueY + hueH && x >= contentX && x <= contentX + pickerSize) {
        currentHue = (float)(x - contentX) / pickerSize;
        currentHue = std::max(0.0f, std::min(1.0f, currentHue));
        Uint8 r, g, b;
        hsvToRgb(currentHue, currentSat, currentVal, r, g, b);
        canvas.setColor(r, g, b);
        return true;
    }

    int pickerY = hueY + hueH + 10;
    if (y >= pickerY && y <= pickerY + pickerSize && x >= contentX && x <= contentX + pickerSize) {
        currentSat = (float)(x - contentX) / pickerSize;
        currentVal = 1.0f - (float)(y - pickerY) / pickerSize;
        currentSat = std::max(0.0f, std::min(1.0f, currentSat));
        currentVal = std::max(0.0f, std::min(1.0f, currentVal));
        Uint8 r, g, b;
        hsvToRgb(currentHue, currentSat, currentVal, r, g, b);
        canvas.setColor(r, g, b);
        return true;
    }

    int previewY = pickerY + pickerSize + 10;
    int btnW = 100, btnH = 40;
    int btnY = previewY + 35;
    int cancelX = panelX + MARGIN;
    int okX = panelX + panelW - MARGIN - btnW;

    if (x >= cancelX && x <= cancelX + btnW && y >= btnY && y <= btnY + btnH) {
        showOverlay = false;
        return true;
    }

    if (x >= okX && x <= okX + btnW && y >= btnY && y <= btnY + btnH) {
        showOverlay = false;
        return true;
    }

    return false;
}

// ---------- file list helpers ----------

void PiPaint::refreshFileList() {
    overlayFiles.clear();
    overlayScroll = 0;
    selectedIndex = -1;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(currentBrowsePath, ec)) {
        if (entry.is_regular_file(ec)) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                overlayFiles.push_back(entry.path().filename().string());
        }
    }
    std::sort(overlayFiles.begin(), overlayFiles.end());
}

void PiPaint::refreshSubdirs() {
    subdirs.clear();
    browseScroll = 0;
    selectedSubdir = -1;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(currentBrowsePath, ec)) {
        if (entry.is_directory(ec))
            subdirs.push_back(entry.path().filename().string());
    }
    std::sort(subdirs.begin(), subdirs.end());
}

std::string PiPaint::generateRandomFilename() {
    return "drawing_" + std::to_string(time(nullptr)) + ".png";
}

void PiPaint::showSaveOverlay() {
    overlayType = "save";
    showOverlay = true;
    browsingFolder = false;
    filenameInput = generateRandomFilename();
    cursorPos = (int)filenameInput.size();
    refreshFileList();
    refreshSubdirs();
}

void PiPaint::showLoadOverlay() {
    overlayType = "load";
    showOverlay = true;
    browsingFolder = false;
    filenameInput = "";
    selectedIndex = -1;
    refreshFileList();
    refreshSubdirs();
}

void PiPaint::saveCurrentDrawing() {
    std::string name = filenameInput.empty() ? generateRandomFilename() : filenameInput;
    if (name.size() < 4 || name.substr(name.size()-4) != ".png")
        name += ".png";
    std::string path = currentBrowsePath + "/" + name;
    if (canvas.save(path))
        std::cout << "Saved: " << path << "\n";
    else
        std::cerr << "Save failed: " << path << "\n";
    showOverlay = false;
}

void PiPaint::loadSelectedDrawing() {
    if (selectedIndex < 0 || selectedIndex >= (int)overlayFiles.size()) return;
    std::string path = currentBrowsePath + "/" + overlayFiles[selectedIndex];
    if (canvas.load(path))
        std::cout << "Loaded: " << path << "\n";
    else
        std::cerr << "Load failed: " << path << "\n";
    showOverlay = false;
}

void PiPaint::enterFolderBrowser() {
    browsingFolder = true;
    refreshSubdirs();
}

void PiPaint::goUp() {
    fs::path p(currentBrowsePath);
    if (p.has_parent_path() && p != p.root_path()) {
        currentBrowsePath = p.parent_path().string();
        refreshSubdirs();
        refreshFileList();
    }
}

void PiPaint::goHome() {
    const char* home = getenv("HOME");
    currentBrowsePath = home ? std::string(home) + "/pi-paint/drawings" : "/home/pi/pi-paint/drawings";
    fs::create_directories(currentBrowsePath);
    refreshSubdirs();
    refreshFileList();
    browsingFolder = false;
}

void PiPaint::goMedia() {
    currentBrowsePath = "/media";
    refreshSubdirs();
    refreshFileList();
    browsingFolder = false;
}

void PiPaint::selectCurrentFolder() {
    if (selectedSubdir >= 0 && selectedSubdir < (int)subdirs.size()) {
        currentBrowsePath += "/" + subdirs[selectedSubdir];
        refreshSubdirs();
        refreshFileList();
        selectedSubdir = -1;
    } else {
        browsingFolder = false;
    }
}

void PiPaint::commitShape(int x1, int y1, int x2, int y2) {
    switch (shapeMode) {
        case ShapeMode::LINE:
            canvas.drawShapeLine(x1, y1, x2, y2);
            break;
        case ShapeMode::RECT:
            canvas.drawShapeRect(x1, y1, x2, y2);
            break;
        case ShapeMode::ELLIPSE: {
            int cx = (x1 + x2) / 2;
            int cy = (y1 + y2) / 2;
            int rx = std::abs(x2 - x1) / 2;
            int ry = std::abs(y2 - y1) / 2;
            canvas.drawShapeEllipse(cx, cy, rx, ry);
            break;
        }
        default: break;
    }
}

void PiPaint::drawGhostShape() {
    if (!shapeDragging || shapeMode == ShapeMode::NONE) return;

    int x1 = shapeStart.x,   y1 = shapeStart.y;
    int x2 = shapeCurrent.x, y2 = shapeCurrent.y;

    Uint32 col = canvas.getCurrentColor();
    Uint8 r = (col >> 16) & 0xFF;
    Uint8 g = (col >>  8) & 0xFF;
    Uint8 b =  col        & 0xFF;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, r, g, b, 180);

    int thick = std::max(1, canvas.getPenSize());

    if (shapeMode == ShapeMode::LINE) {
        for (int t = -thick/2; t <= thick/2; t++) {
            SDL_RenderDrawLine(renderer, x1+t, y1,   x2+t, y2);
            SDL_RenderDrawLine(renderer, x1,   y1+t, x2,   y2+t);
        }
    } else if (shapeMode == ShapeMode::RECT) {
        int left   = std::min(x1, x2), right  = std::max(x1, x2);
        int top    = std::min(y1, y2), bottom = std::max(y1, y2);
        for (int t = 0; t < thick; t++) {
            SDL_Rect r = {left+t, top+t, right-left-2*t, bottom-top-2*t};
            if (r.w > 0 && r.h > 0) SDL_RenderDrawRect(renderer, &r);
        }
    } else if (shapeMode == ShapeMode::ELLIPSE) {
        int cx = (x1 + x2) / 2, cy = (y1 + y2) / 2;
        int rx = std::abs(x2 - x1) / 2;
        int ry = std::abs(y2 - y1) / 2;
        if (rx > 0 && ry > 0) {
            int steps = 2 * (rx + ry);
            for (int i = 0; i < steps; i++) {
                float a0 = 2.0f * M_PI * i       / steps;
                float a1 = 2.0f * M_PI * (i + 1) / steps;
                SDL_RenderDrawLine(renderer,
                    cx + (int)(rx * cosf(a0)), cy + (int)(ry * sinf(a0)),
                    cx + (int)(rx * cosf(a1)), cy + (int)(ry * sinf(a1)));
            }
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void PiPaint::calibrate() {}

// ---------- virtual keyboard ----------

static const char* VK_ROWS[]       = { "1234567890-_", "qwertyuiop", "asdfghjkl.", "zxcvbnm" };
static const char* VK_ROWS_SHIFT[] = { "!@#$%^&*()+=", "QWERTYUIOP", "ASDFGHJKL,", "ZXCVBNM"  };

void PiPaint::drawVirtualKeyboard(int kbX, int kbY, int kbW) {
    int gap  = 5;
    int maxN = 12;
    int keyW = (kbW - gap * (maxN - 1)) / maxN;
    int keyH = keyW;
    SDL_Color dark = {44, 44, 46, 255};

    for (int row = 0; row < 4; row++) {
        const char* keys = vkShift ? VK_ROWS_SHIFT[row] : VK_ROWS[row];
        int n = (int)strlen(keys);
        int rowW = n * (keyW + gap) - gap;
        int startX = kbX + (kbW - rowW) / 2;
        int ky = kbY + row * (keyH + gap);
        for (int k = 0; k < n; k++) {
            int kx = startX + k * (keyW + gap);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            { SDL_Rect _r={kx,ky,keyW,keyH}; SDL_RenderFillRect(renderer,&_r); }
            SDL_SetRenderDrawColor(renderer, 190, 190, 195, 255);
            { SDL_Rect _r={kx,ky,keyW,keyH}; SDL_RenderDrawRect(renderer,&_r); }
            { SDL_Rect _r={kx,ky,keyW,keyH}; hoverHighlight(renderer,_r,lastTouchPos,lastTapPos,lastTapTime,TAP_FLASH_MS); }
            char label[2] = {keys[k], 0};
            int tw=0,th=0; TTF_SizeUTF8(fontSmall, label, &tw, &th);
            renderText(renderer, fontSmall, label, dark, kx+(keyW-tw)/2, ky+(keyH-th)/2);
        }
    }

    int botY = kbY + 4 * (keyH + gap);
    int shiftW = keyW*2+gap, bspW = keyW*2+gap;
    int spaceW = kbW - shiftW - bspW - gap*2;
    int botX = kbX;

    Uint32 shiftBg = vkShift ? 0xFFC8DCFF : 0xFFE6E6EA;
    SDL_SetRenderDrawColor(renderer,(shiftBg>>16)&0xFF,(shiftBg>>8)&0xFF,shiftBg&0xFF,255);
    { SDL_Rect _r={botX,botY,shiftW,keyH}; SDL_RenderFillRect(renderer,&_r); }
    SDL_SetRenderDrawColor(renderer,190,190,195,255);
    { SDL_Rect _r={botX,botY,shiftW,keyH}; SDL_RenderDrawRect(renderer,&_r); }
    { SDL_Rect _r={botX,botY,shiftW,keyH}; hoverHighlight(renderer,_r,lastTouchPos,lastTapPos,lastTapTime,TAP_FLASH_MS); }
    renderText(renderer, fontSmall, vkShift?"SHIFT":"Shift", dark, botX+8, botY+(keyH-20)/2);
    botX += shiftW + gap;

    SDL_SetRenderDrawColor(renderer,255,255,255,255);
    { SDL_Rect _r={botX,botY,spaceW,keyH}; SDL_RenderFillRect(renderer,&_r); }
    SDL_SetRenderDrawColor(renderer,190,190,195,255);
    { SDL_Rect _r={botX,botY,spaceW,keyH}; SDL_RenderDrawRect(renderer,&_r); }
    { SDL_Rect _r={botX,botY,spaceW,keyH}; hoverHighlight(renderer,_r,lastTouchPos,lastTapPos,lastTapTime,TAP_FLASH_MS); }
    renderText(renderer, fontSmall, "Space", dark, botX+(spaceW-50)/2, botY+(keyH-20)/2);
    botX += spaceW + gap;

    SDL_SetRenderDrawColor(renderer,255,210,210,255);
    { SDL_Rect _r={botX,botY,bspW,keyH}; SDL_RenderFillRect(renderer,&_r); }
    SDL_SetRenderDrawColor(renderer,190,190,195,255);
    { SDL_Rect _r={botX,botY,bspW,keyH}; SDL_RenderDrawRect(renderer,&_r); }
    { SDL_Rect _r={botX,botY,bspW,keyH}; hoverHighlight(renderer,_r,lastTouchPos,lastTapPos,lastTapTime,TAP_FLASH_MS); }
    renderText(renderer, fontSmall, "<--", dark, botX+8, botY+(keyH-20)/2);
}

bool PiPaint::handleVKTap(int tx, int ty, int kbX, int kbY, int kbW) {
    int gap  = 5;
    int maxN = 12;
    int keyW = (kbW - gap * (maxN - 1)) / maxN;
    int keyH = keyW;

    for (int row = 0; row < 4; row++) {
        const char* keys = vkShift ? VK_ROWS_SHIFT[row] : VK_ROWS[row];
        int n = (int)strlen(keys);
        int rowW = n * (keyW + gap) - gap;
        int startX = kbX + (kbW - rowW) / 2;
        int ky = kbY + row * (keyH + gap);
        if (ty >= ky && ty < ky + keyH) {
            for (int k = 0; k < n; k++) {
                int kx = startX + k * (keyW + gap);
                if (tx >= kx && tx < kx + keyW) {
                    if ((int)filenameInput.size() < 64) {
                        filenameInput.insert(cursorPos, 1, keys[k]);
                        cursorPos++;
                    }
                    return true;
                }
            }
        }
    }

    int botY = kbY + 4 * (keyH + gap);
    int shiftW = keyW*2+gap, bspW = keyW*2+gap;
    int spaceW = kbW - shiftW - bspW - gap*2;
    int botX = kbX;

    if (ty >= botY && ty < botY + keyH) {
        if (tx >= botX && tx < botX + shiftW) { vkShift = !vkShift; return true; }
        botX += shiftW + gap;
        if (tx >= botX && tx < botX + spaceW) {
            if ((int)filenameInput.size() < 64) { filenameInput.insert(cursorPos, 1, ' '); cursorPos++; }
            return true;
        }
        botX += spaceW + gap;
        if (tx >= botX && tx < botX + bspW) {
            if (cursorPos > 0) { filenameInput.erase(cursorPos-1, 1); cursorPos--; }
            return true;
        }
    }
    return false;
}

// ---------- drawOverlay ----------

void PiPaint::drawOverlay() {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    { SDL_Rect _r={0,0,width,height}; SDL_RenderFillRect(renderer,&_r); }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    if (overlayType == "color_wheel") {
        drawColorWheelOverlay();
        return;
    }

    bool isSave = (overlayType == "save");

    const int PAD   = 20;
    const int GAP   = 5;
    const int MAXN  = 12;
    const int BTN_H = 48;
    const int ROW_H = 52;
    const int SB_W  = 40;

    int panelW = std::min(900, width - 40);
    int kbW    = panelW - PAD*2;
    int keyW   = (kbW - GAP * (MAXN-1)) / MAXN;
    int keyH   = keyW;
    int kbTotalH = 5 * (keyH + GAP);

    int titleH    = 34 + 8;
    int pathH     = 18 + 10;
    int divH      = 1 + 12;
    int listRows  = isSave ? 4 : 7;
    int listH     = listRows * ROW_H;
    int inputH    = isSave ? (20 + 8 + 48 + 12) : 0;
    int kbH       = isSave ? (kbTotalH + 12) : 0;
    int btnRowH   = BTN_H + PAD;

    int panelH = PAD + titleH + pathH + divH + listH + divH + inputH + kbH + btnRowH;
    if (panelH > height - 20) panelH = height - 20;

    int panelX = (width  - panelW) / 2;
    int panelY = (height - panelH) / 2;

    fillRoundRect(renderer, {panelX, panelY, panelW, panelH}, 14, 248, 248, 252, 255);
    SDL_SetRenderDrawColor(renderer, 180, 180, 188, 255);
    { SDL_Rect _r={panelX,panelY,panelW,panelH}; SDL_RenderDrawRect(renderer,&_r); }

    SDL_Color dark  = {44,  44,  46,  255};
    SDL_Color muted = {120, 120, 130, 255};
    SDL_Color white = {255, 255, 255, 255};

    int px = panelX + PAD;
    int py = panelY + PAD;

    renderText(renderer, fontLarge, isSave ? "Save drawing" : "Load drawing", dark, px, py);
    py += titleH;

    renderText(renderer, fontTiny, currentBrowsePath, muted, px, py);
    py += pathH;

    SDL_SetRenderDrawColor(renderer, 210, 210, 215, 255);
    SDL_RenderDrawLine(renderer, panelX+PAD, py, panelX+panelW-PAD, py);
    py += divH;

    if (browsingFolder) {
        renderText(renderer, fontMedium, "Choose folder", dark, px, py);
        py += 40;

        int fbListH = panelH - (py - panelY) - BTN_H - PAD*2 - 16;
        fbListH = std::max(fbListH, ROW_H * 3);
        int listW = panelW - PAD*2 - SB_W - 4;

        SDL_Rect listBox = {panelX+PAD, py, listW, fbListH};
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &listBox);
        SDL_SetRenderDrawColor(renderer, 200, 200, 205, 255);
        SDL_RenderDrawRect(renderer, &listBox);

        int visItems = fbListH / ROW_H;
        for (int i = browseScroll; i < (int)subdirs.size() && i < browseScroll+visItems; i++) {
            int ry = py + (i-browseScroll)*ROW_H;
            SDL_Rect rowR = {panelX+PAD, ry, listW, ROW_H};
            if (i == selectedSubdir) {
                SDL_SetRenderDrawColor(renderer, 210, 230, 255, 255);
                SDL_RenderFillRect(renderer, &rowR);
            }
            hoverHighlight(renderer, rowR, lastTouchPos, lastTapPos, lastTapTime, TAP_FLASH_MS);
            renderText(renderer, fontMedium, "> " + subdirs[i], dark, panelX+PAD+10, ry+(ROW_H-28)/2);
            SDL_SetRenderDrawColor(renderer, 220, 220, 225, 255);
            SDL_RenderDrawLine(renderer, panelX+PAD, ry+ROW_H, panelX+PAD+listW, ry+ROW_H);
        }
        if (subdirs.empty())
            renderText(renderer, fontSmall, "No subfolders here", muted, panelX+PAD+10, py+14);

        drawScrollbar(renderer, panelX+PAD+listW+4, py, fbListH,
                      (int)subdirs.size(), visItems, browseScroll, fontSmall, dark);
        py += fbListH + 16;

        struct { const char* label; const char* action; } fbBtns[] =
            {{"^ Up","up"},{"Home","home"},{"Media","media"},{"Select","select"},{"Cancel","cancel"}};
        int bx = panelX + PAD;
        for (auto& b : fbBtns) {
            bool pri = (std::string(b.action) == "select");
            int bw = pri ? 130 : 105;
            SDL_SetRenderDrawColor(renderer, pri?0:232, pri?122:232, pri?255:236, 255);
            { SDL_Rect _r={bx,py,bw,BTN_H}; SDL_RenderFillRect(renderer,&_r); }
            SDL_SetRenderDrawColor(renderer, 180, 180, 185, 255);
            { SDL_Rect _r={bx,py,bw,BTN_H}; SDL_RenderDrawRect(renderer,&_r); }
            { SDL_Rect _r={bx,py,bw,BTN_H}; hoverHighlight(renderer,_r,lastTouchPos,lastTapPos,lastTapTime,TAP_FLASH_MS); }
            int tw=0,th=0; TTF_SizeUTF8(fontSmall,b.label,&tw,&th);
            renderText(renderer, fontSmall, b.label, pri?white:dark, bx+(bw-tw)/2, py+(BTN_H-th)/2);
            bx += bw + 8;
        }
        return;
    }

    int listW = panelW - PAD*2 - SB_W - 4;
    int listY = py;

    SDL_Rect listBox = {panelX+PAD, listY, listW, listH};
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(renderer, &listBox);
    SDL_SetRenderDrawColor(renderer, 200, 200, 205, 255);
    SDL_RenderDrawRect(renderer, &listBox);

    int visItems = listH / ROW_H;
    for (int i = overlayScroll; i < (int)overlayFiles.size() && i < overlayScroll+visItems; i++) {
        int ry = listY + (i-overlayScroll)*ROW_H;
        SDL_Rect rowR = {panelX+PAD, ry, listW, ROW_H};
        if (i == selectedIndex) {
            SDL_SetRenderDrawColor(renderer, 210, 230, 255, 255);
            SDL_RenderFillRect(renderer, &rowR);
        }
        hoverHighlight(renderer, rowR, lastTouchPos, lastTapPos, lastTapTime, TAP_FLASH_MS);
        renderText(renderer, fontMedium, overlayFiles[i], dark, panelX+PAD+10, ry+(ROW_H-28)/2);
        SDL_SetRenderDrawColor(renderer, 220, 220, 225, 255);
        SDL_RenderDrawLine(renderer, panelX+PAD, ry+ROW_H, panelX+PAD+listW, ry+ROW_H);
    }
    if (overlayFiles.empty())
        renderText(renderer, fontSmall, "No drawings found", muted, panelX+PAD+10, listY+14);

    drawScrollbar(renderer, panelX+PAD+listW+4, listY, listH,
                  (int)overlayFiles.size(), visItems, overlayScroll, fontSmall, dark);
    py = listY + listH;

    SDL_SetRenderDrawColor(renderer, 210, 210, 215, 255);
    SDL_RenderDrawLine(renderer, panelX+PAD, py+6, panelX+panelW-PAD, py+6);
    py += divH;

    if (isSave) {
        renderText(renderer, fontSmall, "Filename", muted, px, py);
        py += 28;

        SDL_Rect inputBox = {panelX+PAD, py, panelW-PAD*2, 48};
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &inputBox);
        SDL_SetRenderDrawColor(renderer, 0, 122, 255, 255);
        SDL_RenderDrawRect(renderer, &inputBox);
        renderText(renderer, fontMedium, filenameInput, dark, panelX+PAD+10, py+10);

        Uint32 now = SDL_GetTicks();
        if (now - cursorTimer > 500) { cursorVisible = !cursorVisible; cursorTimer = now; }
        if (cursorVisible) {
            int cx=0, cy2=0;
            TTF_SizeUTF8(fontMedium, filenameInput.substr(0,cursorPos).c_str(), &cx, &cy2);
            SDL_SetRenderDrawColor(renderer, 44, 44, 46, 255);
            SDL_RenderDrawLine(renderer, panelX+PAD+10+cx, py+6, panelX+PAD+10+cx, py+42);
        }
        py += 60;

        drawVirtualKeyboard(panelX+PAD, py, kbW);
        py += kbTotalH + 12;
    }

    struct BtnDef { std::string label, action; bool primary; };
    std::vector<BtnDef> btns = isSave
        ? std::vector<BtnDef>{{"Browse","browse",false},{"Save","save",true},{"Cancel","cancel",false}}
        : std::vector<BtnDef>{{"Browse","browse",false},{"Load","load",true},{"Cancel","cancel",false}};

    int bx = panelX + PAD;
    for (auto& b : btns) {
        bool pri = b.primary;
        int bw = pri ? 150 : (b.action=="browse" ? 150 : 120);
        SDL_SetRenderDrawColor(renderer, pri?0:232, pri?122:232, pri?255:236, 255);
        { SDL_Rect _r={bx,py,bw,BTN_H}; SDL_RenderFillRect(renderer,&_r); }
        SDL_SetRenderDrawColor(renderer, 180, 180, 185, 255);
        { SDL_Rect _r={bx,py,bw,BTN_H}; SDL_RenderDrawRect(renderer,&_r); }
        { SDL_Rect _r={bx,py,bw,BTN_H}; hoverHighlight(renderer,_r,lastTouchPos,lastTapPos,lastTapTime,TAP_FLASH_MS); }
        int tw=0,th=0; TTF_SizeUTF8(fontSmall,b.label.c_str(),&tw,&th);
        renderText(renderer, fontSmall, b.label, pri?white:dark, bx+(bw-tw)/2, py+(BTN_H-th)/2);
        bx += bw + 12;
    }
}

void PiPaint::run() {
    SDL_ShowCursor(SDL_ENABLE);
    SDL_Event e;
    const Uint32 FRAME_MS = 16;
    Uint32 lastRender = 0;

    while (true) {
        while (touch.pollTouchEvent(e)) {
            needsRender = true;
            if (e.type == SDL_FINGERDOWN) {
                int x = (int)(e.tfinger.x * width);
                int y = (int)(e.tfinger.y * height);
                canvas.setPressure(e.tfinger.pressure);
                if (e.tfinger.pressure > 0.0f && e.tfinger.pressure != 0.5f) {
                    int pressureSize = std::max(1, (int)(e.tfinger.pressure * penSize * 2.0f));
                    canvas.setSize(std::min(pressureSize, 50));
                }
                handleTouchDown(e.tfinger.fingerId, x, y);
            } else if (e.type == SDL_FINGERMOTION) {
                int x = (int)(e.tfinger.x * width);
                int y = (int)(e.tfinger.y * height);
                canvas.setPressure(e.tfinger.pressure);
                if (e.tfinger.pressure > 0.0f && e.tfinger.pressure != 0.5f) {
                    int pressureSize = std::max(1, (int)(e.tfinger.pressure * penSize * 2.0f));
                    canvas.setSize(std::min(pressureSize, 50));
                }
                handleTouchMove(e.tfinger.fingerId, x, y);
            } else if (e.type == SDL_FINGERUP) {
                canvas.setSize(penSize);
                canvas.setPressure(1.0f);
                handleTouchUp(e.tfinger.fingerId);
            }
        }

        while (SDL_PollEvent(&e)) {
            needsRender = true;
            if (e.type == SDL_QUIT) return;
            else if (e.type == SDL_MOUSEBUTTONDOWN) handleMouseButtonDown(e.button);
            else if (e.type == SDL_MOUSEMOTION)     handleMouseMotion(e.motion);
            else if (e.type == SDL_MOUSEBUTTONUP)   handleMouseButtonUp(e.button);
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) handleKeyboard(e.key);
        }

        Uint32 now = SDL_GetTicks();
        if (needsRender || (now - lastRender) >= FRAME_MS) {
            lastRender = now;

            updateCanvasTexture();
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, canvasTexture, nullptr, nullptr);
            drawToolbar();
            if (showOverlay) drawOverlay();
            drawGhostShape();

            SDL_RenderPresent(renderer);
            needsRender = false;
        }
    }
}

int main(int argc, char* argv[]) {
    PiPaint app;
    app.run();
    return 0;
}
