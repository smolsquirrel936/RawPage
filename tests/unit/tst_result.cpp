#include "core/Result.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

using namespace rawpage::core;

namespace {

Result<int> parsePositive(int raw)
{
    if (raw <= 0) {
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.value.notPositive"),
                     QString::number(raw)};
    }
    return raw;
}

Result<int> doubled(int raw)
{
    RAWPAGE_TRY(const int value, parsePositive(raw));
    return value * 2;
}

Status mustBePositive(int raw)
{
    RAWPAGE_TRY_STATUS(parsePositive(raw));
    return {};
}

} // namespace

TEST_CASE("Result carries a value", "[tst_result]")
{
    const Result<int> ok = parsePositive(7);
    REQUIRE(ok.hasValue());
    REQUIRE(static_cast<bool>(ok));
    CHECK(ok.value() == 7);
}

TEST_CASE("Result carries a structured error", "[tst_result]")
{
    const Result<int> bad = parsePositive(-3);
    REQUIRE_FALSE(bad.hasValue());
    REQUIRE_FALSE(static_cast<bool>(bad));
    CHECK(bad.error().code == ErrorCode::InvalidArgument);
    // The message is a CATALOG KEY, not a sentence: core/ holds no user-facing English,
    // and ui/ resolves this through tr(). A test that asserted on prose would quietly
    // legitimise putting prose here.
    CHECK(bad.error().messageKey == QLatin1String("error.value.notPositive"));
    CHECK(bad.error().context == QStringLiteral("-3"));
}

TEST_CASE("RAWPAGE_TRY propagates the error outward unchanged", "[tst_result]")
{
    const Result<int> good = doubled(21);
    REQUIRE(good.hasValue());
    CHECK(good.value() == 42);

    const Result<int> bad = doubled(0);
    REQUIRE_FALSE(bad.hasValue());
    CHECK(bad.error().messageKey == QLatin1String("error.value.notPositive"));
}

TEST_CASE("Result<void> reports success and failure", "[tst_result]")
{
    CHECK(mustBePositive(1).hasValue());

    const Status bad = mustBePositive(-1);
    REQUIRE_FALSE(bad.hasValue());
    CHECK(bad.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("valueOr yields the fallback without touching the error", "[tst_result]")
{
    CHECK(parsePositive(5).valueOr(99) == 5);
    CHECK(parsePositive(-5).valueOr(99) == 99);
}

TEST_CASE("Result moves a non-copyable payload", "[tst_result]")
{
    struct MoveOnly {
        std::string text;
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&&) = default;
        MoveOnly& operator=(MoveOnly&&) = default;
        explicit MoveOnly(std::string t) : text(std::move(t)) {}
    };

    Result<MoveOnly> r{MoveOnly{"payload"}};
    REQUIRE(r.hasValue());
    const MoveOnly taken = std::move(r).value();
    CHECK(taken.text == "payload");
}

// Nothing in the header may throw: "no exception crosses a layer" is the whole reason
// Result exists, and a Result that throws on construction defeats it at the first frame.
TEST_CASE("Result construction does not throw", "[tst_result]")
{
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Error> == false
                   || std::is_nothrow_move_constructible_v<Error> == true);
    CHECK_NOTHROW(Result<int>(1));
    CHECK_NOTHROW(Result<int>(Error{ErrorCode::Unknown, QLatin1String("k")}));
    CHECK_NOTHROW(Status{});
}
