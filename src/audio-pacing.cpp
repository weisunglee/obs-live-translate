#include "audio-pacing.hpp"

namespace lt {

AudioPacketShape audio_packet_shape(uint32_t sample_rate, uint16_t bits_per_sample,
                                    uint16_t channels, uint32_t interval_ms)
{
    AudioPacketShape shape{};
    shape.frames = static_cast<size_t>(sample_rate) * interval_ms / 1000;
    shape.bytes = shape.frames * channels * bits_per_sample / 8;
    return shape;
}

uint64_t audio_packet_duration_ns(size_t frames, uint32_t sample_rate)
{
    return static_cast<uint64_t>(frames) * 1000000000ULL / sample_rate;
}

size_t output_jitter_start_bytes()
{
    return audio_packet_shape(24000, 16, 1, 750).bytes;
}

size_t output_jitter_min_bytes()
{
    return audio_packet_shape(24000, 16, 1, 300).bytes;
}

bool OutputJitterBuffer::should_play(size_t buffered_bytes, size_t packet_bytes)
{
    if (!active_) {
        active_ = buffered_bytes >= start_threshold_bytes_;
    }
    const size_t minimum = min_play_bytes_ > packet_bytes ? min_play_bytes_ : packet_bytes;
    if (active_ && buffered_bytes < minimum) {
        active_ = false;
    }
    return active_;
}

}
