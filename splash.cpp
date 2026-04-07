#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <errno.h>
#include <cstring>

int main() {
    // Clear framebuffer
    system("dd if=/dev/zero of=/dev/fb0 bs=1024 count=1024 2>/dev/null");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG) {
        std::cerr << "IMG_Init failed: " << IMG_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) != 0) {
        std::cerr << "SDL_GetCurrentDisplayMode failed: " << SDL_GetError() << std::endl;
        IMG_Quit();
        SDL_Quit();
        return 1;
    }
    int w = dm.w, h = dm.h;

    SDL_Window* win = SDL_CreateWindow("Splash", 0, 0, w, h,
                                       SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        IMG_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(win);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Surface* img = IMG_Load("splash.png");
    if (img) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, img);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(img);
    } else {
        std::cerr << "Could not load splash.png, using solid color." << std::endl;
        SDL_SetRenderDrawColor(ren, 30, 30, 50, 255);
        SDL_RenderClear(ren);
    }
    SDL_RenderPresent(ren);
    SDL_Delay(3000);   // 3 seconds

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();

    // Launch the main application
    const char* app_path = "/home/snsu/paint_cpp/pipaint";
    if (access(app_path, X_OK) != 0) {
        std::cerr << "Main application not found or not executable: " << app_path << std::endl;
        return 1;
    }
    execl(app_path, "pipaint", nullptr);
    std::cerr << "exec failed: " << strerror(errno) << std::endl;
    return 1;
}
