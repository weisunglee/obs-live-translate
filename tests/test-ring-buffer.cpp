#include <catch2/catch_test_macros.hpp>
#include "ring-buffer.hpp"
#include <vector>

using lt::ByteRingBuffer;

TEST_CASE("write then read returns same bytes")
{
    ByteRingBuffer rb(1024);
    std::vector<uint8_t> in{1, 2, 3, 4};
    REQUIRE(rb.write(in.data(), in.size()) == 4);
    std::vector<uint8_t> out(4);
    REQUIRE(rb.read(out.data(), 4) == 4);
    REQUIRE(out == in);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("read returns only what is available")
{
    ByteRingBuffer rb(1024);
    std::vector<uint8_t> in{9, 8};
    rb.write(in.data(), in.size());
    std::vector<uint8_t> out(10);
    REQUIRE(rb.read(out.data(), 10) == 2);
}

TEST_CASE("overflow drops oldest bytes")
{
    ByteRingBuffer rb(4);
    std::vector<uint8_t> a{1, 2, 3, 4};
    rb.write(a.data(), a.size());
    std::vector<uint8_t> b{5, 6};
    rb.write(b.data(), b.size());
    std::vector<uint8_t> out(4);
    REQUIRE(rb.read(out.data(), 4) == 4);
    REQUIRE(out == std::vector<uint8_t>{3, 4, 5, 6});
}

TEST_CASE("writing more than capacity keeps only newest capacity bytes")
{
    ByteRingBuffer rb(3);
    std::vector<uint8_t> a{1, 2, 3, 4, 5};
    rb.write(a.data(), a.size());
    std::vector<uint8_t> out(3);
    REQUIRE(rb.read(out.data(), 3) == 3);
    REQUIRE(out == std::vector<uint8_t>{3, 4, 5});
}
