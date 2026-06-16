#include "audio-pacing.hpp"
#include "translation-session.hpp"
#include <atomic>
#include <obs-module.h>
#include <thread>
#include <util/platform.h>
#include <vector>

namespace {

constexpr uint32_t kOutputSampleRate = 24000;
constexpr size_t kDrainCapBytes = 9600;        // 200 ms of 24 kHz mono S16
constexpr uint32_t kWaitTimeoutMs = 100;
constexpr uint64_t kMaxLeadNs = 2000000000ULL; // 2 s

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

        uint64_t ts = d->timestamper.next_timestamp(os_gettime_ns(), frames);
        if (d->timestamper.over_lead())
            blog(LOG_WARNING,
                 "[live-translate] translated audio running ahead of clock; "
                 "OBS may drop samples");

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
