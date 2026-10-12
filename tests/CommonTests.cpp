#include <core/common/Assert.hpp>
#include <core/functions/Tag.hpp>
#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace
{
class PolicyScope
{
public:
    PolicyScope() : Saved(errorPolicy()) {}
    ~PolicyScope() { errorPolicy() = std::move(Saved); }
    PolicyScope(const PolicyScope &) = delete;
    PolicyScope &operator=(const PolicyScope &) = delete;

private:
    ErrorPolicy Saved;
};
}

TEST_CASE("Tag is available from functions", "[functions][tag]")
{
    const core::Tag Path{{"Saved", "Player", "Sample"}};
    REQUIRE(Path.get() == "Saved.Player.Sample");
    REQUIRE(Path.has("Player"));
    REQUIRE(Path.at(2) == "Sample");
}

TEST_CASE("Error preserves payload and call site", "[common][error]")
{
    const auto Location = std::source_location::current();
    const Error Failure(EErrorCode::OutOfRange, "outside range", Location);
    const std::exception &Base = Failure;
    REQUIRE(std::string_view(Base.what()) == "outside range");
    REQUIRE(Failure.code() == EErrorCode::OutOfRange);
    REQUIRE(Failure.location().line() == Location.line());
    REQUIRE(std::string_view(Failure.location().file_name()) == Location.file_name());

    UI_CHECK(true, EErrorCode::Logic, "unused");
    const auto ExpectedLine = std::source_location::current().line() + 3;
    try
    {
        UI_CHECK(false, EErrorCode::InvalidArgument, "invalid input");
        FAIL("UI_CHECK must throw");
    }
    catch (const Error &Caught)
    {
        REQUIRE(Caught.code() == EErrorCode::InvalidArgument);
        REQUIRE(std::string_view(Caught.what()) == "invalid input");
        REQUIRE(Caught.location().line() == ExpectedLine);
    }
}

TEST_CASE("Returning or empty error handlers cannot suppress raise", "[common][error]")
{
    PolicyScope Restore;
    std::size_t Calls = 0;
    errorPolicy().OnError = [&](const Error &Failure, std::source_location Location) {
        ++Calls;
        REQUIRE(Failure.code() == EErrorCode::Runtime);
        REQUIRE(Failure.location().line() == Location.line());
    };
    REQUIRE_THROWS_AS(Error::raise(EErrorCode::Runtime, "failure"), Error);
    REQUIRE(Calls == 1);
    Error::require(true, EErrorCode::Runtime, "success");
    REQUIRE(Calls == 1);
    errorPolicy().OnError = {};
    REQUIRE_THROWS_AS(Error::require(false, EErrorCode::Runtime, "failure"), Error);
}

TEST_CASE("Diagnostic macros preserve configuration evaluation rules", "[common][assert]")
{
    PolicyScope Restore;
    std::size_t Calls = 0;
    errorPolicy().OnAssert = [&](std::string_view Message, std::source_location Location) {
        ++Calls;
        REQUIRE(Message == "condition");
        REQUIRE(Location.line() != 0);
    };
    int Evaluations = 0;
    UI_ASSERT(++Evaluations == 0, "condition");
    UI_VERIFY(++Evaluations == 0, "condition");
#ifndef NDEBUG
    REQUIRE(Evaluations == 2);
    REQUIRE(Calls == 2);
#else
    REQUIRE(Evaluations == 1);
    REQUIRE(Calls == 0);
#endif
    diag::assertFailed("condition", std::source_location::current());
#ifndef NDEBUG
    REQUIRE(Calls == 3);
#else
    REQUIRE(Calls == 1);
#endif
}

TEST_CASE("Fatal dispatches code and original location to custom policy", "[common][fatal]")
{
    PolicyScope Restore;
    errorPolicy().OnFatal = [](const Error &Failure, std::source_location Location) {
        REQUIRE(Failure.code() == EErrorCode::Io);
        REQUIRE(Failure.location().line() == Location.line());
        throw Failure;
    };
    REQUIRE_THROWS_AS(UI_FATAL(EErrorCode::Io, "fatal input"), Error);
}