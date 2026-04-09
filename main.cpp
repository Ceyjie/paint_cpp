#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <iostream>
#include <cstdlib>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <dirent.h>
#include <ctime>
#include "drawingcanvas.h"
#include "touchhandler.h"

namespace fs = std::filesystem;

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

static void drawRoundedBorder(SDL_Renderer* r, SDL_Rect rect, int radius, Uint8 R, Uint8 G, Uint8 B) {
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    SDL_RenderDrawLine(r, rect.x + radius, rect.y, rect.x + rect.w - radius, rect.y);
    SDL_RenderDrawLine(r, rect.x + rect.w, rect.y + radius, rect.x + rect.w, rect.y + rect.h - radius);
    SDL_RenderDrawLine(r, rect.x + radius, rect.y + rect.h, rect.x + rect.w - radius, rect.y + rect.h);
    SDL_RenderDrawLine(r, rect.x, rect.y + radius, rect.x, rect.y + rect.h - radius);
    for (int i = 0; i <= radius; i++) {
        float angle1 = M_PI, angle2 = M_PI * 1.5;
        int x1 = rect.x + radius + (int)(radius * cos(angle1));
        int y1 = rect.y + radius + (int)(radius * sin(angle1));
        int x2 = rect.x + rect.w - radius + (int)(radius * cos(angle2));
        int y2 = rect.y + radius + (int)(radius * sin(angle2));
        (void)x1; (void)y1; (void)x2; (void)y2;
    }
    for (int dy = -radius; dy <= 0; dy++) {
        for (int dx = -radius; dx <= 0; dx++) {
            if (dx*dx + dy*dy <= radius*radius) {
                SDL_RenderDrawPoint(r, rect.x + radius + dx, rect.y + radius + dy);
                break;
            }
        }
    }
    for (int dy = -radius; dy <= 0; dy++) {
        for (int dx = 0; dx <= radius; dx++) {
            if (dx*dx + dy*dy <= radius*radius) {
                SDL_RenderDrawPoint(r, rect.x + rect.w - radius + dx, rect.y + radius + dy);
                break;
            }
        }
    }
    for (int dy = 0; dy <= radius; dy++) {
        for (int dx = -radius; dx <= 0; dx++) {
            if (dx*dx + dy*dy <= radius*radius) {
                SDL_RenderDrawPoint(r, rect.x + radius + dx, rect.y + rect.h - radius + dy);
                break;
            }
        }
    }
    for (int dy = 0; dy <= radius; dy++) {
        for (int dx = 0; dx <= radius; dx++) {
            if (dx*dx + dy*dy <= radius*radius) {
                SDL_RenderDrawPoint(r, rect.x + rect.w - radius + dx, rect.y + rect.h - radius + dy);
                break;
            }
        }
    }
}

static void drawModernButton(SDL_Renderer* r, SDL_Rect rect, Uint32 bgColor, Uint32 borderColor,
                            const char* label, TTF_Font* font, SDL_Color textColor, bool selected) {
    if (selected) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 100, 200, 40);
        SDL_Rect shadowRect = {rect.x + 2, rect.y + 2, rect.w, rect.h};
        fillRoundRect(r, shadowRect, 8, 0, 100, 200, 40);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
    
    SDL_SetRenderDrawColor(r, (bgColor>>16)&0xFF, (bgColor>>8)&0xFF, bgColor&0xFF, 255);
    fillRoundRect(r, rect, 8, (bgColor>>16)&0xFF, (bgColor>>8)&0xFF, bgColor&0xFF, 255);
    
    drawRoundedBorder(r, rect, 8, (borderColor>>16)&0xFF, (borderColor>>8)&0xFF, borderColor&0xFF);
    
    if (label) {
        int tw, th;
        TTF_SizeText(font, label, &tw, &th);
        int tx = rect.x + (rect.w - tw) / 2;
        int ty = rect.y + (rect.h - th) / 2;
        renderText(r, font, label, textColor, tx, ty);
    }
}

static void drawColorSwatch(SDL_Renderer* r, SDL_Rect rect, Uint32 color, bool selected) {
    SDL_SetRenderDrawColor(r, (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF, 255);
    fillRoundRect(r, rect, 6, (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF, 255);
    
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    drawRoundedBorder(r, rect, 6, 255, 255, 255);
    
    if (selected) {
        SDL_SetRenderDrawColor(r, 0, 122, 255, 255);
        SDL_Rect selRect = {rect.x - 2, rect.y - 2, rect.w + 4, rect.h + 4};
        drawRoundedBorder(r, selRect, 8, 0, 122, 255);
    }
}

static void drawCircleButton(SDL_Renderer* r, SDL_Rect rect, Uint32 bgColor, Uint32 borderColor,
                            const char* label, TTF_Font* font, SDL_Color textColor, bool selected) {
    int cx = rect.x + rect.w / 2;
    int cy = rect.y + rect.h / 2;
    int radius = rect.w / 2;
    
    if (selected) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 100, 200, 40);
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx*dx + dy*dy <= radius*radius) {
                    SDL_RenderDrawPoint(r, cx + dx + 1, cy + dy + 1);
                }
            }
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
    
    SDL_SetRenderDrawColor(r, (bgColor>>16)&0xFF, (bgColor>>8)&0xFF, bgColor&0xFF, 255);
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx*dx + dy*dy <= radius*radius) {
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
            }
        }
    }
    
    SDL_SetRenderDrawColor(r, (borderColor>>16)&0xFF, (borderColor>>8)&0xFF, borderColor&0xFF, 255);
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx*dx + dy*dy <= radius*radius && dx*dx + dy*dy >= (radius-1)*(radius-1)) {
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
            }
        }
    }
    
    if (label) {
        int tw, th;
        TTF_SizeText(font, label, &tw, &th);
        int tx = rect.x + (rect.w - tw) / 2;
        int ty = rect.y + (rect.h - th) / 2;
        renderText(r, font, label, textColor, tx, ty);
    }
}

