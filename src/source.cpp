#include "translation-session.hpp"
#include <atomic>
#include <chrono>
#include <obs-module.h>
#include <thread>
#include <util/platform.h>
#include <vector>

namespace {

struct SourceData {
    obs_source_t *context = nullptr;
    std::thread thread;
    std::atomic<bool> active{false};
};

const char *source_get_name(void *)
{
    return obs_module_text("Gemini Translated Audio");
}

void emit_loop(SourceData *d)
{
    const size_t kFrames = 2400;
    const size_t kBytes = kFrames * 2;
    std::vector<uint8_t> buf(kBytes);
    while (d->active) {
        size_t n = lt::TranslationSession::instance().pull_output_pcm(
            buf.data(), buf.size());
        if (n >= 2) {
            struct obs_source_audio out = {};
            out.data[0] = buf.data();
            out.frames = static_cast<uint32_t>(n / 2);
            out.speakers = SPEAKERS_MONO;
            out.format = AUDIO_FORMAT_16BIT;
            out.samples_per_sec = 24000;
            out.timestamp = os_gettime_ns();
            obs_source_output_audio(d->context, &out);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void *source_create(obs_data_t *, obs_source_t *source)
{
    auto *d = new SourceData();
    d->context = source;
    d->active = true;
    d->thread = std::thread(emit_loop, d);
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
