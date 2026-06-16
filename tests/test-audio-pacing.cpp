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

TEST_CASE("default output jitter thresholds favor smooth translated speech")
{
    REQUIRE(output_jitter_start_bytes() == audio_packet_shape(24000, 16, 1, 1200).bytes);
    REQUIRE(output_jitter_grace_ms() == 500);
}

TEST_CASE("output jitter waits for startup threshold before playback")
{
    size_t start_threshold = output_jitter_start_bytes();
    OutputJitterBuffer jitter(start_threshold, output_jitter_grace_ms());
    REQUIRE(jitter.next_action(start_threshold - 1, 960, 20, false) ==
            OutputPlaybackAction::Silence);
    REQUIRE(jitter.next_action(start_threshold, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
}

TEST_CASE("output jitter keeps playing below startup threshold after start")
{
    size_t start_threshold = output_jitter_start_bytes();
    OutputJitterBuffer jitter(start_threshold, output_jitter_grace_ms());
    REQUIRE(jitter.next_action(start_threshold, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
    REQUIRE(jitter.next_action(audio_packet_shape(24000, 16, 1, 200).bytes, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
}

TEST_CASE("output jitter enters grace instead of stopping immediately")
{
    size_t start_threshold = output_jitter_start_bytes();
    OutputJitterBuffer jitter(start_threshold, output_jitter_grace_ms());
    REQUIRE(jitter.next_action(start_threshold, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
    REQUIRE(jitter.next_action(0, 960, 20, false) ==
            OutputPlaybackAction::Hold);
    REQUIRE(jitter.next_action(0, 960, 480, false) ==
            OutputPlaybackAction::Hold);
    REQUIRE(jitter.next_action(0, 960, 20, false) ==
            OutputPlaybackAction::Silence);
}

TEST_CASE("output jitter resumes playback when audio arrives during grace")
{
    size_t start_threshold = output_jitter_start_bytes();
    OutputJitterBuffer jitter(start_threshold, output_jitter_grace_ms());
    REQUIRE(jitter.next_action(start_threshold, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
    REQUIRE(jitter.next_action(0, 960, 20, false) ==
            OutputPlaybackAction::Hold);
    REQUIRE(jitter.next_action(960, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
    REQUIRE(jitter.next_action(960, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
}

TEST_CASE("output jitter can drain a tail after input idle while priming")
{
    size_t start_threshold = output_jitter_start_bytes();
    OutputJitterBuffer jitter(start_threshold, output_jitter_grace_ms());
    REQUIRE(jitter.next_action(960, 960, 20, false) ==
            OutputPlaybackAction::Silence);
    REQUIRE(jitter.next_action(960, 960, 20, true) ==
            OutputPlaybackAction::PlayAudio);
}

TEST_CASE("pcm smoother fades in when audio resumes")
{
    PcmS16MonoSmoother smoother(4);
    int16_t silence[4] = {};
    smoother.apply(silence, 4, false);

    int16_t audio[4] = {1000, 1000, 1000, 1000};
    smoother.apply(audio, 4, true);
    REQUIRE(audio[0] == 0);
    REQUIRE(audio[1] == 250);
    REQUIRE(audio[2] == 500);
    REQUIRE(audio[3] == 750);
}

TEST_CASE("pcm smoother fades out on underrun instead of hard cutting")
{
    PcmS16MonoSmoother smoother(4);
    int16_t audio[4] = {1000, 1000, 1000, 1000};
    smoother.apply(audio, 4, true);
    int16_t steady_audio[4] = {1000, 1000, 1000, 1000};
    smoother.apply(steady_audio, 4, true);

    int16_t silence[4] = {};
    smoother.apply(silence, 4, false);
    REQUIRE(silence[0] == 1000);
    REQUIRE(silence[1] == 750);
    REQUIRE(silence[2] == 500);
    REQUIRE(silence[3] == 250);
}
