#pragma once
#include <cstdint>

namespace lt {

class Backoff {
public:
    Backoff(uint32_t base_ms, uint32_t max_ms)
        : base_ms_(base_ms), max_ms_(max_ms), current_(base_ms)
    {
    }

    uint32_t next_ms()
    {
        uint32_t value = current_;
        uint64_t doubled = static_cast<uint64_t>(current_) * 2;
        current_ = doubled > max_ms_ ? max_ms_ : static_cast<uint32_t>(doubled);
        return value > max_ms_ ? max_ms_ : value;
    }

    void reset() { current_ = base_ms_; }

private:
    uint32_t base_ms_;
    uint32_t max_ms_;
    uint32_t current_;
};

}