static SDL_Color uintToColor(Uint32 c) {
    return {(Uint8)((c>>16)&0xFF), (Uint8)((c>>8)&0xFF), (Uint8)(c&0xFF), 255};
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

// ---------- Modern UI colour definitions ----------
namespace {
    SDL_PixelFormat* _fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
    inline Uint32 _rgb(Uint8 r, Uint8 g, Uint8 b) { return SDL_MapRGB(_fmt, r, g, b); }
}

const Uint32 COLOR_WHITE      = _rgb(255, 255, 255);
const Uint32 COLOR_BLACK      = _rgb(  0,   0,   0);
const Uint32 COLOR_LIGHT_GRAY = _rgb(240, 240, 245);
const Uint32 COLOR_DARK_GRAY  = _rgb( 50,  50,  55);
const Uint32 COLOR_BORDER     = _rgb(210, 210, 215);
const Uint32 COLOR_BORDER_DARK= _rgb(180, 180, 185);
const Uint32 COLOR_TOOLBAR_BG = _rgb(248, 248, 252);
const Uint32 COLOR_BTN_BG     = _rgb(255, 255, 255);
const Uint32 COLOR_BTN_HOVER  = _rgb(245, 245, 250);
const Uint32 COLOR_BTN_ACTIVE = _rgb(  0, 122, 255);
const Uint32 COLOR_TEXT       = _rgb( 50,  50,  55);
const Uint32 COLOR_TEXT_WHITE = _rgb(255, 255, 255);
const Uint32 COLOR_SHADOW     = _rgb(180, 180, 190);

const Uint32 COLOR_RED        = _rgb(255,  59,  48);
const Uint32 COLOR_GREEN      = _rgb( 40, 205,  65);
const Uint32 COLOR_BLUE       = _rgb(  0, 122, 255);
const Uint32 COLOR_YELLOW     = _rgb(255, 204,   0);
const Uint32 COLOR_PURPLE     = _rgb(175,  82, 222);
const Uint32 COLOR_ORANGE     = _rgb(255, 149,   0);
const Uint32 COLOR_PINK       = _rgb(255,  45,  85);
const Uint32 COLOR_CYAN       = _rgb( 90, 200, 250);

struct PalettePreset {
    std::string name;
    std::vector<Uint32> colors;
};

static const std::vector<PalettePreset> PALETTE_PRESETS = {
    {"Material", {_rgb(0,0,0), _rgb(244,67,54), _rgb(76,175,80), _rgb(33,150,243),
                  _rgb(255,235,59), _rgb(156,39,176), _rgb(255,152,0), _rgb(233,30,99),
                  _rgb(0,188,212), _rgb(255,255,255)}},
    {"Pastel", {_rgb(255,182,193), _rgb(255,218,185), _rgb(255,255,210), _rgb(189,255,201),
                _rgb(186,225,255), _rgb(230,187,228), _rgb(254,200,216), _rgb(212,165,165),
                _rgb(240,230,140), _rgb(250,240,230)}},
    {"Neon", {_rgb(255,7,58), _rgb(255,0,255), _rgb(0,255,255), _rgb(57,255,20),
              _rgb(255,110,199), _rgb(191,0,255), _rgb(0,255,127), _rgb(255,69,0),
              _rgb(123,104,238), _rgb(255,255,0)}},
    {"Earth", {_rgb(139,69,19), _rgb(160,82,45), _rgb(210,180,140), _rgb(189,183,107),
               _rgb(210,105,30), _rgb(244,164,96), _rgb(139,115,85), _rgb(188,143,143),
               _rgb(196,196,196), _rgb(192,128,64)}}
};

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
    SDL_Surface* compositeSurface = nullptr;
    TTF_Font* fontTiny;
    TTF_Font* fontSmall;
    TTF_Font* fontMedium;
    TTF_Font* fontLarge;

    DrawingCanvas* canvas;
    TouchHandler* touch;

    int toolbarHeight = 68;
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
    bool needsRender = true;
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
    FILE*     tempFile    = nullptr;

    // Color wheel / Palette
    SDL_Texture* svSquareTexture = nullptr;
    SDL_Texture* hueSliderTexture = nullptr;
    int wheelTexW = 0, wheelTexH = 0;
    float currentHue = 0.0f, currentSat = 1.0f, currentVal = 1.0f;
    float lastBuiltHue = -1.0f;
    int selectedPaletteIndex = 0;
    int selectedSwatchIndex = -1;
    std::vector<Uint32> currentPalette;
    void generateSvSquareTexture();
    void showPalette();
    void drawPaletteOverlay();
    void savePalette(const std::string& filename);
    bool loadPalette(const std::string& filename);
    void resetCustomPalette();

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

    std::unordered_map<std::string, SDL_Texture*> textCache;
    SDL_Texture* renderTextCached(TTF_Font* font, const std::string& text, SDL_Color col, int x, int y);
};

// ---------- PiPaint member function implementations ----------

