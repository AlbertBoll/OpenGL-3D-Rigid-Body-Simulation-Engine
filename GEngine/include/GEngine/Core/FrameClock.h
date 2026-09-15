#pragma once
#include "Timestep.h"
#include <algorithm>
#include <chrono>

namespace GEngine
{
    struct FrameTime
    {
        Seconds rawDelta{};     // Actual wall time, including pacing and stalls.
        Seconds clampedDelta{};
        Seconds renderDelta{};  // Presentation policy; no interpolation yet.
    };

    // Measures frames only. The scene remains the sole fixed-step scheduler.
    class FrameClock
    {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        static constexpr Seconds MaxRenderDelta{0.25};

        explicit FrameClock(TimePoint now = Clock::now()) : m_LastSample(now) {}

        void Reset(TimePoint now = Clock::now()) { m_LastSample = now; }
        TimePoint LastSample() const { return m_LastSample; }

        FrameTime Tick(TimePoint now = Clock::now())
        {
            // An injected backwards sample must neither produce negative time nor
            // move the anchor back and count the same interval twice.
            const Seconds raw = now >= m_LastSample ? Seconds(now - m_LastSample) : Seconds::zero();
            m_LastSample = std::max(m_LastSample, now);
            const Seconds clamped = std::min(raw, MaxRenderDelta);
            return {raw, clamped, clamped};
        }

    private:
        TimePoint m_LastSample;
    };
}
