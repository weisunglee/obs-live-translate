#include <catch2/catch_test_macros.hpp>
#include "base64.hpp"
#include <string>
#include <vector>

using lt::base64_decode;
using lt::base64_encode;

TEST_CASE("base64 encodes known vectors")
{
    REQUIRE(base64_encode(reinterpret_cast<const uint8_t *>(""), 0) == "");
    REQUIRE(base64_encode(reinterpret_cast<const uint8_t *>("f"), 1) == "Zg==");
    REQUIRE(base64_encode(reinterpret_cast<const uint8_t *>("fo"), 2) == "Zm8=");
    REQUIRE(base64_encode(reinterpret_cast<const uint8_t *>("foo"), 3) == "Zm9v");
    REQUIRE(base64_encode(reinterpret_cast<const uint8_t *>("foobar"), 6) == "Zm9vYmFy");
}

TEST_CASE("base64 round-trips arbitrary bytes")
{
    std::vector<uint8_t> bytes{0x00, 0xFF, 0x10, 0x80, 0x7F, 0x01};
    auto enc = base64_encode(bytes.data(), bytes.size());
    auto dec = base64_decode(enc);
    REQUIRE(dec == bytes);
}
