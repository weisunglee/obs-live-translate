#pragma once
#include "ring-buffer.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace lt {

enum class ConnStatus { Idle, Connecting, Connected, Reconnecting, AuthError };

class TranslationSession {
public:
    static TranslationSession &instance();

    void configure(const std::string &api_key, const std::string &target_lang,
                   bool echo_target);
    void stop();

    void push_input_pcm(const uint8_t *data, size_t len);
    void append_output(const uint8_t *data, size_t len);
    void signal_interrupt();
    size_t wait_and_read_output(uint8_t *out, size_t max_len, uint32_t timeout_ms);
    size_t output_backlog_bytes();  // diag: bytes buffered but not yet pushed
    bool take_interrupted();
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
    std::condition_variable output_cv_;
    std::mutex output_wait_mtx_;
    std::atomic<bool> interrupted_{false};
    std::atomic<uint64_t> last_input_ms_{0};
    std::atomic<uint64_t> last_audio_ms_{0};  // diag: translated-audio arrival

    std::mutex cfg_mtx_;
    std::string api_key_;
    std::string target_lang_;
    bool echo_target_ = true;

    std::atomic<bool> running_{false};
    std::atomic<bool> config_changed_{false};
    std::thread thread_;

    std::mutex status_mtx_;
    ConnStatus status_ = ConnStatus::Idle;
    std::string status_detail_;
};

}
