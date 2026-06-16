#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lt {

std::vector<float> downmix_to_mono(const float *const *planes,
                                   size_t channels, size_t frames);

std::vector<uint8_t> float_to_s16le(const float *samples, size_t count);

class Chunker {
public:
    explicit Chunker(size_t chunk_bytes) : chunk_bytes_(chunk_bytes) {}
    std::vector<std::vector<uint8_t>> push(const uint8_t *data, size_t len);
    void reset() { acc_.clear(); }

private:
    size_t chunk_bytes_;
    std::vector<uint8_t> acc_;
};

}
