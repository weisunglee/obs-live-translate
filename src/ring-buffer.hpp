#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace lt {

class ByteRingBuffer {
public:
    explicit ByteRingBuffer(size_t capacity) : capacity_(capacity) {}

    size_t write(const uint8_t *data, size_t len);
    size_t read(uint8_t *out, size_t len);

    size_t size();
    void clear();

private:
    size_t capacity_;
    std::vector<uint8_t> buf_;
    std::mutex mtx_;
};

}
