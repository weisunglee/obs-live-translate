#pragma once
#include "ring-buffer.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace lt {

enum class ConnStatus { Idle, Connecting, Connected, Reconnecting, AuthError };

class TranslationSession {
public:
    static TranslationSession &instance();

    void configure(const std::string &api_key, const std::string &target_lang);
    void stop();

    void push_input_pcm(const uint8_t *data, size_t len);
    size_t pull_output_pcm(uint8_t *out, size_t len);
    size_t output_buffered_bytes();
    uint64_t input_idle_ms();

    ConnStatus status();
    std::string status_text();

private:
    TranslationSession();
    ~TranslationSession();
    void run();
    void set_status(ConnStatus s, const std::string &detail = "");

    ByteRingBuffer input_{16000 * 2 * 5};
    ByteRingBuffer output_{24000 * 2 * 10};
    std::atomic<uint64_t> last_input_ms_{0};

    std::mutex cfg_mtx_;
    std::string api_key_;
    std::string target_lang_;

    std::atomic<bool> running_{false};
    std::atomic<bool> config_changed_{false};
    std::thread thread_;

    std::mutex status_mtx_;
    ConnStatus status_ = ConnStatus::Idle;
    std::string status_detail_;
};

}
