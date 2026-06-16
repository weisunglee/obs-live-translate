#include <catch2/catch_test_macros.hpp>
#include "backoff.hpp"

using lt::Backoff;

TEST_CASE("backoff doubles and caps")
{
    Backoff b(1000, 30000);
    REQUIRE(b.next_ms() == 1000);
    REQUIRE(b.next_ms() == 2000);
    REQUIRE(b.next_ms() == 4000);
    REQUIRE(b.next_ms() == 8000);
    REQUIRE(b.next_ms() == 16000);
    REQUIRE(b.next_ms() == 30000);
    REQUIRE(b.next_ms() == 30000);
}

TEST_CASE("reset returns to base")
{
    Backoff b(1000, 30000);
    b.next_ms();
    b.next_ms();
    b.reset();
    REQUIRE(b.next_ms() == 1000);
}
