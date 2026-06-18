#include <catch2/catch_test_macros.hpp>
#include "owner-guard.hpp"

using lt::OwnerGuard;

TEST_CASE("first claimer wins, second is denied")
{
    OwnerGuard g;
    int a, b; // distinct objects -> distinct token addresses
    REQUIRE(g.claim(&a));
    REQUIRE_FALSE(g.claim(&b));
    REQUIRE(g.owned_by_other(&b));
    REQUIRE_FALSE(g.owned_by_other(&a));
    REQUIRE(g.has_owner());
}

TEST_CASE("claiming again with the same token stays owned")
{
    OwnerGuard g;
    int a;
    REQUIRE(g.claim(&a));
    REQUIRE(g.claim(&a));
    REQUIRE_FALSE(g.owned_by_other(&a));
}

TEST_CASE("release frees the resource for another token")
{
    OwnerGuard g;
    int a, b;
    REQUIRE(g.claim(&a));
    g.release(&a);
    REQUIRE_FALSE(g.has_owner());
    REQUIRE(g.claim(&b));
    REQUIRE(g.owned_by_other(&a));
}

TEST_CASE("a non-owner cannot release")
{
    OwnerGuard g;
    int a, b;
    REQUIRE(g.claim(&a));
    g.release(&b); // no-op: b is not the owner
    REQUIRE(g.has_owner());
    REQUIRE_FALSE(g.claim(&b));
}
