// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>

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
    // Twice the frame period is a saturated encoder, not a busy moment: one window is enough to step.
    static constexpr double SaturatedFraction = 2.0;

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
            const uint32_t windowsToStep =
                averageEncodeMs > framePeriodMs * SaturatedFraction ? 1u : OverloadedWindows;
            if (++overloadedWindows_ >= windowsToStep && state.level < maxLevel_)
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

// The encoded pixel rate (pixels per frame times refresh rate) this Mac's encoder was seen to keep up
// with and the lowest rate it fell behind at, kept across sessions so a new session or a new refresh rate
// starts at a foveation level the encoder can sustain instead of finding it out over several seconds.
class EncoderLimits
{
public:
    double OverloadedPixelRate() const { return overloadedPixelRate_; }
    double SustainedPixelRate() const { return sustainedPixelRate_; }

    bool Allows(double pixelRate) const { return overloadedPixelRate_ <= 0.0 || pixelRate < overloadedPixelRate_; }

    // Returns true when the limits changed.
    bool RecordOverloaded(double pixelRate)
    {
        if (overloadedPixelRate_ > 0.0 && pixelRate >= overloadedPixelRate_)
        {
            return false;
        }
        overloadedPixelRate_ = pixelRate;
        sustainedPixelRate_ = std::min(sustainedPixelRate_, pixelRate);
        return true;
    }

    // Returns true when the limits changed. A rate that keeps up raises a stale overload limit with it,
    // so a slow moment (thermal throttling, another encode) is not remembered forever.
    bool RecordSustained(double pixelRate)
    {
        if (pixelRate <= sustainedPixelRate_)
        {
            return false;
        }
        sustainedPixelRate_ = pixelRate;
        if (overloadedPixelRate_ > 0.0 && overloadedPixelRate_ <= pixelRate)
        {
            overloadedPixelRate_ = 0.0;
        }
        return true;
    }

    // One line per encoder ("<encoder> <overloaded pixel rate> <sustained pixel rate>").
    void Load(const std::string& path, const std::string& encoder)
    {
        std::ifstream file(path);
        std::string name;
        double overloaded = 0.0;
        double sustained = 0.0;
        while (file >> name >> overloaded >> sustained)
        {
            if (name == encoder)
            {
                overloadedPixelRate_ = std::max(overloaded, 0.0);
                sustainedPixelRate_ = std::max(sustained, 0.0);
                return;
            }
        }
    }

    void Save(const std::string& path, const std::string& encoder) const
    {
        std::string kept;
        {
            std::ifstream file(path);
            std::string name;
            double overloaded = 0.0;
            double sustained = 0.0;
            while (file >> name >> overloaded >> sustained)
            {
                if (name != encoder)
                {
                    kept += name + " " + std::to_string(overloaded) + " " + std::to_string(sustained) + "\n";
                }
            }
        }
        std::ofstream file(path, std::ios::trunc);
        file << kept << encoder << " " << overloadedPixelRate_ << " " << sustainedPixelRate_ << "\n";
    }

private:
    double overloadedPixelRate_ = 0.0;
    double sustainedPixelRate_ = 0.0;
};

} // namespace oxrsys::encode_budget
