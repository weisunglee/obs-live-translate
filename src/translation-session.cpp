#include "translation-session.hpp"
#include "backoff.hpp"
#include "live-protocol.hpp"
#include <ixwebsocket/IXWebSocket.h>
#include <obs.h>
#include <chrono>
#include <vector>

namespace lt {

TranslationSession &TranslationSession::instance()
{
    static TranslationSession s;
    return s;
}

TranslationSession::TranslationSession() = default;

TranslationSession::~TranslationSession() { stop(); }

void TranslationSession::set_status(ConnStatus s, const std::string &detail)
{
    std::lock_guard<std::mutex> lk(status_mtx_);
    status_ = s;
    status_detail_ = detail;
}

ConnStatus TranslationSession::status()
{
    std::lock_guard<std::mutex> lk(status_mtx_);
    return status_;
}

std::string TranslationSession::status_text()
{
    std::lock_guard<std::mutex> lk(status_mtx_);
    switch (status_) {
    case ConnStatus::Idle:
        return "Idle";
    case ConnStatus::Connecting:
        return "Connecting...";
    case ConnStatus::Connected:
        return "Connected";
    case ConnStatus::Reconnecting:
        return "Reconnecting...";
    case ConnStatus::AuthError:
        return "API key error: " + status_detail_;
    }
    return "Unknown";
}

void TranslationSession::configure(const std::string &api_key,
                                   const std::string &target_lang)
{
    {
        std::lock_guard<std::mutex> lk(cfg_mtx_);
        api_key_ = api_key;
        target_lang_ = target_lang;
    }
    if (api_key.empty()) {
        stop();
        return;
    }
    config_changed_ = true;
    if (!running_.exchange(true)) {
        thread_ = std::thread([this] { run(); });
    }
}

void TranslationSession::stop()
{
    if (running_.exchange(false)) {
        if (thread_.joinable()) thread_.join();
    }
    input_.clear();
    output_.clear();
    set_status(ConnStatus::Idle);
}

void TranslationSession::push_input_pcm(const uint8_t *data, size_t len)
{
    input_.write(data, len);
}

size_t TranslationSession::pull_output_pcm(uint8_t *out, size_t len)
{
    return output_.read(out, len);
}

void TranslationSession::run()
{
    Backoff backoff(1000, 30000);

    while (running_) {
        std::string key, lang;
        {
            std::lock_guard<std::mutex> lk(cfg_mtx_);
            key = api_key_;
            lang = target_lang_;
        }
        config_changed_ = false;

        ix::WebSocket ws;
        std::string url =
            "wss://generativelanguage.googleapis.com/ws/"
            "google.ai.generativelanguage.v1beta.GenerativeService."
            "BidiGenerateContent?key=" + key;
        ws.setUrl(url);

        std::atomic<bool> auth_error{false};
        std::atomic<bool> open{false};

        ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr &msg) {
            if (msg->type == ix::WebSocketMessageType::Open) {
                open = true;
                std::string setup = build_setup_message(lang, true);
                ws.sendBinary(setup);
                set_status(ConnStatus::Connected);
                backoff.reset();
            } else if (msg->type == ix::WebSocketMessageType::Message) {
                ServerMessage m = parse_server_message(msg->str);
                if (m.kind == ServerMessage::Kind::Audio && !m.audio.empty()) {
                    output_.write(m.audio.data(), m.audio.size());
                } else if (m.kind == ServerMessage::Kind::Error) {
                    blog(LOG_ERROR, "[live-translate] server error: %s",
                         m.error_message.c_str());
                    set_status(ConnStatus::AuthError, m.error_message);
                    auth_error = true;
                    ws.stop();
                }
            } else if (msg->type == ix::WebSocketMessageType::Close ||
                       msg->type == ix::WebSocketMessageType::Error) {
                open = false;
            }
        });

        set_status(ConnStatus::Connecting);
        ws.start();

        std::vector<uint8_t> chunk(3200);
        while (running_ && !auth_error && !config_changed_) {
            size_t n = input_.read(chunk.data(), chunk.size());
            if (n == chunk.size() && open) {
                std::string m =
                    build_realtime_input_message(chunk.data(), chunk.size());
                ws.sendBinary(m);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        ws.stop();

        if (auth_error) {
            running_ = false;
            break;
        }
        if (config_changed_) continue;
        if (!running_) break;

        set_status(ConnStatus::Reconnecting);
        uint32_t wait = backoff.next_ms();
        for (uint32_t waited = 0; waited < wait && running_ && !config_changed_;
             waited += 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    set_status(status() == ConnStatus::AuthError ? ConnStatus::AuthError
                                                 : ConnStatus::Idle);
}

}