PiPaint::PiPaint() {
    const char* fbDrivers[] = {"fbdev", "kmsdrm", nullptr};
    const char* desktopDrivers[] = {"x11", "wayland", nullptr};
    
    bool initialized = false;
    
    for (int i = 0; fbDrivers[i]; i++) {
        setenv("SDL_VIDEODRIVER", fbDrivers[i], 1);
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0) {
            SDL_DisplayMode dm;
            if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
                std::cout << "Using video driver: " << fbDrivers[i] << std::endl;
                initialized = true;
                break;
            }
            SDL_Quit();
        }
    }
    
    if (!initialized) {
        for (int i = 0; desktopDrivers[i]; i++) {
            setenv("SDL_VIDEODRIVER", desktopDrivers[i], 1);
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0) {
                SDL_DisplayMode dm;
                if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
                    std::cout << "Using video driver: " << desktopDrivers[i] << std::endl;
                    initialized = true;
                    break;
                }
                SDL_Quit();
            }
        }
    }
    
    if (!initialized) {
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        std::cout << "Using default video driver" << std::endl;
    }
    
    IMG_Init(IMG_INIT_PNG);
    TTF_Init();

    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) != 0 || dm.w <= 0 || dm.h <= 0) {
        width = 1280;
        height = 720;
    } else {
        width = dm.w;
        height = dm.h;
    }
    
    currentPalette = PALETTE_PRESETS[0].colors;
    
    canvas = new DrawingCanvas(width, height);
    touch = new TouchHandler(width, height);
    
    window = SDL_CreateWindow("Pi Paint", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               width, height, SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        exit(1);
    }
    
    int rendererFlags = SDL_RENDERER_SOFTWARE;
    if (getenv("SDL_RENDERER_VSYNC")) rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
    renderer = SDL_CreateRenderer(window, -1, rendererFlags);
    if (!renderer) {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << std::endl;
        exit(1);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RendererInfo info;
    SDL_GetRendererInfo(renderer, &info);
    std::cout << "Renderer: " << info.name << std::endl;
    std::cout << "Display: " << width << "x" << height << std::endl;
    
    canvasTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    compositeSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    fontTiny   = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13);
    fontSmall  = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20);
    fontMedium = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 28);
    fontLarge  = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 40);
    if (!fontTiny)  fontTiny  = TTF_OpenFont("DejaVuSans.ttf", 13);
    if (!fontSmall) fontSmall = TTF_OpenFont("DejaVuSans.ttf", 20);

    touch->init();
    createToolbar();

    system("mkdir -p ~/pi-paint/drawings");
    currentBrowsePath = std::string(getenv("HOME")) + "/pi-paint/drawings";

    tempFile = nullptr;
    FILE* testFile = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (testFile) {
        tempFile = testFile;
    } else {
        testFile = fopen("/sys/class/thermal/thermal_zone1/temp", "r");
        if (testFile) tempFile = testFile;
    }

    canvas->clear();
}

PiPaint::~PiPaint() {
    TTF_CloseFont(fontTiny);
    TTF_CloseFont(fontSmall);
    TTF_CloseFont(fontMedium);
    TTF_CloseFont(fontLarge);
    delete canvas;
    delete touch;
    SDL_DestroyTexture(canvasTexture);
    SDL_FreeSurface(compositeSurface);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (svSquareTexture) SDL_DestroyTexture(svSquareTexture);
    if (hueSliderTexture) SDL_DestroyTexture(hueSliderTexture);
    for (auto& kv : textCache) {
        SDL_DestroyTexture(kv.second);
    }
    if (tempFile) fclose(tempFile);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

void PiPaint::createToolbar() {
    int colorSize = 36, margin = 4;
    int y = (toolbarHeight - colorSize) / 2;
    int x = 8;
    
    for (int i = 0; i < 10; i++) {
        Button btn;
        btn.rect = {x, y, colorSize, colorSize};
        btn.type = "color";
        btn.index = i;
        toolbarButtons.push_back(btn);
        x += colorSize + margin;
    }

    x += margin;
    
    Button paletteBtn;
    paletteBtn.rect = {x, y, 80, 32};
    paletteBtn.type = "palette";
    paletteBtn.index = 0;
    toolbarButtons.push_back(paletteBtn);
    x += 80 + margin;

    int btnW = 64, btnH = 30;
    margin = 4;
    y = (toolbarHeight - btnH) / 2;
    struct { const char* label; const char* type; } textBtns[] = {
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
        btn.index = 0;
        toolbarButtons.push_back(btn);
        x += btnW + margin;
    }

    int sizeBtn = 32;
    margin = 3;
    y = (toolbarHeight - sizeBtn) / 2;

    Button minusBtn;
    minusBtn.rect = {x, y, sizeBtn, sizeBtn};
    minusBtn.type = "size_down";
    minusBtn.index = 0;
    toolbarButtons.push_back(minusBtn);
    x += sizeBtn + margin;

    Button sizeLabel;
    sizeLabel.rect = {x, y, 40, sizeBtn};
    sizeLabel.type = "size_label";
    sizeLabel.index = 0;
    toolbarButtons.push_back(sizeLabel);
    x += 40 + margin;

    Button plusBtn;
    plusBtn.rect = {x, y, sizeBtn, sizeBtn};
    plusBtn.type = "size_up";
    plusBtn.index = 0;
    toolbarButtons.push_back(plusBtn);
}

void PiPaint::updateCanvasTexture() {
    canvas->compositeToSurface(compositeSurface);
    SDL_Rect region = canvas->getDirtyRect();
    SDL_Rect* regionPtr = canvas->hasDirtyRegion() ? &region : nullptr;
    SDL_UpdateTexture(canvasTexture, regionPtr, compositeSurface->pixels, compositeSurface->pitch);
    canvas->clearDirty();
}

SDL_Texture* PiPaint::renderTextCached(TTF_Font* font, const std::string& text, SDL_Color col, int x, int y) {
    if (text.empty()) return nullptr;
    std::string key = std::to_string((uintptr_t)font) + ":" + text + ":" +
                      std::to_string(col.r) + std::to_string(col.g) + std::to_string(col.b);
    auto it = textCache.find(key);
    if (it != textCache.end()) {
        SDL_QueryTexture(it->second, nullptr, nullptr, nullptr, nullptr);
        SDL_Rect dst = {x, y, 0, 0};
        SDL_QueryTexture(it->second, nullptr, nullptr, &dst.w, &dst.h);
        SDL_RenderCopy(renderer, it->second, nullptr, &dst);
        return it->second;
    }
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), col);
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);
    if (!t) return nullptr;
    textCache[key] = t;
    SDL_Rect dst = {x, y, 0, 0};
    SDL_QueryTexture(t, nullptr, nullptr, &dst.w, &dst.h);
    SDL_RenderCopy(renderer, t, nullptr, &dst);
    return t;
}

