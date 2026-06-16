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

TEST_CASE("default output jitter thresholds favor stable recording")
{
    REQUIRE(output_jitter_start_bytes() == audio_packet_shape(24000, 16, 1, 750).bytes);
    REQUIRE(output_jitter_min_bytes() == audio_packet_shape(24000, 16, 1, 300).bytes);
}

TEST_CASE("output jitter buffer waits for threshold before playback")
{
    size_t start_threshold = output_jitter_start_bytes();
    size_t min_play = output_jitter_min_bytes();
    OutputJitterBuffer jitter(start_threshold, min_play);
    REQUIRE_FALSE(jitter.should_play(start_threshold - 1, 960));
    REQUIRE(jitter.should_play(start_threshold, 960));
    REQUIRE(jitter.should_play(min_play, 960));
}

TEST_CASE("output jitter buffer tolerates low buffer until minimum")
{
    size_t start_threshold = output_jitter_start_bytes();
    size_t min_play = output_jitter_min_bytes();
    OutputJitterBuffer jitter(start_threshold, min_play);
    REQUIRE(jitter.should_play(start_threshold, 960));
    REQUIRE(jitter.should_play(min_play, 960));
    REQUIRE_FALSE(jitter.should_play(min_play - 1, 960));
    REQUIRE_FALSE(jitter.should_play(start_threshold - 1, 960));
    REQUIRE(jitter.should_play(start_threshold, 960));
}
