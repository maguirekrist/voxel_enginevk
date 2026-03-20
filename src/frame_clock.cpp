#include "frame_clock.h"

FrameClock::FrameClock()
    : _lastFrameTime(Clock::now()),
      _lastFpsTime(Clock::now())
{
}

float FrameClock::tick_frame()
{
    const TimePoint now = Clock::now();
    const std::chrono::duration<float> elapsed = now - _lastFrameTime;
    _lastFrameTime = now;
    return elapsed.count();
}

void FrameClock::report_frame_rendered()
{
    ++_framesSinceReport;

    const std::chrono::duration<float> elapsed = Clock::now() - _lastFpsTime;
    if (elapsed.count() < 1.0f)
    {
        return;
    }

    const float fps = _framesSinceReport / elapsed.count();
    std::println("FPS: {}", fps);
    _framesSinceReport = 0;
    _lastFpsTime = Clock::now();
}
