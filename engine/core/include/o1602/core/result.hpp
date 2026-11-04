// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace o1602::core {

// Open1602 runs on 64-bit systems only; sizes such as std::size_t are 64 bits everywhere.
static_assert(sizeof(void*) == 8, "Open1602 needs a 64-bit target");

enum class ErrorCode {
    IoError,        ///< File could not be opened / read / written.
    NotFound,       ///< File or entry does not exist.
    InvalidFormat,  ///< Data does not match the expected format.
    OutOfRange,     ///< Index or offset outside of valid range.
    LimitExceeded,  ///< More than the game or a file format holds; the message names the limit.
    Unsupported,    ///< Valid data, but a variant we do not handle (yet).
    PlatformError,  ///< Operating system or SDL call failed (window, renderer, ...).
    ScriptError,    ///< A mod script failed; the message names the script, the line and the cause.
    Cancelled,      ///< Stopped on request before it was done (a preview no longer wanted).
};

// NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign): the analyser loses the field in std::variant
struct Error {
    ErrorCode code = ErrorCode::IoError;
    std::string message;
};

/// Minimal result type (stand-in for C++23 std::expected).
/// Parsing original game data must never crash on malformed input, so all loaders return Result.
template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : m_data(std::move(value)) {}      // NOLINT(google-explicit-constructor)
    Result(Error error) : m_data(std::move(error)) {}  // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(m_data); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & {
        assert(ok());
        return std::get<T>(m_data);
    }
    [[nodiscard]] const T& value() const& {
        assert(ok());
        return std::get<T>(m_data);
    }
    [[nodiscard]] T&& value() && {
        assert(ok());
        return std::get<T>(std::move(m_data));
    }

    [[nodiscard]] const Error& error() const& {
        assert(!ok());
        return std::get<Error>(m_data);
    }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }

private:
    std::variant<T, Error> m_data;
};

/// Result without a value.
struct Ok {};
using Status = Result<Ok>;

inline Error make_error(ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

}  // namespace o1602::core
