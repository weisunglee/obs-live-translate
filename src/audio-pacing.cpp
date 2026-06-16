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

OutputTimestamper::OutputTimestamper(uint32_t sample_rate, uint64_t max_lead_ns)
    : sample_rate_(sample_rate), max_lead_ns_(max_lead_ns)
{}

uint64_t OutputTimestamper::next_timestamp(uint64_t now_ns, size_t frames)
{
    uint64_t ts = next_ts_ > now_ns ? next_ts_ : now_ns;
    next_ts_ = ts + static_cast<uint64_t>(frames) * 1000000000ULL / sample_rate_;
    over_lead_ = next_ts_ > now_ns + max_lead_ns_;
    return ts;
}

uint64_t OutputTimestamper::pacing_delay_ns(uint64_t now_ns) const
{
    uint64_t lead = next_ts_ > now_ns ? next_ts_ - now_ns : 0;
    return lead > max_lead_ns_ ? lead - max_lead_ns_ : 0;
}

void OutputTimestamper::reset()
{
    next_ts_ = 0;
    over_lead_ = false;
}

}
