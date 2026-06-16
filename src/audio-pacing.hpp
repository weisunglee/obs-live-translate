#pragma once
#include <cstddef>
#include <cstdint>

namespace lt {

struct AudioPacketShape {
    size_t frames;
    size_t bytes;
};

AudioPacketShape audio_packet_shape(uint32_t sample_rate, uint16_t bits_per_sample,
                                    uint16_t channels, uint32_t interval_ms);
uint64_t audio_packet_duration_ns(size_t frames, uint32_t sample_rate);

// Produces contiguous, duration-spaced timestamps for pushed PCM so the OBS
// mixer schedules playback. Clamps forward to the wall clock after a gap and
// resets on interrupt.
class OutputTimestamper {
public:
    OutputTimestamper(uint32_t sample_rate, uint64_t max_lead_ns);
    uint64_t next_timestamp(uint64_t now_ns, size_t frames);
    // Nanoseconds to wait before emitting so the furthest-scheduled audio
    // stays within max_lead_ns of the wall clock (bounds OBS scheduling lead,
    // so a recording stop truncates at most max_lead_ns of the tail).
    uint64_t pacing_delay_ns(uint64_t now_ns) const;
    void reset();
    bool over_lead() const { return over_lead_; }

private:
    uint32_t sample_rate_;
    uint64_t max_lead_ns_;
    uint64_t next_ts_ = 0;
    bool over_lead_ = false;
};

}
