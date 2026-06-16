#include "translation-session.hpp"
#include "backoff.hpp"
#include "live-protocol.hpp"
#include <ixwebsocket/IXWebSocket.h>
#include <obs.h>
#include <chrono>
#include <vector>

namespace lt {

namespace {
uint64_t now_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
}

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
        blog(LOG_INFO, "[live-translate] API key cleared; stopping session");
        stop();
        return;
    }
    blog(LOG_INFO, "[live-translate] configuring session: target=%s api_key=%s",
         target_lang.c_str(), api_key.empty() ? "empty" : "set");
    config_changed_ = true;
    if (!running_.exchange(true)) {
        // A previous run() may have exited on its own (e.g. auth error) and left
        // thread_ joinable. Join it before reassigning, or the assignment would
        // std::terminate.
        if (thread_.joinable()) thread_.join();
        thread_ = std::thread([this] { run(); });
    }
}

void TranslationSession::stop()
{
    running_.exchange(false);
    // Always join: run() may have already cleared running_ itself (auth error),
    // in which case the thread is still joinable and must be reaped here so the
    // singleton's destruction does not std::terminate.
    if (thread_.joinable()) thread_.join();
    input_.clear();
    output_.clear();
    set_status(ConnStatus::Idle);
}

void TranslationSession::push_input_pcm(const uint8_t *data, size_t len)
{
    last_input_ms_.store(now_ms(), std::memory_order_relaxed);
    input_.write(data, len);
}

size_t TranslationSession::pull_output_pcm(uint8_t *out, size_t len)
{
    return output_.read(out, len);
}

size_t TranslationSession::output_buffered_bytes()
{
    return output_.size();
}

uint64_t TranslationSession::input_idle_ms()
{
    uint64_t last = last_input_ms_.load(std::memory_order_relaxed);
    if (last == 0) return UINT64_MAX;
    uint64_t now = now_ms();
    return now >= last ? now - last : 0;
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
                std::string setup = build_setup_message(lang, false);
                ws.send(setup);
                blog(LOG_INFO, "[live-translate] websocket opened; setup sent");
                set_status(ConnStatus::Connected);
                backoff.reset();
            } else if (msg->type == ix::WebSocketMessageType::Message) {
                ServerMessage m = parse_server_message(msg->str);
                if (m.kind == ServerMessage::Kind::Audio && !m.audio.empty()) {
                    blog(LOG_DEBUG, "[live-translate] received audio bytes: %zu",
                         m.audio.size());
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
                if (msg->type == ix::WebSocketMessageType::Error) {
                    blog(LOG_ERROR, "[live-translate] websocket error: %s",
                         msg->errorInfo.reason.c_str());
                } else {
                    blog(LOG_INFO, "[live-translate] websocket closed: %s",
                         msg->closeInfo.reason.c_str());
                }
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
                ws.send(m);
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