void PiPaint::drawToolbar() {
    SDL_Rect toolbarBg = {0, 0, width, toolbarHeight};
    SDL_SetRenderDrawColor(renderer, (COLOR_TOOLBAR_BG>>16)&0xFF, (COLOR_TOOLBAR_BG>>8)&0xFF, COLOR_TOOLBAR_BG&0xFF, 255);
    SDL_RenderFillRect(renderer, &toolbarBg);
    SDL_SetRenderDrawColor(renderer, (COLOR_BORDER>>16)&0xFF, (COLOR_BORDER>>8)&0xFF, COLOR_BORDER&0xFF, 255);
    SDL_RenderDrawLine(renderer, 0, toolbarHeight, width, toolbarHeight);

    Uint32 currentColor = canvas->getCurrentColor();
    
    for (auto& btn : toolbarButtons) {
        if (btn.type == "color") {
            Uint32 col = currentPalette[btn.index];
            bool isSelected = (col == currentColor && !canvas->isEraserMode());
            drawColorSwatch(renderer, btn.rect, col, isSelected);
        } else if (btn.type == "palette") {
            bool isActive = showOverlay && overlayType == "palette";
            drawModernButton(renderer, btn.rect, COLOR_BTN_BG, COLOR_BORDER, "Palette", fontTiny, uintToColor(COLOR_TEXT), isActive);
        } else if (btn.type == "size_label") {
            char text[16];
            snprintf(text, sizeof(text), "%dpx", penSize);
            int tw, th;
            TTF_SizeText(fontTiny, text, &tw, &th);
            int tx = btn.rect.x + (btn.rect.w - tw) / 2;
            int ty = btn.rect.y + (btn.rect.h - th) / 2;
            renderText(renderer, fontTiny, text, uintToColor(COLOR_TEXT), tx, ty);
        } else if (btn.type == "size_up") {
            bool isActive = false;
            drawCircleButton(renderer, btn.rect, COLOR_BTN_BG, COLOR_BORDER, "+", fontMedium, uintToColor(COLOR_TEXT), isActive);
        } else if (btn.type == "size_down") {
            bool isActive = false;
            drawCircleButton(renderer, btn.rect, COLOR_BTN_BG, COLOR_BORDER, "-", fontMedium, uintToColor(COLOR_TEXT), isActive);
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

            Uint32 bg = COLOR_BTN_BG;
            if (btn.type == "eraser" && canvas->isEraserMode()) bg = COLOR_BTN_ACTIVE;
            else if (btn.type == "fill"   && fillArmed)             bg = COLOR_BTN_ACTIVE;
            else if (btn.type == "shape_line"    && shapeMode == ShapeMode::LINE)    bg = COLOR_BTN_ACTIVE;
            else if (btn.type == "shape_rect"    && shapeMode == ShapeMode::RECT)    bg = COLOR_BTN_ACTIVE;
            else if (btn.type == "shape_ellipse" && shapeMode == ShapeMode::ELLIPSE) bg = COLOR_BTN_ACTIVE;
            
            bool isActive = (bg == COLOR_BTN_ACTIVE);
            SDL_Color textCol = isActive ? uintToColor(COLOR_TEXT_WHITE) : uintToColor(COLOR_TEXT);
            drawModernButton(renderer, btn.rect, bg, COLOR_BORDER, label, fontTiny, textCol, false);
        }
    }
}

