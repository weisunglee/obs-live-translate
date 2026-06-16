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

class OutputJitterBuffer {
public:
    explicit OutputJitterBuffer(size_t start_threshold_bytes)
        : start_threshold_bytes_(start_threshold_bytes)
    {}
    bool should_play(size_t buffered_bytes, size_t packet_bytes);

private:
    size_t start_threshold_bytes_;
    bool active_ = false;
};

}
