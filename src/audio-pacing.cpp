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

}
