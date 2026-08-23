// Result<T> -- how failure crosses a layer boundary in RawPage.
//
// THE RULE (Implementation-Plan.md 1.4): no exception crosses a layer. Every boundary
// returns Result<T>, so a caller cannot ignore a failure by accident and no layer has to
// know which exception types another layer might throw.
//
// WHY THE MESSAGE IS A KEY, NOT A SENTENCE: core/ must hold no user-facing English. From
// Stage 1, tools/i18n-lint fails the build on a user-facing literal that is not wrapped in
// tr(), and core/ has no tr() -- it is not a QObject and has no translation context. So an
// Error carries a CATALOG KEY that ui/ resolves, plus untranslated context (a filename, an
// object number) that is interpolated into the translated string. Getting this wrong is not
// a style issue: it is the difference between a product that can ship in zh-TW and one that
// cannot.

#ifndef RAWPAGE_CORE_RESULT_H
#define RAWPAGE_CORE_RESULT_H

#include <QLatin1String>
#include <QString>

#include <cassert>
#include <type_traits>
#include <utility>
#include <variant>

namespace rawpage::core {

// Deliberately coarse. These are the categories a CALLER can act on differently; the
// specific cause belongs in messageKey and context. A code per failure site would give
// hundreds of values that every switch has to ignore.
//
// `Unavailable` was added at Stage 1 T3 against exactly that test. `ActionRegistry::dispatch`
// must distinguish "no such action" from "that action is not available right now"
// (Stage1-AppShell-Testing-Guide.md A-22), and a CLI or batch caller genuinely acts differently on
// the two: the first is a typo in the user's sequence file and is fatal to it, the second is
// document state and may be worth skipping and continuing. `NotFound` covers the first;
// `PermissionDenied` does not cover the second, since a disabled action usually reflects selection
// or mode rather than a permission. Distinguishing them by messageKey alone would have left the
// code -- the thing callers switch on -- unable to express the difference.
enum class ErrorCode {
    Unknown,
    InvalidArgument,   // the caller passed something impossible
    NotFound,          // an object, page, key, or file that should exist does not
    PermissionDenied,  // refused by document permissions or by the OS
    IoFailure,         // the disk, the network, or a handle failed
    ParseFailure,      // malformed input; for PDFs this is the repair path's trigger
    Unsupported,       // well-formed, but a feature we do not implement
    Unavailable,       // exists and is implemented, but not applicable in the current state
    Cancelled,         // the user or a scheduler stopped the operation
    Corrupted,         // internal invariant broken -- a bug, not bad input
};

struct Error {
    ErrorCode code = ErrorCode::Unknown;

    // A catalog key such as "error.pdf.badXref", NOT a sentence. Resolved by ui/.
    QLatin1String messageKey{""};

    // Untranslated detail interpolated into the resolved message: a file name, an object
    // number, an offset. Never a whole sentence -- word order differs between languages.
    QString context;

    Error() = default;
    Error(ErrorCode c, QLatin1String key, QString ctx = {})
        : code(c), messageKey(key), context(std::move(ctx)) {}
};

// ---------------------------------------------------------------------------------------
// Result<T>
// ---------------------------------------------------------------------------------------
template <class T>
class Result {
public:
    static_assert(!std::is_same_v<T, Error>, "Result<Error> is a mistake; use Result<void>");

    // Implicit on purpose, both ways: `return value;` and `return Error{...};` are what
    // call sites should read like. A Result that needs ceremony to construct is a Result
    // people route around.
    Result(T value) : storage_(std::move(value)) {}
    Result(Error error) : storage_(std::move(error)) {}

    bool hasValue() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return hasValue(); }

    // PRECONDITION: hasValue(). Asserted in debug builds. This is a programming error, not
    // a failure to report -- which is why it asserts rather than throwing. "No exception
    // crosses a layer" is about error SIGNALLING; a precondition violation is a bug in the
    // caller and should stop the process in a build where anyone is looking.
    const T& value() const& { assert(hasValue()); return *std::get_if<T>(&storage_); }
    T& value() & { assert(hasValue()); return *std::get_if<T>(&storage_); }
    T&& value() && { assert(hasValue()); return std::move(*std::get_if<T>(&storage_)); }

    const Error& error() const { assert(!hasValue()); return *std::get_if<Error>(&storage_); }

    T valueOr(T fallback) const& { return hasValue() ? value() : std::move(fallback); }

private:
    std::variant<T, Error> storage_;
};

// ---------------------------------------------------------------------------------------
// Result<void> -- an operation that either worked or did not.
// ---------------------------------------------------------------------------------------
template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)), failed_(true) {}

    bool hasValue() const noexcept { return !failed_; }
    explicit operator bool() const noexcept { return hasValue(); }
    const Error& error() const { assert(failed_); return error_; }

private:
    Error error_;
    bool failed_ = false;
};

using Status = Result<void>;

// ---------------------------------------------------------------------------------------
// Propagation
// ---------------------------------------------------------------------------------------
// The Error branch converts implicitly into any Result<U>, so a failure travels outward
// without every frame restating it.
//
//     Result<Document> open(const QString& path) {
//         RAWPAGE_TRY(auto bytes, readAll(path));      // returns on failure
//         RAWPAGE_TRY_STATUS(validate(bytes));
//         return Document(std::move(bytes));
//     }
#define RAWPAGE_TRY(declaration, expression)                                                 \
    auto&& RAWPAGE_TRY_tmp_##__LINE__ = (expression);                                        \
    if (!RAWPAGE_TRY_tmp_##__LINE__) return RAWPAGE_TRY_tmp_##__LINE__.error();                \
    declaration = std::move(RAWPAGE_TRY_tmp_##__LINE__).value()

#define RAWPAGE_TRY_STATUS(expression)                                                       \
    do {                                                                                   \
        auto&& RAWPAGE_TRY_status = (expression);                                            \
        if (!RAWPAGE_TRY_status) return RAWPAGE_TRY_status.error();                            \
    } while (false)

} // namespace rawpage::core

#endif // RAWPAGE_CORE_RESULT_H
