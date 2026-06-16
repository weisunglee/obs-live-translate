#include <catch2/catch_test_macros.hpp>
#include "audio-pacing.hpp"

using namespace lt;

TEST_CASE("audio pacing calculates bytes and frames for a fixed interval")
{
    AudioPacketShape shape = audio_packet_shape(24000, 16, 1, 20);
    REQUIRE(shape.frames == 480);
    REQUIRE(shape.bytes == 960);
}

TEST_CASE("audio pacing calculates packet duration in nanoseconds")
{
    REQUIRE(audio_packet_duration_ns(480, 24000) == 20000000ULL);
}

TEST_CASE("output jitter buffer waits for threshold before playback")
{
    OutputJitterBuffer jitter(24000);
    REQUIRE_FALSE(jitter.should_play(23999, 960));
    REQUIRE(jitter.should_play(24000, 960));
    REQUIRE(jitter.should_play(12000, 960));
}

TEST_CASE("output jitter buffer pauses on underrun and waits to refill")
{
    OutputJitterBuffer jitter(24000);
    REQUIRE(jitter.should_play(24000, 960));
    REQUIRE_FALSE(jitter.should_play(959, 960));
    REQUIRE_FALSE(jitter.should_play(12000, 960));
    REQUIRE(jitter.should_play(24000, 960));
}
