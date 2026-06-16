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
    return audio_packet_shape(24000, 16, 1, 1200).bytes;
}

size_t output_jitter_grace_ms()
{
    return 500;
}

OutputJitterBuffer::OutputJitterBuffer(size_t start_threshold_bytes,
                                       size_t grace_ms)
    : start_threshold_bytes_(start_threshold_bytes), grace_ms_(grace_ms)
{}

OutputPlaybackAction OutputJitterBuffer::next_action(size_t buffered_bytes,
                                                     size_t packet_bytes,
                                                     uint32_t elapsed_ms,
                                                     bool input_idle_flush)
{
    if (buffered_bytes >= packet_bytes) {
        if (state_ == State::Priming &&
            buffered_bytes < start_threshold_bytes_ && !input_idle_flush) {
            return OutputPlaybackAction::Silence;
        }
        state_ = State::Playing;
        grace_elapsed_ms_ = 0;
        return OutputPlaybackAction::PlayAudio;
    }

    if (state_ == State::Playing || state_ == State::Grace) {
        state_ = State::Grace;
        grace_elapsed_ms_ += elapsed_ms;
        if (grace_elapsed_ms_ <= grace_ms_)
            return OutputPlaybackAction::Hold;
    }

    state_ = State::Priming;
    grace_elapsed_ms_ = 0;
    return OutputPlaybackAction::Silence;
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
