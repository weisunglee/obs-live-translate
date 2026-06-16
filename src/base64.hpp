#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace lt {
std::string base64_encode(const uint8_t *data, size_t len);
std::vector<uint8_t> base64_decode(const std::string &in);
}