void PiPaint::handleTouchDown(int fingerId, int x, int y) {
    needsRender = true;
    lastTouchPos = {x, y};
    lastTapPos   = {x, y};
    lastTapTime  = SDL_GetTicks();

    if (showOverlay) {
        if (overlayType == "palette") {
            int svSize = 280;
            int hueW = 36, hueH = 280;
            int swatchSize = 48, swatchMargin = 8;
            int btnW = 120, btnH = 40;
            
            int panelW = svSize + hueW + 60;
            int panelH = 100 + svSize + 80 + btnH + 20;
            int panelX = (width - panelW) / 2;
            int panelY = (height - panelH) / 2;

            int tabY = panelY + 10;
            int tabW = 100, tabH = 32;
            int tabsX = panelX + 10;
            for (int i = 0; i < 5; i++) {
                SDL_Rect tabRect = {tabsX + i * (tabW + 8), tabY, tabW, tabH};
                if (x >= tabRect.x && x <= tabRect.x + tabRect.w && y >= tabRect.y && y <= tabRect.y + tabRect.h) {
                    selectedPaletteIndex = i;
                    if (i == 4) {
                        selectedSwatchIndex = -1;
                    }
                    return;
                }
            }

            int swatchY = panelY + 55;
            std::vector<Uint32>& displayColors = (selectedPaletteIndex < 4) ? 
                const_cast<std::vector<PalettePreset>&>(PALETTE_PRESETS)[selectedPaletteIndex].colors : currentPalette;
            
            for (int row = 0; row < 2; row++) {
                for (int col = 0; col < 5; col++) {
                    int idx = row * 5 + col;
                    if (idx >= (int)displayColors.size()) break;
                    SDL_Rect sRect = {tabsX + col * (swatchSize + swatchMargin), 
                                     swatchY + row * (swatchSize + swatchMargin), 
                                     swatchSize, swatchSize};
                    if (x >= sRect.x && x <= sRect.x + sRect.w && y >= sRect.y && y <= sRect.y + sRect.h) {
                        selectedSwatchIndex = idx;
                        Uint32 col = displayColors[idx];
                        canvas->setColor((col>>16)&0xFF, (col>>8)&0xFF, col&0xFF);
                        return;
                    }
                }
            }

            int pickerY = swatchY + 2 * (swatchSize + swatchMargin) + 20;
            SDL_Rect svRect = {panelX + 20, pickerY, svSize, svSize};
            if (x >= svRect.x && x <= svRect.x + svRect.w && y >= svRect.y && y <= svRect.y + svRect.h) {
                currentSat = (float)(x - svRect.x) / svSize;
                currentVal = 1.0f - (float)(y - svRect.y) / svSize;
                if (currentSat < 0) currentSat = 0;
                if (currentSat > 1) currentSat = 1;
                if (currentVal < 0) currentVal = 0;
                if (currentVal > 1) currentVal = 1;
                Uint8 r, g, b;
                hsvToRgb(currentHue, currentSat, currentVal, r, g, b);
                canvas->setColor(r, g, b);
                if (selectedSwatchIndex >= 0 && selectedSwatchIndex < 10) {
                    currentPalette[selectedSwatchIndex] = _rgb(r, g, b);
                }
                return;
            }

            int hueX = svRect.x + svSize + 15;
            SDL_Rect hueRect = {hueX, pickerY, hueW, hueH};
            if (x >= hueRect.x && x <= hueRect.x + hueRect.w && y >= hueRect.y && y <= hueRect.y + hueRect.h) {
                currentHue = 1.0f - (float)(y - hueRect.y) / hueH;
                if (currentHue < 0) currentHue = 0;
                if (currentHue > 1) currentHue = 1;
                generateSvSquareTexture();
                Uint8 r, g, b;
                hsvToRgb(currentHue, currentSat, currentVal, r, g, b);
                canvas->setColor(r, g, b);
                return;
            }

            int btnY = panelY + panelH - btnH - 15;
            int btnSpacing = (panelW - btnW * 4 - 30) / 3;
            
            SDL_Rect saveBtn = {panelX + 15, btnY, btnW, btnH};
            SDL_Rect loadBtn = {saveBtn.x + btnW + btnSpacing/2, btnY, btnW, btnH};
            SDL_Rect resetBtn = {loadBtn.x + btnW + btnSpacing/2, btnY, btnW, btnH};
            SDL_Rect closeBtn = {resetBtn.x + btnW + btnSpacing/2, btnY, btnW, btnH};

            if (x >= saveBtn.x && x <= saveBtn.x + saveBtn.w && y >= saveBtn.y && y <= saveBtn.y + saveBtn.h) {
                system("mkdir -p ~/pi-paint/palettes");
                time_t now = time(nullptr);
                char fname[256];
                char timebuf[64];
                strftime(timebuf, sizeof(timebuf), "%Y-%m-%d_%H%M%S", localtime(&now));
                snprintf(fname, sizeof(fname), "%s/palette_%s.json", std::string(getenv("HOME") ? getenv("HOME") : ".").c_str(), timebuf);
                savePalette(fname);
                return;
            }

            if (x >= loadBtn.x && x <= loadBtn.x + loadBtn.w && y >= loadBtn.y && y <= loadBtn.y + loadBtn.h) {
                showOverlay = false;
                overlayType = "load_palette";
                showLoadOverlay();
                return;
            }

            if (x >= resetBtn.x && x <= resetBtn.x + resetBtn.w && y >= resetBtn.y && y <= resetBtn.y + resetBtn.h) {
                resetCustomPalette();
                return;
            }

            if (x >= closeBtn.x && x <= closeBtn.x + closeBtn.w && y >= closeBtn.y && y <= closeBtn.y + closeBtn.h) {
                showOverlay = false;
                return;
            }
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
        if (activeFingers.empty()) { canvas->floodFill(x, y); fillArmed = false; }
    } else if (shapeMode != ShapeMode::NONE) {
        if (shapeOwnerFinger == -1) {
            shapeOwnerFinger = fingerId;
            shapeStart = shapeCurrent = {x, y};
            shapeDragging = true;
            activeFingers.insert(fingerId);
        }
    } else {
        activeFingers.insert(fingerId);
        canvas->startStroke(x, y, fingerId);
    }
}

void PiPaint::handleTouchMove(int fingerId, int x, int y) {
    needsRender = true;
    lastTouchPos = {x, y};
    activeFingerPos[fingerId] = {x, y};

    if (!activeFingers.count(fingerId)) return;
    if (y <= toolbarHeight) return;

    if (shapeDragging && fingerId == shapeOwnerFinger) {
        shapeCurrent = {x, y};
    } else if (!shapeDragging) {
        canvas->continueStroke(x, y, fingerId);
    }
}

void PiPaint::handleTouchUp(int fingerId) {
    needsRender = true;
    activeFingers.erase(fingerId);
    activeFingerPos.erase(fingerId);
    if (activeFingers.empty()) lastTouchPos = {-1, -1};

    if (shapeDragging && fingerId == shapeOwnerFinger) {
        commitShape(shapeStart.x, shapeStart.y, shapeCurrent.x, shapeCurrent.y);
        shapeDragging = false;
        shapeOwnerFinger = -1;
    } else {
        canvas->endStroke(fingerId);
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
    else { canvas->continueStroke(ev.x, ev.y, 0); }
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
            canvas->undo();
            updateCanvasTexture();
        } else if (ev.keysym.sym == SDLK_y && (ev.keysym.mod & KMOD_CTRL)) {
            canvas->redo();
            updateCanvasTexture();
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
            case 0: canvas->setColor(0,0,0); break;
            case 1: canvas->setColor(255,59,48); break;
            case 2: canvas->setColor(40,205,65); break;
            case 3: canvas->setColor(0,122,255); break;
            case 4: canvas->setColor(255,204,0); break;
            case 5: canvas->setColor(175,82,222); break;
            case 6: canvas->setColor(255,149,0); break;
            case 7: canvas->setColor(255,45,85); break;
            case 8: canvas->setColor(90,200,250); break;
            case 9: canvas->setColor(255,255,255); break;
        }
    } else if (type == "palette") {
        showPalette();
    } else if (type == "eraser") {
        canvas->toggleEraser();
        shapeMode = ShapeMode::NONE;
    } else if (type == "fill") {
        canvas->toggleFill();
        fillArmed = canvas->isFillMode();
        shapeMode = ShapeMode::NONE;
    } else if (type == "shape_line") {
        shapeMode = (shapeMode == ShapeMode::LINE) ? ShapeMode::NONE : ShapeMode::LINE;
        fillArmed = false;
        if (canvas->isEraserMode()) canvas->toggleEraser();
    } else if (type == "shape_rect") {
        shapeMode = (shapeMode == ShapeMode::RECT) ? ShapeMode::NONE : ShapeMode::RECT;
        fillArmed = false;
        if (canvas->isEraserMode()) canvas->toggleEraser();
    } else if (type == "shape_ellipse") {
        shapeMode = (shapeMode == ShapeMode::ELLIPSE) ? ShapeMode::NONE : ShapeMode::ELLIPSE;
        fillArmed = false;
        if (canvas->isEraserMode()) canvas->toggleEraser();
    } else if (type == "bg") {
        canvas->toggleBackground();
        needsRender = true;
    } else if (type == "undo") {
        canvas->undo();
        updateCanvasTexture();
        needsRender = true;
    } else if (type == "redo") {
        canvas->redo();
        updateCanvasTexture();
        needsRender = true;
    } else if (type == "clear") {
    canvas->clear();
        needsRender = true;
    } else if (type == "new") {
        newCanvas();
    } else if (type == "save") {
        showSaveOverlay();
    } else if (type == "load") {
        showLoadOverlay();
    } else if (type == "size_up") {
        penSize = std::min(50, penSize + 1);
        canvas->setSize(penSize);
    } else if (type == "size_down") {
        penSize = std::max(1, penSize - 1);
        canvas->setSize(penSize);
    }
}

