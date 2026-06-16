#include "audio-pacing.hpp"
#include "translation-session.hpp"
#include <atomic>
#include <chrono>
#include <obs-module.h>
#include <thread>
#include <util/platform.h>
#include <vector>
#include <cstring>

namespace {

struct SourceData {
    obs_source_t *context = nullptr;
    std::thread thread;
    std::atomic<bool> active{false};
    lt::OutputJitterBuffer jitter{lt::audio_packet_shape(24000, 16, 1, 500).bytes,
                                  lt::audio_packet_shape(24000, 16, 1, 150).bytes};
};

const char *source_get_name(void *)
{
    return obs_module_text("Gemini Translated Audio");
}

void emit_loop(SourceData *d)
{
    const lt::AudioPacketShape packet = lt::audio_packet_shape(24000, 16, 1, 20);
    const uint64_t packet_duration_ns =
        lt::audio_packet_duration_ns(packet.frames, 24000);
    uint64_t timestamp = os_gettime_ns();
    std::vector<uint8_t> buf(packet.bytes);
    while (d->active) {
        std::memset(buf.data(), 0, buf.size());
        lt::TranslationSession &session = lt::TranslationSession::instance();
        if (d->jitter.should_play(session.output_buffered_bytes(), buf.size()))
            session.pull_output_pcm(buf.data(), buf.size());

        struct obs_source_audio out = {};
        out.data[0] = buf.data();
        out.frames = static_cast<uint32_t>(packet.frames);
        out.speakers = SPEAKERS_MONO;
        out.format = AUDIO_FORMAT_16BIT;
        out.samples_per_sec = 24000;
        out.timestamp = timestamp;
        obs_source_output_audio(d->context, &out);

        timestamp += packet_duration_ns;
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
