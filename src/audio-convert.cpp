#include "audio-convert.hpp"
#include <algorithm>
#include <cmath>

namespace lt {

std::vector<float> downmix_to_mono(const float *const *planes,
                                   size_t channels, size_t frames)
{
    std::vector<float> mono(frames, 0.0f);
    if (channels == 0) return mono;
    for (size_t ch = 0; ch < channels; ++ch)
        for (size_t i = 0; i < frames; ++i)
            mono[i] += planes[ch][i];
    const float inv = 1.0f / static_cast<float>(channels);
    for (float &s : mono) s *= inv;
    return mono;
}

std::vector<uint8_t> float_to_s16le(const float *samples, size_t count)
{
    std::vector<uint8_t> out(count * 2);
    for (size_t i = 0; i < count; ++i) {
        float v = std::max(-1.0f, std::min(1.0f, samples[i]));
        int32_t s = static_cast<int32_t>(std::lround(v * 32767.0f));
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[i * 2] = static_cast<uint8_t>(s & 0xFF);
        out[i * 2 + 1] = static_cast<uint8_t>((s >> 8) & 0xFF);
    }
    return out;
}

double s16le_rms(const uint8_t *pcm, size_t len)
{
    size_t samples = len / 2;
    if (samples == 0) return 0.0;

    double sum_sq = 0.0;
    for (size_t i = 0; i < samples; ++i) {
        int16_t sample = static_cast<int16_t>(pcm[i * 2] | (pcm[i * 2 + 1] << 8));
        double v = static_cast<double>(sample);
        sum_sq += v * v;
    }
    return std::sqrt(sum_sq / static_cast<double>(samples));
}

bool s16le_has_signal(const uint8_t *pcm, size_t len, double threshold)
{
    return s16le_rms(pcm, len) >= threshold;
}

size_t voice_gate_tail_chunks(uint32_t tail_ms, uint32_t chunk_ms)
{
    if (chunk_ms == 0) return 0;
    return (tail_ms + chunk_ms - 1) / chunk_ms;
}

bool VoiceGate::should_send(bool has_signal)
{
    if (has_signal) {
        remaining_tail_chunks_ = tail_chunks_;
        return true;
    }
    if (remaining_tail_chunks_ > 0) {
        --remaining_tail_chunks_;
        return true;
    }
    return false;
}

std::vector<std::vector<uint8_t>> Chunker::push(const uint8_t *data, size_t len)
{
    acc_.insert(acc_.end(), data, data + len);
    std::vector<std::vector<uint8_t>> chunks;
    size_t off = 0;
    while (acc_.size() - off >= chunk_bytes_) {
        chunks.emplace_back(acc_.begin() + off, acc_.begin() + off + chunk_bytes_);
        off += chunk_bytes_;
    }
    if (off > 0) acc_.erase(acc_.begin(), acc_.begin() + off);
    return chunks;
}

}
