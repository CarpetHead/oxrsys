// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace oxrsys::encode_budget
{

enum class StreamMode : uint8_t
{
    Color = 0,
    PackedAlpha = 1,
};

// Picks a foveation level per stream mode from the measured encode time: a level whose frames queue up
// behind the encoder steps to a stronger one and is not tried again, and a level with spare time steps
// back towards the configured foveation until it reaches one that was too slow. A saturated encoder's
// average climbs far past the frame period, while a busy app only nudges it over.
class Controller
{
public:
    static constexpr uint32_t OverloadedWindows = 3;
    static constexpr uint32_t HeadroomWindows = 5;
    static constexpr double OverloadedFraction = 1.15;
    static constexpr double HeadroomFraction = 0.75;

    explicit Controller(uint32_t maxLevel) : maxLevel_(maxLevel) { Reset(); }

    void Reset()
    {
        modes_ = {};
        overloadedWindows_ = 0;
        headroomWindows_ = 0;
    }

    bool HasLevel(StreamMode mode) const { return modes_[Index(mode)].hasLevel; }
    uint32_t Level(StreamMode mode) const { return modes_[Index(mode)].level; }

    void SetLevel(StreamMode mode, uint32_t level)
    {
        ModeState& state = modes_[Index(mode)];
        state.level = std::min(level, maxLevel_);
        state.hasLevel = true;
        overloadedWindows_ = 0;
        headroomWindows_ = 0;
    }

    // One measurement window of the given mode's current level; returns the level to stream next.
    uint32_t Update(StreamMode mode, double averageEncodeMs, double framePeriodMs)
    {
        ModeState& state = modes_[Index(mode)];
        state.hasLevel = true;
        if (averageEncodeMs > framePeriodMs * OverloadedFraction)
        {
            headroomWindows_ = 0;
            if (++overloadedWindows_ >= OverloadedWindows && state.level < maxLevel_)
            {
                state.slowestUsableLevel = std::max(state.slowestUsableLevel, state.level + 1);
                ++state.level;
                overloadedWindows_ = 0;
            }
            return state.level;
        }

        overloadedWindows_ = 0;
        if (averageEncodeMs < framePeriodMs * HeadroomFraction)
        {
            if (++headroomWindows_ >= HeadroomWindows && state.level > state.slowestUsableLevel)
            {
                --state.level;
                headroomWindows_ = 0;
            }
            return state.level;
        }

        headroomWindows_ = 0;
        return state.level;
    }

private:
    struct ModeState
    {
        uint32_t level = 0;
        uint32_t slowestUsableLevel = 0;
        bool hasLevel = false;
    };

    static size_t Index(StreamMode mode) { return static_cast<size_t>(mode); }

    uint32_t maxLevel_ = 0;
    std::array<ModeState, 2> modes_ = {};
    uint32_t overloadedWindows_ = 0;
    uint32_t headroomWindows_ = 0;
};

} // namespace oxrsys::encode_budget
