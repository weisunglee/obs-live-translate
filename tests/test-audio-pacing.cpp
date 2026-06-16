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

TEST_CASE("timestamper stamps the first buffer at the current clock")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    REQUIRE(ts.next_timestamp(1000000000ULL, 480) == 1000000000ULL);
}

TEST_CASE("timestamper keeps timestamps contiguous within a burst")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(1000000000ULL, 480);
    REQUIRE(ts.next_timestamp(1005000000ULL, 480) == 1020000000ULL);
}

TEST_CASE("timestamper clamps forward to the clock after a gap")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(1000000000ULL, 480);
    REQUIRE(ts.next_timestamp(5000000000ULL, 480) == 5000000000ULL);
}

TEST_CASE("timestamper reset restarts at the clock")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(1000000000ULL, 480);
    ts.reset();
    REQUIRE(ts.next_timestamp(3000000000ULL, 480) == 3000000000ULL);
}

TEST_CASE("timestamper flags excessive lead over the clock")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(0, 72000);
    REQUIRE(ts.over_lead());
}

TEST_CASE("timestamper does not flag lead within the guard")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(0, 480);
    REQUIRE_FALSE(ts.over_lead());
}

TEST_CASE("pacing delay is zero when scheduled audio is within target lead")
{
    OutputTimestamper ts(24000, 200000000ULL); // 200 ms target
    ts.next_timestamp(1000000000ULL, 4800);    // 200 ms audio -> next_ts 1.2e9
    // furthest scheduled = 1.2e9, now = 1.0e9, lead = 200 ms == target -> no wait
    REQUIRE(ts.pacing_delay_ns(1000000000ULL) == 0);
}

TEST_CASE("pacing delay caps scheduling lead at the target")
{
    OutputTimestamper ts(24000, 200000000ULL);
    ts.next_timestamp(1000000000ULL, 4800); // next_ts 1.2e9
    ts.next_timestamp(1000000000ULL, 4800); // burst, same clock -> next_ts 1.4e9
    // furthest scheduled = 1.4e9, now = 1.0e9, lead = 400 ms, target 200 ms -> wait 200 ms
    REQUIRE(ts.pacing_delay_ns(1000000000ULL) == 200000000ULL);
}

TEST_CASE("pacing delay is zero once the clock catches up")
{
    OutputTimestamper ts(24000, 200000000ULL);
    ts.next_timestamp(1000000000ULL, 4800); // next_ts 1.2e9
    // clock advanced to the scheduled end -> no lead -> no wait
    REQUIRE(ts.pacing_delay_ns(1200000000ULL) == 0);
}
