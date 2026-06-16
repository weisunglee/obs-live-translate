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
size_t output_jitter_grace_ms();

enum class OutputPlaybackAction {
    Silence,
    PlayAudio,
    DrainPartial,
    Hold,
};

class OutputJitterBuffer {
public:
    OutputJitterBuffer(size_t start_threshold_bytes, size_t grace_ms);
    OutputPlaybackAction next_action(size_t buffered_bytes, size_t packet_bytes,
                                     uint32_t elapsed_ms,
                                     bool input_idle_flush);

private:
    enum class State {
        Priming,
        Playing,
        Grace,
    };

    size_t start_threshold_bytes_;
    size_t grace_ms_;
    State state_ = State::Priming;
    uint32_t grace_elapsed_ms_ = 0;
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
