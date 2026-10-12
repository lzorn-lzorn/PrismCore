#include <core/reflect/Reflect.hpp>
#include <catch2/catch_test_macros.hpp>

#if defined(NLOHMANN_JSON_VERSION_MAJOR) || defined(TOML_LIB_MAJOR)
#error Reflect must not include JSON or TOML parsers
#endif

namespace
{
struct ReflectBaseProbe
{
    REFLECT_CLASS(ReflectBaseProbe);
    REFLECT_FIELDS((int, identifier));
};

struct ReflectDerivedProbe : ReflectBaseProbe
{
    REFLECT_CLASS(ReflectDerivedProbe, ReflectBaseProbe);
    REFLECT_FIELDS((int, value));
};
}

TEST_CASE("Reflect exposes metadata without format parsers", "[reflect]")
{
    static_assert(core::TypeInfo<ReflectDerivedProbe>::HasBase);
    static_assert(core::TypeInfo<ReflectDerivedProbe>::BaseName == "ReflectBaseProbe");
    ReflectDerivedProbe object{{7}, 42};
    int total = 0;
    core::TypeInfo<ReflectDerivedProbe>::forEachMembers(object,
        [&total](const char *, int value) { total += value; });
    REQUIRE(total == 49);
    REQUIRE(core::TypeInfo<ReflectDerivedProbe>::isDerivedFrom("ReflectBaseProbe"));
    REQUIRE(core::TypeInfo<ReflectBaseProbe>::derivedNames() ==
        std::vector<std::string>{"ReflectDerivedProbe"});
}

TEST_CASE("ReflectObject copies and moves values", "[reflect]")
{
    core::ReflectObject original{ReflectBaseProbe{7}};
    auto copy = original;
    copy.cast<ReflectBaseProbe>().identifier = 9;
    REQUIRE(original.cast<ReflectBaseProbe>().identifier == 7);
    auto moved = std::move(copy);
    REQUIRE_FALSE(copy.hasValue());
    REQUIRE(moved.cast<ReflectBaseProbe>().identifier == 9);
    REQUIRE_THROWS_AS(moved.cast<int>(), std::bad_cast);
}