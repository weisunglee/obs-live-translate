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
size_t output_jitter_start_bytes();
size_t output_jitter_min_bytes();

class OutputJitterBuffer {
public:
    OutputJitterBuffer(size_t start_threshold_bytes, size_t min_play_bytes)
        : start_threshold_bytes_(start_threshold_bytes), min_play_bytes_(min_play_bytes)
    {}
    bool should_play(size_t buffered_bytes, size_t packet_bytes);

private:
    size_t start_threshold_bytes_;
    size_t min_play_bytes_;
    bool active_ = false;
};

class PcmS16MonoSmoother {
public:
    explicit PcmS16MonoSmoother(size_t fade_frames) : fade_frames_(fade_frames) {}
    void apply(int16_t *samples, size_t frames, bool has_audio);

private:
    size_t fade_frames_;
    bool previous_had_audio_ = false;
    int16_t last_sample_ = 0;
};

}