void PiPaint::newCanvas() {
    canvas->resetToBlank();
    shapeMode = ShapeMode::NONE;
    shapeDragging = false;
    shapeOwnerFinger = -1;
}

void PiPaint::showPalette() {
    overlayType = "palette";
    showOverlay = true;
    browsingFolder = false;
    selectedSwatchIndex = -1;
    Uint32 col = canvas->getCurrentColor();
    Uint8 r = (col >> 16) & 0xFF;
    Uint8 g = (col >> 8) & 0xFF;
    Uint8 b = col & 0xFF;
    rgbToHsv(r, g, b, currentHue, currentSat, currentVal);
    generateSvSquareTexture();
}

void PiPaint::generateSvSquareTexture() {
    if (svSquareTexture) SDL_DestroyTexture(svSquareTexture);
    
    int svSize = 280;
    SDL_Surface* svSurf = SDL_CreateRGBSurfaceWithFormat(0, svSize, svSize, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!svSurf) return;
    
    for (int y = 0; y < svSize; y++) {
        for (int x = 0; x < svSize; x++) {
            float sat = (float)x / svSize;
            float val = 1.0f - (float)y / svSize;
            Uint8 rr, gg, bb;
            hsvToRgb(currentHue, sat, val, rr, gg, bb);
            ((Uint32*)svSurf->pixels)[y * svSize + x] = SDL_MapRGB(svSurf->format, rr, gg, bb);
        }
    }
    svSquareTexture = SDL_CreateTextureFromSurface(renderer, svSurf);
    SDL_FreeSurface(svSurf);
}

