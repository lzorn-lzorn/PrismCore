#include <catch2/catch_test_macros.hpp>
#include <Core.hpp>

namespace
{
enum class BuildFlag : unsigned
{
    Enabled = 1
};
}

TEST_CASE("Core public headers are available to consumers", "[build]")
{
    core::Flags<BuildFlag> flags(BuildFlag::Enabled);
    REQUIRE(flags.has(BuildFlag::Enabled));
}