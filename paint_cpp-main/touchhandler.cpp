#include "touchhandler.h"
#include <libevdev/libevdev.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/select.h>
#include <cerrno>
#include <iostream>
#include <dirent.h>
#include <cstring>
#include <chrono>

static std::string findTouchDevice() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return "";
    struct dirent* entry;
    std::string result;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        std::string path = "/dev/input/" + std::string(entry->d_name);
        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) continue;
        struct libevdev* dev = nullptr;
        if (libevdev_new_from_fd(fd, &dev) == 0) {
            const char* name = libevdev_get_name(dev);
            std::cout << "Found input device: " << name << " at " << path << std::endl;
            if (name && (strstr(name, "Touch") || strstr(name, "p403") || strstr(name, "Virtual Ink"))) {
                result = path;
                libevdev_free(dev);
                close(fd);
                break;
            }
            libevdev_free(dev);
        }
        close(fd);
    }
    closedir(dir);
    return result;
}

TouchHandler::TouchHandler(int w, int h) : screenW(w), screenH(h) {
    calibrationX = calibrationY = 0;
    touchXMax = 4095;
    touchYMax = 4095;
    pressureMax = 255;
    dev = nullptr;
    fd = -1;

    for (int i = 0; i < TOUCH_QUEUE_SIZE; i++) {
        touchQueue[i].valid = false;
    }
}

TouchHandler::~TouchHandler() {
    stopInputThread();
    if (dev) libevdev_free(dev);
    if (fd != -1) close(fd);
}

