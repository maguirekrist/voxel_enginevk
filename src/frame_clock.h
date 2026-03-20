#pragma once

#include <vk_types.h>

class FrameClock
{
public:
    FrameClock();

    float tick_frame();
    void report_frame_rendered();

private:
    TimePoint _lastFrameTime;
    TimePoint _lastFpsTime;
    int _framesSinceReport{0};
};
