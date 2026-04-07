#ifndef TOUCHHANDLER_H
#define TOUCHHANDLER_H

#include <SDL2/SDL.h>
#include <vector>
#include <map>
#include <thread>
#include <atomic>

struct libevdev;

class TouchHandler {
public:
    TouchHandler(int screenWidth, int screenHeight);
    ~TouchHandler();

    bool init();
    void processEvents(std::vector<SDL_Event>& events);
    void setCalibration(int xOffset, int yOffset);
    void getCalibration(int& xOffset, int& yOffset) const;
    void startInputThread();
    void stopInputThread();

private:
    int screenW, screenH;
    int calibrationX, calibrationY;
    int touchXMax, touchYMax;
    struct libevdev* dev;
    int fd;
    int pressureMax;
    int currentSlot = 0;

    std::thread* inputThread = nullptr;
    std::atomic<bool> inputThreadRunning{false};

    // Map slot -> tracking ID (for active touches)
    std::map<int, int> slotToTrackingId;
    // Map tracking ID -> current state (coordinates, pressure)
    struct SlotState { int x, y; float pressure; };
    std::map<int, SlotState> currentStates;

    void applyCalibration(int& x, int& y);
    void generateTouchEvent(int type, int fingerId, int x, int y, float pressure, std::vector<SDL_Event>& events);
};

#endif