bool TouchHandler::init() {
    std::string devicePath = findTouchDevice();
    if (devicePath.empty()) {
        std::cerr << "No touch device found.\n";
        return false;
    }
    std::cout << "Using touch device: " << devicePath << std::endl;
    fd = open(devicePath.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;

    if (libevdev_new_from_fd(fd, &dev) < 0) {
        close(fd);
        fd = -1;
        return false;
    }

    const struct input_absinfo* abs_x = libevdev_get_abs_info(dev, ABS_MT_POSITION_X);
    const struct input_absinfo* abs_y = libevdev_get_abs_info(dev, ABS_MT_POSITION_Y);
    const struct input_absinfo* abs_p = libevdev_get_abs_info(dev, ABS_MT_PRESSURE);
    if (abs_x) touchXMax = abs_x->maximum;
    if (abs_y) touchYMax = abs_y->maximum;
    if (abs_p) pressureMax = abs_p->maximum;
    std::cout << "Touch range: " << touchXMax << "x" << touchYMax
              << " pressure max: " << pressureMax << std::endl;
    return true;
}

void TouchHandler::enqueueTouchEvent(int type, int fingerId, int x, int y, float pressure) {
    int nextWrite = (queueWrite.load() + 1) % TOUCH_QUEUE_SIZE;
    int currentRead = queueRead.load();

    if (nextWrite == currentRead) {
        return;
    }

    std::lock_guard<std::mutex> lock(queueMutex);
    touchQueue[nextWrite] = {type, fingerId, x, y, pressure, true};
    queueWrite.store(nextWrite);
}

bool TouchHandler::pollTouchEvent(SDL_Event& out) {
    if (queueRead.load() == queueWrite.load()) {
        return false;
    }

    int nextRead = (queueRead.load() + 1) % TOUCH_QUEUE_SIZE;
    
    std::lock_guard<std::mutex> lock(queueMutex);
    if (!touchQueue[nextRead].valid) {
        return false;
    }

    const TouchEvent& ev = touchQueue[nextRead];
    out.type = ev.type;
    out.tfinger.fingerId = ev.fingerId;
    out.tfinger.x = ev.x / (float)screenW;
    out.tfinger.y = ev.y / (float)screenH;
    out.tfinger.dx = 0;
    out.tfinger.dy = 0;
    out.tfinger.pressure = ev.pressure;

    touchQueue[nextRead].valid = false;
    queueRead.store(nextRead);

    return true;
}

void TouchHandler::processEvents() {
    if (!dev) return;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = {0, 1000};
    if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0) return;

    struct input_event ev;
    struct StagedSlot {
        int x, y;
        float pressure;
        bool hasPos;
        bool moved;
    };
    std::map<int, StagedSlot> staged;

    for (auto& [tid, state] : currentStates) {
        staged[tid] = {state.x, state.y, state.pressure, true, false};
    }

    while (true) {
        int rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            while (rc == LIBEVDEV_READ_STATUS_SYNC)
                rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
            continue;
        }
        if (rc == -EAGAIN) break;
        if (rc < 0) break;

        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_SLOT) {
                currentSlot = ev.value;
            }
            else if (ev.code == ABS_MT_TRACKING_ID) {
                if (ev.value == -1) {
                    auto it = slotToTrackingId.find(currentSlot);
                    if (it != slotToTrackingId.end()) {
                        int trackingId = it->second;
                        auto stateIt = currentStates.find(trackingId);
                        if (stateIt != currentStates.end()) {
                            enqueueTouchEvent(SDL_FINGERUP, trackingId,
                                               stateIt->second.x, stateIt->second.y,
                                               stateIt->second.pressure);
                            currentStates.erase(stateIt);
                        }
                        slotToTrackingId.erase(it);
                        staged.erase(trackingId);
                    }
                } else {
                    slotToTrackingId[currentSlot] = ev.value;
                    staged[ev.value] = {0, 0, 0.5f, false, false};
                }
            }
            else if (ev.code == ABS_MT_POSITION_X) {
                auto it = slotToTrackingId.find(currentSlot);
                if (it != slotToTrackingId.end()) {
                    int tid = it->second;
                    auto& ss = staged[tid];
                    ss.x = ev.value * screenW / touchXMax;
                    ss.moved = true;
                }
            }
            else if (ev.code == ABS_MT_POSITION_Y) {
                auto it = slotToTrackingId.find(currentSlot);
                if (it != slotToTrackingId.end()) {
                    int tid = it->second;
                    auto& ss = staged[tid];
                    ss.y = ev.value * screenH / touchYMax;
                    ss.moved = true;
                    ss.hasPos = true;
                }
            }
            else if (ev.code == ABS_MT_PRESSURE) {
                auto it = slotToTrackingId.find(currentSlot);
                if (it != slotToTrackingId.end()) {
                    int tid = it->second;
                    auto& ss = staged[tid];
                    ss.pressure = (pressureMax > 0)
                        ? std::max(0.0f, std::min(1.0f, (float)ev.value / pressureMax))
                        : 0.5f;
                    ss.moved = true;
                }
            }
        }
        else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            for (auto& [tid, ss] : staged) {
                int x = ss.x, y = ss.y;
                applyCalibration(x, y);
                float pressure = ss.pressure;
                bool known = currentStates.count(tid) > 0;

                if (!known && ss.hasPos) {
                    enqueueTouchEvent(SDL_FINGERDOWN, tid, x, y, pressure);
                    currentStates[tid] = {x, y, pressure};
                } else if (known && ss.moved) {
                    enqueueTouchEvent(SDL_FINGERMOTION, tid, x, y, pressure);
                    currentStates[tid] = {x, y, pressure};
                }
            }
            for (auto& [tid, ss] : staged) ss.moved = false;
        }
    }
}

void TouchHandler::startInputThread() {
    if (inputThread) return;
    inputThreadRunning.store(true);
    inputThread = new std::thread([this]() {
        std::cout << "Touch input thread started" << std::endl;
        while (inputThreadRunning.load()) {
            processEvents();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        std::cout << "Touch input thread stopped" << std::endl;
    });
}

void TouchHandler::stopInputThread() {
    if (!inputThread) return;
    inputThreadRunning.store(false);
    if (inputThread->joinable()) {
        inputThread->join();
    }
    delete inputThread;
    inputThread = nullptr;
}

void TouchHandler::applyCalibration(int& x, int& y) {
    x += calibrationX;
    y += calibrationY;
    x = std::max(0, std::min(screenW - 1, x));
    y = std::max(0, std::min(screenH - 1, y));
}

void TouchHandler::setCalibration(int xOffset, int yOffset) {
    calibrationX = xOffset;
    calibrationY = yOffset;
}

void TouchHandler::getCalibration(int& xOffset, int& yOffset) const {
    xOffset = calibrationX;
    yOffset = calibrationY;
}