void PiPaint::drawPaletteOverlay() {
    int svSize = 280;
    int hueW = 36, hueH = 280;
    int swatchSize = 48, swatchMargin = 8;
    int btnW = 120, btnH = 40;
    
    int panelW = svSize + hueW + 60;
    int panelH = 100 + svSize + 80 + btnH + 20;
    int panelX = (width - panelW) / 2;
    int panelY = (height - panelH) / 2;

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect bg = {panelX, panelY, panelW, panelH};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 180, 180, 185, 255);
    SDL_RenderDrawRect(renderer, &bg);

    int tabY = panelY + 10;
    int tabW = 100, tabH = 32;
    int tabsX = panelX + 10;
    struct { const char* label; } tabs[] = {
        {"Material"}, {"Pastel"}, {"Neon"}, {"Earth"}, {"Custom"}
    };
    for (int i = 0; i < 5; i++) {
        SDL_Rect tabRect = {tabsX + i * (tabW + 8), tabY, tabW, tabH};
        bool isActive = (selectedPaletteIndex == i);
        Uint32 bg = isActive ? COLOR_BTN_ACTIVE : COLOR_BTN_BG;
        SDL_Color txt = isActive ? uintToColor(COLOR_TEXT_WHITE) : uintToColor(COLOR_TEXT);
        drawModernButton(renderer, tabRect, bg, COLOR_BORDER, (char*)tabs[i].label, fontTiny, txt, false);
    }

    int swatchY = panelY + 55;
    std::vector<Uint32>& displayColors = (selectedPaletteIndex < 4) ? 
        const_cast<std::vector<PalettePreset>&>(PALETTE_PRESETS)[selectedPaletteIndex].colors : currentPalette;
    
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 5; col++) {
            int idx = row * 5 + col;
            if (idx >= (int)displayColors.size()) break;
            SDL_Rect sRect = {tabsX + col * (swatchSize + swatchMargin), 
                             swatchY + row * (swatchSize + swatchMargin), 
                             swatchSize, swatchSize};
            drawColorSwatch(renderer, sRect, displayColors[idx], false);
        }
    }

    int pickerY = swatchY + 2 * (swatchSize + swatchMargin) + 20;
    SDL_Rect svRect = {panelX + 20, pickerY, svSize, svSize};
    SDL_RenderCopy(renderer, svSquareTexture, nullptr, &svRect);
    SDL_SetRenderDrawColor(renderer, 180, 180, 185, 255);
    SDL_RenderDrawRect(renderer, &svRect);

    int svX = svRect.x + (int)(currentSat * svSize);
    int svY = svRect.y + (int)((1.0f - currentVal) * svSize);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawLine(renderer, svX - 6, svY, svX + 6, svY);
    SDL_RenderDrawLine(renderer, svX, svY - 6, svX, svY + 6);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawLine(renderer, svX - 8, svY, svX + 8, svY);
    SDL_RenderDrawLine(renderer, svX, svY - 8, svX, svY + 8);

    int hueX = svRect.x + svSize + 15;
    SDL_Rect hueRect = {hueX, pickerY, hueW, hueH};
    for (int y = 0; y < hueH; y++) {
        float hue = 1.0f - (float)y / hueH;
        Uint8 rr, gg, bb;
        hsvToRgb(hue, 1.0f, 1.0f, rr, gg, bb);
        SDL_SetRenderDrawColor(renderer, rr, gg, bb, 255);
        SDL_RenderDrawLine(renderer, hueX, pickerY + y, hueX + hueW, pickerY + y);
    }
    SDL_SetRenderDrawColor(renderer, 180, 180, 185, 255);
    SDL_RenderDrawRect(renderer, &hueRect);

    int markerY = pickerY + (int)((1.0f - currentHue) * hueH);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawLine(renderer, hueX - 3, markerY, hueX + hueW + 3, markerY);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawLine(renderer, hueX - 5, markerY, hueX + hueW + 5, markerY);

    int btnY = panelY + panelH - btnH - 15;
    int btnSpacing = (panelW - btnW * 4 - 30) / 3;
    
    SDL_Rect saveBtn = {panelX + 15, btnY, btnW, btnH};
    drawModernButton(renderer, saveBtn, COLOR_BTN_BG, COLOR_BORDER, "Save", fontTiny, uintToColor(COLOR_TEXT), false);
    
    SDL_Rect loadBtn = {saveBtn.x + btnW + btnSpacing/2, btnY, btnW, btnH};
    drawModernButton(renderer, loadBtn, COLOR_BTN_BG, COLOR_BORDER, "Load", fontTiny, uintToColor(COLOR_TEXT), false);
    
    SDL_Rect resetBtn = {loadBtn.x + btnW + btnSpacing/2, btnY, btnW, btnH};
    drawModernButton(renderer, resetBtn, COLOR_BTN_BG, COLOR_BORDER, "Reset", fontTiny, uintToColor(COLOR_TEXT), false);
    
    SDL_Rect closeBtn = {resetBtn.x + btnW + btnSpacing/2, btnY, btnW, btnH};
    drawModernButton(renderer, closeBtn, COLOR_BTN_BG, COLOR_BORDER, "Close", fontTiny, uintToColor(COLOR_TEXT), false);
}

