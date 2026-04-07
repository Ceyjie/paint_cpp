#ifndef TOUCHHANDLER_H
#define TOUCHHANDLER_H

#include <SDL2/SDL.h>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <mutex>

struct libevdev;

class TouchHandler {
public:
    TouchHandler(int screenWidth, int screenHeight);
    ~TouchHandler();

    bool init();
    void processEvents();
    void setCalibration(int xOffset, int yOffset);
    void getCalibration(int& xOffset, int& yOffset) const;

    void startInputThread();
    void stopInputThread();
    bool pollTouchEvent(SDL_Event& out);

private:
    int screenW, screenH;
    int calibrationX, calibrationY;
    int touchXMax, touchYMax;
    struct libevdev* dev;
    int fd;
    int pressureMax;
    int currentSlot = 0;

    std::map<int, int> slotToTrackingId;
    struct SlotState { int x, y; float pressure; };
    std::map<int, SlotState> currentStates;

    void applyCalibration(int& x, int& y);
    void enqueueTouchEvent(int type, int fingerId, int x, int y, float pressure);

    static const int TOUCH_QUEUE_SIZE = 256;
    struct TouchEvent {
        int type;
        int fingerId;
        int x, y;
        float pressure;
        bool valid;
    };
    TouchEvent touchQueue[TOUCH_QUEUE_SIZE];
    std::atomic<int> queueWrite{0};
    std::atomic<int> queueRead{0};
    std::atomic<bool> inputThreadRunning{false};
    std::thread* inputThread = nullptr;
    std::mutex queueMutex;
};

#endif
