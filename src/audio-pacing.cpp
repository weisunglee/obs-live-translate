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
    return audio_packet_shape(24000, 16, 1, 20).bytes;
}

bool OutputJitterBuffer::should_play(size_t buffered_bytes, size_t packet_bytes)
{
    if (!active_) {
        if (buffered_bytes >= start_threshold_bytes_) {
            active_ = true;
            pending_ticks_ = 0;
        } else if (buffered_bytes >= packet_bytes) {
            ++pending_ticks_;
            active_ = pending_ticks_ >= 50;
        } else {
            pending_ticks_ = 0;
        }
    }
    const size_t minimum = min_play_bytes_ > packet_bytes ? min_play_bytes_ : packet_bytes;
    if (active_ && buffered_bytes < minimum) {
        active_ = false;
        pending_ticks_ = 0;
    }
    return active_;
}

void PcmS16MonoSmoother::apply(int16_t *samples, size_t frames, bool has_audio)
{
    const size_t fade = fade_frames_ < frames ? fade_frames_ : frames;
    if (fade == 0) {
        previous_had_audio_ = has_audio;
        return;
    }

    if (has_audio && !previous_had_audio_) {
        for (size_t i = 0; i < fade; ++i)
            samples[i] = static_cast<int16_t>(
                static_cast<int32_t>(samples[i]) * static_cast<int32_t>(i) /
                static_cast<int32_t>(fade));
    } else if (!has_audio && previous_had_audio_) {
        for (size_t i = 0; i < fade; ++i) {
            size_t remaining = fade - i;
            samples[i] = static_cast<int16_t>(
                static_cast<int32_t>(last_sample_) *
                static_cast<int32_t>(remaining) / static_cast<int32_t>(fade));
        }
    }

    last_sample_ = has_audio && frames > 0 ? samples[frames - 1] : 0;
    previous_had_audio_ = has_audio;
}

}