void PiPaint::savePalette(const std::string& filename) {
    FILE* f = fopen(filename.c_str(), "w");
    if (!f) return;
    fprintf(f, "{\n  \"name\": \"My Palette\",\n");
    time_t now = time(nullptr);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d_%H%M%S", localtime(&now));
    fprintf(f, "  \"created\": \"%s\",\n", timebuf);
    fprintf(f, "  \"colors\": [\n");
    for (int i = 0; i < 10; i++) {
        Uint32 c = currentPalette[i];
        fprintf(f, "    \"#%02X%02X%02X\"%s\n", 
                (c>>16)&0xFF, (c>>8)&0xFF, c&0xFF,
                i < 9 ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
}

bool PiPaint::loadPalette(const std::string& filename) {
    FILE* f = fopen(filename.c_str(), "r");
    if (!f) return false;
    char line[256];
    bool inColors = false;
    int colorIdx = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "\"colors\"")) {
            inColors = true;
            continue;
        }
        if (inColors && strstr(line, "\"")) {
            int r, g, b;
            if (sscanf(line, "    \"#%2x%2x%2x\"", &r, &g, &b) == 3) {
                if (colorIdx < 10) {
                    currentPalette[colorIdx] = _rgb(r, g, b);
                    colorIdx++;
                }
            }
        }
    }
    fclose(f);
    return colorIdx > 0;
}

void PiPaint::resetCustomPalette() {
    currentPalette = PALETTE_PRESETS[0].colors;
}

void PiPaint::commitShape(int x1, int y1, int x2, int y2) {
    switch (shapeMode) {
        case ShapeMode::LINE:
            canvas->drawShapeLine(x1, y1, x2, y2);
            break;
        case ShapeMode::RECT:
            canvas->drawShapeRect(x1, y1, x2, y2);
            break;
        case ShapeMode::ELLIPSE: {
            int cx = (x1 + x2) / 2;
            int cy = (y1 + y2) / 2;
            int rx = std::abs(x2 - x1) / 2;
            int ry = std::abs(y2 - y1) / 2;
            canvas->drawShapeEllipse(cx, cy, rx, ry);
            break;
        }
        default: break;
    }
}

void PiPaint::drawGhostShape() {
    if (!shapeDragging || shapeMode == ShapeMode::NONE) return;

    int x1 = shapeStart.x,   y1 = shapeStart.y;
    int x2 = shapeCurrent.x, y2 = shapeCurrent.y;

    Uint32 col = canvas->getCurrentColor();
    Uint8 r = (col >> 16) & 0xFF;
    Uint8 g = (col >>  8) & 0xFF;
    Uint8 b =  col        & 0xFF;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, r, g, b, 180);

    int thick = std::max(1, canvas->getPenSize());

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

    if (overlayType == "palette") {
        drawPaletteOverlay();
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
    const Uint32 BASE_FRAME_MS = 16;
    Uint32 frameMs = BASE_FRAME_MS;
    Uint32 lastRender = SDL_GetTicks();
    int frameSkipCount = 0;

    while (true) {
        std::vector<SDL_Event> touchEvents;
        touch->processEvents(touchEvents);
        for (auto& ev : touchEvents) {
            if (ev.type == SDL_FINGERDOWN) {
                int x = (int)(ev.tfinger.x * width);
                int y = (int)(ev.tfinger.y * height);
                if (ev.tfinger.pressure > 0.0f && ev.tfinger.pressure != 0.5f) {
                    int pressureSize = std::max(1, (int)(ev.tfinger.pressure * penSize * 2.0f));
                    canvas->setSize(std::min(pressureSize, 50));
                }
                handleTouchDown(ev.tfinger.fingerId, x, y);
            } else if (ev.type == SDL_FINGERMOTION) {
                int x = (int)(ev.tfinger.x * width);
                int y = (int)(ev.tfinger.y * height);
                if (ev.tfinger.pressure > 0.0f && ev.tfinger.pressure != 0.5f) {
                    int pressureSize = std::max(1, (int)(ev.tfinger.pressure * penSize * 2.0f));
                    canvas->setSize(std::min(pressureSize, 50));
                }
                handleTouchMove(ev.tfinger.fingerId, x, y);
            } else if (ev.type == SDL_FINGERUP) {
                canvas->setSize(penSize);
                handleTouchUp(ev.tfinger.fingerId);
            }
        }

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
            else if (e.type == SDL_MOUSEBUTTONDOWN) handleMouseButtonDown(e.button);
            else if (e.type == SDL_MOUSEMOTION)     handleMouseMotion(e.motion);
            else if (e.type == SDL_MOUSEBUTTONUP)   handleMouseButtonUp(e.button);
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) handleKeyboard(e.key);
        }

        if (tempFile) {
            rewind(tempFile);
            int temp = 0;
            if (fscanf(tempFile, "%d", &temp) == 1) {
                if (temp > 65000) {
                    frameMs = 33;
                    frameSkipCount = 2;
                } else if (temp > 55000) {
                    frameMs = 20;
                    frameSkipCount = 1;
                } else {
                    frameMs = BASE_FRAME_MS;
                    frameSkipCount = 0;
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        if (needsRender || (now - lastRender) >= frameMs) {
            lastRender = now;

            updateCanvasTexture();
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, canvasTexture, nullptr, nullptr);
            drawToolbar();
            if (showOverlay) drawOverlay();
            drawGhostShape();
            SDL_RenderPresent(renderer);
            needsRender = false;
        } else {
            SDL_Delay(1);
        }
    }
}

int main(int argc, char* argv[]) {
    PiPaint app;
    app.run();
    return 0;
}
