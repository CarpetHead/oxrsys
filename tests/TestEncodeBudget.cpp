// SPDX-License-Identifier: MPL-2.0

#include <catch2/catch_test_macros.hpp>

#include "EncodeBudget.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace oxrsys::encode_budget;

namespace
{
constexpr double FramePeriodMs = 13.9;

uint32_t RunWindows(Controller& controller, StreamMode mode, double encodeMs, uint32_t windows)
{
    uint32_t level = controller.Level(mode);
    for (uint32_t i = 0; i < windows; ++i)
    {
        level = controller.Update(mode, encodeMs, FramePeriodMs);
    }
    return level;
}
} // namespace

TEST_CASE("Encode budget steps to stronger foveation after consecutive overloaded windows", "[streaming][encode_budget]")
{
    Controller controller(3);

    CHECK(controller.Update(StreamMode::Color, 20.0, FramePeriodMs) == 0);
    CHECK(controller.Update(StreamMode::Color, 20.0, FramePeriodMs) == 0);
    CHECK(controller.Update(StreamMode::Color, 20.0, FramePeriodMs) == 1);
    CHECK(RunWindows(controller, StreamMode::Color, 70.0, 10) == 3);
}

TEST_CASE("Encode budget ignores a single overloaded window", "[streaming][encode_budget]")
{
    Controller controller(3);

    CHECK(controller.Update(StreamMode::Color, 20.0, FramePeriodMs) == 0);
    CHECK(controller.Update(StreamMode::Color, 12.0, FramePeriodMs) == 0);
    CHECK(controller.Update(StreamMode::Color, 20.0, FramePeriodMs) == 0);
}

TEST_CASE("Encode budget returns towards configured foveation but never to a level that was too slow", "[streaming][encode_budget]")
{
    Controller controller(4);
    controller.SetLevel(StreamMode::PackedAlpha, 3);

    CHECK(RunWindows(controller, StreamMode::PackedAlpha, 5.0, Controller::HeadroomWindows) == 2);
    CHECK(RunWindows(controller, StreamMode::PackedAlpha, 20.0, Controller::OverloadedWindows) == 3);
    CHECK(RunWindows(controller, StreamMode::PackedAlpha, 5.0, Controller::HeadroomWindows * 4) == 3);
}

TEST_CASE("Encode budget holds a level whose encode time is within the period", "[streaming][encode_budget]")
{
    Controller controller(3);
    controller.SetLevel(StreamMode::Color, 1);

    CHECK(RunWindows(controller, StreamMode::Color, 12.5, 20) == 1);
}

TEST_CASE("Encode budget rides out an app that briefly slows encoding", "[streaming][encode_budget]")
{
    Controller controller(3);

    CHECK(RunWindows(controller, StreamMode::Color, 15.5, 20) == 0);
    CHECK(controller.Update(StreamMode::Color, 25.0, FramePeriodMs) == 0);
    CHECK(controller.Update(StreamMode::Color, 25.0, FramePeriodMs) == 0);
    CHECK(controller.Update(StreamMode::Color, 12.0, FramePeriodMs) == 0);
    CHECK(controller.Update(StreamMode::Color, 25.0, FramePeriodMs) == 0);
}

TEST_CASE("Encode budget keeps a level per stream mode", "[streaming][encode_budget]")
{
    Controller controller(3);
    controller.SetLevel(StreamMode::PackedAlpha, 2);

    CHECK(controller.HasLevel(StreamMode::PackedAlpha));
    CHECK_FALSE(controller.HasLevel(StreamMode::Color));
    CHECK(controller.Level(StreamMode::Color) == 0);
    CHECK(controller.Level(StreamMode::PackedAlpha) == 2);

    controller.Reset();
    CHECK_FALSE(controller.HasLevel(StreamMode::PackedAlpha));
}

TEST_CASE("Encode budget steps after one window when the encoder is saturated", "[streaming][encode_budget]")
{
    Controller controller(3);

    CHECK(controller.Update(StreamMode::Color, FramePeriodMs * 2.5, FramePeriodMs) == 1);
    CHECK(controller.Update(StreamMode::Color, FramePeriodMs * 1.5, FramePeriodMs) == 1);
}

TEST_CASE("Encoder limits allow rates below the lowest overloaded rate", "[streaming][encode_budget]")
{
    EncoderLimits limits;
    CHECK(limits.Allows(300.0e6));

    CHECK(limits.RecordOverloaded(295.0e6));
    CHECK_FALSE(limits.RecordOverloaded(300.0e6));
    CHECK(limits.RecordOverloaded(232.0e6));
    CHECK_FALSE(limits.Allows(232.0e6));
    CHECK(limits.Allows(186.0e6));

    CHECK(limits.RecordSustained(186.0e6));
    CHECK_FALSE(limits.RecordSustained(150.0e6));
    CHECK(limits.SustainedPixelRate() == 186.0e6);
}

TEST_CASE("Encoder limits forget an overload that a faster rate later sustained", "[streaming][encode_budget]")
{
    EncoderLimits limits;
    limits.RecordOverloaded(200.0e6);
    limits.RecordSustained(210.0e6);

    CHECK(limits.OverloadedPixelRate() == 0.0);
    CHECK(limits.Allows(250.0e6));
}

TEST_CASE("Encoder limits persist per encoder", "[streaming][encode_budget]")
{
    const std::string path = std::string(std::getenv("TMPDIR") != nullptr ? std::getenv("TMPDIR") : "/tmp") +
                             "/oxrsys-encoder-limits-test.txt";
    std::remove(path.c_str());

    EncoderLimits hevc;
    hevc.RecordOverloaded(232.0e6);
    hevc.RecordSustained(186.0e6);
    hevc.Save(path, "h265-low-latency");
    EncoderLimits avc;
    avc.RecordOverloaded(400.0e6);
    avc.Save(path, "h264-low-latency");

    EncoderLimits loaded;
    loaded.Load(path, "h265-low-latency");
    CHECK(loaded.OverloadedPixelRate() == 232.0e6);
    CHECK(loaded.SustainedPixelRate() == 186.0e6);

    EncoderLimits unknown;
    unknown.Load(path, "h265");
    CHECK(unknown.Allows(1.0e9));
    std::remove(path.c_str());
}
