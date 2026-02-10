#pragma once

#include "Logging.h"

#include "imstring.h"

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

/// @brief Error code enumeration.
enum class ErrorCode : int
{
    System = 1
};

/// @brief Representation of an error with code and message.
class Error
{
public:
    Error() = default;

    Error(const char* message)
        : Error(ErrorCode::System, message)
    {
    }

    Error(std::string_view message)
        : Error(ErrorCode::System, message)
    {
    }

    template<typename... Args>
    Error(std::format_string<Args...> fmt, Args&&... args)
        : Error(ErrorCode::System, fmt, std::forward<Args>(args)...)
    {
    }

    Error(const ErrorCode code, std::string_view message)
        : m_Code(code),
          m_Message(message)
    {
    }

    template<typename... Args>
    Error(ErrorCode code, std::format_string<Args...> fmt, Args&&... args)
        : Error(code, std::format(fmt, std::forward<Args>(args)...))
    {
    }

    bool operator==(const Error& other) const
    {
        return m_Code == other.m_Code && m_Message == other.m_Message;
    }

    bool operator!=(const Error& other) const { return !(*this == other); }

    ErrorCode GetCode() const { return m_Code; }

    const imstring& GetMessage() const { return m_Message; }

    // ===== Expression error helpers ======

    /// @brief Used to create error messages for assertion failures.
    template<typename... Args>
    static Error MakeExprError(const char* file,
        const int line,
        const char* exprStr,
        std::string_view format,
        Args&&... args)
    {
        std::string message;
        if(!format.empty())
        {
            message = std::format("[{}:{}]:({}) {}",
                file,
                line,
                exprStr,
                std::vformat(format, std::make_format_args(args...)));
        }
        else
        {
            message = std::format("[{}:{}]:{}", file, line, exprStr);
        }

        return Error(message);
    }

    static Error MakeExprError(
        const char* file, const int line, const char* exprStr, const Error& error)
    {
        return MakeExprError(file, line, exprStr, "{}", error);
    }

private:

    ErrorCode m_Code;

    imstring m_Message;
};

/// @brief Formatter specialization for Error to support std::format.
template<>
struct std::formatter<Error> : std::formatter<imstring>
{
    auto format(const Error& e, std::format_context& ctx) const
    {
        return std::formatter<imstring>::format(e.GetMessage(), ctx);
    }
};

/// @brief Representation of a result that can either be a value of type T or an Error.
template<typename T>
class Result
{
public:
    Result() = default;

    Result(const T& t)
        : m_ValueOrError(t)
    {
    }

    Result(T&& t)
        : m_ValueOrError(std::move(t))
    {
    }

    Result(const Error& error)
        : m_ValueOrError(error)
    {
    }

    Result(Error&& error)
        : m_ValueOrError(std::move(error))
    {
    }

    Result(const Result& other) = default;
    Result(Result&& other) = default;
    Result& operator=(const Result& other) = default;
    Result& operator=(Result&& other) = default;

    Result& operator=(const Error& error)
    {
        m_ValueOrError = error;
        return *this;
    }

    constexpr T& value() &
    {
        return std::get<T>(m_ValueOrError);
    }
    constexpr const T& value() const&
    {
        return std::get<T>(m_ValueOrError);
    }
    constexpr T&& value() &&
    {
        return std::move(std::get<T>(m_ValueOrError));
    }
    constexpr const T&& value() const&&
    {
        return std::move(std::get<T>(m_ValueOrError));
    }

    constexpr T& operator*() & { return value(); }
    constexpr const T& operator*() const& { return value(); }
    constexpr T&& operator*() && { return std::move(value()); }
    constexpr const T&& operator*() const&& { return std::move(value()); }

    constexpr T* operator->() { return &value(); }
    constexpr const T* operator->() const { return &value(); }

    constexpr Error& error() & { return std::get<Error>(m_ValueOrError); }
    constexpr const Error& error() const& { return std::get<Error>(m_ValueOrError); }
    constexpr Error&& error() && { return std::move(std::get<Error>(m_ValueOrError)); }
    constexpr const Error&& error() const&& { return std::move(std::get<Error>(m_ValueOrError)); }

    bool has_value() const { return std::holds_alternative<T>(m_ValueOrError); }

    bool has_error() const { return std::holds_alternative<Error>(m_ValueOrError); }

    operator bool() const { return has_value(); }

    bool operator==(const Result& other) const
    {
        if(has_value() != other.has_value())
        {
            return false;
        }

        if(has_value())
        {
            return value() == other.value();
        }
        else
        {
            return error() == other.error();
        }
    }

    bool operator!=(const Result& other) const { return !(*this == other); }

private:
    std::variant<Error, T> m_ValueOrError;
};

/// @brief Tag type to represent a successful void result.
struct ResultOkTag
{
    explicit ResultOkTag() = default;
};

/// @brief Constant instance of ResultOkTag.
/// Return this from functions returning Result<void> to indicate success.
inline constexpr ResultOkTag ResultOk{};

/// @brief Specialization of Result for void type.
template<>
class Result<void>
{
public:
    Result() = delete;

    Result( ResultOkTag )
        : m_ValueOrError(std::monostate{})
    {
    }

    Result(const Error& error)
        : m_ValueOrError(error)
    {
    }

    Result(Error&& error)
        : m_ValueOrError(std::move(error))
    {
    }

    Result(const Result& other) = default;
    Result(Result&& other) = default;
    Result& operator=(const Result& other) = default;
    Result& operator=(Result&& other) = default;

    constexpr Error& error() & { return std::get<Error>(m_ValueOrError); }
    constexpr const Error& error() const& { return std::get<Error>(m_ValueOrError); }
    constexpr Error&& error() && { return std::move(std::get<Error>(m_ValueOrError)); }
    constexpr const Error&& error() const&& { return std::move(std::get<Error>(m_ValueOrError)); }

    bool has_error() const { return std::holds_alternative<Error>(m_ValueOrError); }

    operator bool() const { return !has_error(); }

    bool operator==(const Result& other) const
    {
        if(has_error() != other.has_error())
        {
            return false;
        }

        if(has_error())
        {
            return (error() == other.error());
        }

        return true;
    }

    bool operator!=(const Result& other) const { return !(*this == other); }

private:

    std::variant<std::monostate, Error> m_ValueOrError;
};

#define MAKE_EXPR_ERROR(exprStr, ...)                                                              \
    Error::MakeExprError(__FILE__, __LINE__, exprStr, ##__VA_ARGS__)

#define expect(expr, ...)                                               \
    {                                                                   \
        if(!static_cast<bool>(expr))                                    \
        {                                                               \
            const Error error = MAKE_EXPR_ERROR(#expr, ##__VA_ARGS__);  \
            logError("{}", error);                                      \
            return error;                                               \
        }                                                               \
    }

// Like expect but also calls verify and pops an assert if false.
#define expectv(expr, ...)                                              \
    {                                                                   \
        if(!everify(expr))                                              \
        {                                                               \
            const Error error = MAKE_EXPR_ERROR(#expr, ##__VA_ARGS__);  \
            logError("{}", error);                                      \
            return error;                                               \
        }                                                               \
    }
