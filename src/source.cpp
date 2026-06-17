#include "audio-pacing.hpp"
#include "translation-session.hpp"
#include <atomic>
#include <chrono>
#include <obs-module.h>
#include <thread>
#include <util/platform.h>
#include <vector>

namespace {

constexpr uint32_t kOutputSampleRate = 24000;
constexpr size_t kDrainCapBytes = 9600;        // 200 ms of 24 kHz mono S16
constexpr uint32_t kWaitTimeoutMs = 100;
// Scheduling lead cap. Diagnostics showed the model delivers audio every ~250 ms
// and pauses up to ~600 ms at phrase boundaries (never longer). Buffering 600 ms
// ahead lets OBS play through those pauses so live playback stays continuous
// instead of stuttering at every boundary. For one-way live streaming the only
// cost is a little more end-to-end latency, which is irrelevant. (The tail-on-stop
// truncation this enlarges only affects recordings, not live streams.) Bounded,
// so the lead can't grow unboundedly the way the original (uncapped) lead did.
constexpr uint64_t kMaxLeadNs = 600000000ULL;  // 600 ms scheduling lead cap

struct SourceData {
    obs_source_t *context = nullptr;
    std::thread thread;
    std::atomic<bool> active{false};
    lt::OutputTimestamper timestamper{kOutputSampleRate, kMaxLeadNs};
};

const char *source_get_name(void *)
{
    return obs_module_text("Gemini Translated Audio");
}

void push_loop(SourceData *d)
{
    lt::TranslationSession &session = lt::TranslationSession::instance();
    std::vector<uint8_t> buf(kDrainCapBytes);
    while (d->active) {
        if (session.take_interrupted())
            d->timestamper.reset();

        size_t n = session.wait_and_read_output(buf.data(), buf.size(),
                                                kWaitTimeoutMs);
        if (n < sizeof(int16_t))
            continue;
        size_t frames = n / sizeof(int16_t);

        uint64_t now = os_gettime_ns();
        uint64_t ts = d->timestamper.next_timestamp(now, frames);
        // diag: backlog = audio buffered in our ring but not yet pushed; lead =
        // how far ahead of the wall clock this push is scheduled. If backlog
        // grows to seconds, the delay is OUR pipeline holding data, not the
        // model; if it stays small, the lag is upstream (arrival side).
        uint64_t backlog_ms = session.output_backlog_bytes() / 48;
        uint64_t lead_ms = ts > now ? (ts - now) / 1000000ULL : 0;
        blog(LOG_INFO, "[live-translate][out] backlog=%llums lead=%llums",
             (unsigned long long)backlog_ms, (unsigned long long)lead_ms);
        // Pace emission so we never schedule audio further than kMaxLeadNs ahead
        // of the wall clock. Without this the scheduling lead, established during
        // the model's initial burst, never drains (the stream has no gaps) and a
        // recording stop truncates that whole lead off the tail.
        uint64_t delay = d->timestamper.pacing_delay_ns(now);
        if (delay > 0)
            std::this_thread::sleep_for(std::chrono::nanoseconds(delay));

        struct obs_source_audio out = {};
        out.data[0] = buf.data();
        out.frames = static_cast<uint32_t>(frames);
        out.speakers = SPEAKERS_MONO;
        out.format = AUDIO_FORMAT_16BIT;
        out.samples_per_sec = kOutputSampleRate;
        out.timestamp = ts;
        obs_source_output_audio(d->context, &out);
    }
}

void *source_create(obs_data_t *, obs_source_t *source)
{
    auto *d = new SourceData();
    d->context = source;
    d->active = true;
    d->thread = std::thread(push_loop, d);
    return d;
}

void source_destroy(void *data)
{
    auto *d = static_cast<SourceData *>(data);
    d->active = false;
    if (d->thread.joinable()) d->thread.join();
    delete d;
}

} // namespace

struct obs_source_info live_translate_source_info = [] {
    struct obs_source_info info = {};
    info.id = "gemini_translated_audio_source";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = source_get_name;
    info.create = source_create;
    info.destroy = source_destroy;
    return info;
}();
