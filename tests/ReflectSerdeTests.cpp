#include <core/serde/Serde.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>

namespace
{
using Scores = std::map<std::string, int>;

struct SerdeBaseProbe
{
    REFLECT_CLASS(SerdeBaseProbe);
    REFLECT_FIELDS((int, identifier));

    int Identifier() const { return identifier; }
    REFLECT_FUNCTIONS(Identifier);
};

struct SerdeDerivedProbe : SerdeBaseProbe
{
    REFLECT_CLASS(SerdeDerivedProbe, SerdeBaseProbe);
    REFLECT_FIELDS(
        (std::string, name),
        (std::vector<int>, values),
        (Scores, scores),
        (std::optional<std::string>, label)
    );

    void Rename(const std::string &value) { name = value; }
    REFLECT_FUNCTIONS(Rename);
};

SerdeDerivedProbe sample()
{
    return {{7}, "player", {1, 2, 3}, {{"score", 42}}, "active"};
}

struct Workspace
{
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("PrismCoreSerde-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path previous = core::SerdeServer::self().root();

    Workspace() { core::SerdeServer::self().setRoot(path); }
    ~Workspace()
    {
        core::SerdeServer::self().setRoot(previous);
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
}

TEST_CASE("Reflected objects round trip through ADT and formats", "[serde]")
{
    const auto value = core::toAdt(sample());
    auto restored = core::fromAdt<SerdeDerivedProbe>(value);
    REQUIRE(restored);
    REQUIRE(restored->identifier == 7);
    REQUIRE(restored->name == "player");
    REQUIRE(restored->values == std::vector<int>{1, 2, 3});
    REQUIRE(restored->scores.at("score") == 42);
    REQUIRE(restored->label == "active");

    for (auto backend : {core::ESerdeBackend::Json, core::ESerdeBackend::Toml})
    {
        auto *engine = core::getEngineFor(backend);
        REQUIRE(engine);
        const auto text = engine->serialize(value);
        REQUIRE(text.back() == '\n');
        auto decoded = core::fromAdt<SerdeDerivedProbe>(engine->parse(text));
        REQUIRE(decoded);
        REQUIRE(decoded->identifier == restored->identifier);
        REQUIRE(decoded->name == restored->name);
        REQUIRE(decoded->values == restored->values);
        REQUIRE(decoded->scores == restored->scores);
        REQUIRE(decoded->label == restored->label);
        REQUIRE_THROWS(engine->parse("[invalid"));
    }

    REQUIRE_FALSE(core::fromAdt<SerdeDerivedProbe>(core::SerdeADT{42}));
    REQUIRE_FALSE(core::fromAdt<int>(core::SerdeADT{"invalid"}));
    REQUIRE_FALSE(*core::fromAdt<std::optional<int>>(core::SerdeADT{nullptr}));
}

TEST_CASE("Runtime reflection preserves inherited members and methods", "[reflect][serde]")
{
    auto &registry = core::ReflectRegistry::self();
    REQUIRE(registry.has("SerdeDerivedProbe"));
    auto object = registry.create("SerdeDerivedProbe", core::toAdt(sample()));
    REQUIRE(registry.get("SerdeDerivedProbe", "identifier", object).asInt() == 7);
    registry.set("SerdeDerivedProbe", "identifier", object, core::SerdeADT{9});
    REQUIRE(object.cast<SerdeDerivedProbe>().identifier == 9);
    REQUIRE(registry.invoke("SerdeDerivedProbe", "Identifier", object,
        core::SerdeADT{core::SerdeArray{}}).asInt() == 9);
    registry.invoke("SerdeDerivedProbe", "Rename", object,
        core::SerdeADT{core::SerdeArray{core::SerdeADT{"renamed"}}});
    REQUIRE(object.cast<SerdeDerivedProbe>().name == "renamed");
    REQUIRE(registry.baseOf("SerdeDerivedProbe") == "SerdeBaseProbe");
    REQUIRE_THROWS_AS(registry.create("MissingType"), std::invalid_argument);
    REQUIRE_THROWS_AS(registry.get("SerdeDerivedProbe", "missing", object), std::invalid_argument);
    REQUIRE_THROWS_AS(registry.invoke("SerdeDerivedProbe", "Rename", object,
        core::SerdeADT{core::SerdeArray{}}), std::invalid_argument);
}

TEST_CASE("JSON preserves binary and datetime ADT values", "[serde]")
{
    core::SerdeJsonEngine engine;
    const core::SerdeBinary bytes{std::byte{0}, std::byte{42}, std::byte{255}};
    const auto binary = engine.parse(engine.serialize(core::SerdeADT{bytes}));
    REQUIRE(binary.isBinary());
    REQUIRE(binary.asBinary() == bytes);
    const auto timestamp = std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}};
    const auto datetime = engine.parse(engine.serialize(core::SerdeADT{timestamp}));
    REQUIRE(datetime.isDatetime());
    REQUIRE(datetime.asDatetime() == timestamp);
}

TEST_CASE("SerdeServer saves and loads through tagged paths", "[serde][filesystem]")
{
    Workspace workspace;
    auto &server = core::SerdeServer::self();
    const core::Tag key{{"Saved", "Player", "Sample"}};
    for (auto backend : {core::ESerdeBackend::Json, core::ESerdeBackend::Toml})
    {
        REQUIRE(server.serialize(core::toAdt(sample()), key, backend, core::ESerdeMode::Overwrite));
        auto value = server.deserialize(key, backend);
        REQUIRE(value);
        auto restored = core::fromAdt<SerdeDerivedProbe>(*value);
        REQUIRE(restored);
        REQUIRE(restored->name == "player");
    }
    REQUIRE(std::filesystem::exists(workspace.path / "Saved/Player/Sample.json"));
    REQUIRE(std::filesystem::exists(workspace.path / "Saved/Player/Sample.toml"));
    const core::Tag missing{{"Missing", "Sample"}};
    REQUIRE_FALSE(server.deserialize(missing, core::ESerdeBackend::Json));
    REQUIRE_FALSE(std::filesystem::exists(workspace.path / "Missing"));
    REQUIRE_FALSE(server.serialize(core::toAdt(sample()), key,
        core::ESerdeBackend::Binary, core::ESerdeMode::Overwrite));
}