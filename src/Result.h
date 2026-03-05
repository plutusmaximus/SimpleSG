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

struct ResultFail final {};

struct ResultSuccess final {};

template<typename T> class ResultBase{};

template<>
class ResultBase<ResultSuccess>
{
public:
    static constexpr ResultSuccess Success = ResultSuccess{};
};

/// @brief Representation of a result that can either be a value of type T or an Error.
template<typename T = ResultSuccess>
class Result final : public ResultBase<T>, private std::variant<Error, T>
{
    using Base = std::variant<Error, T>;
public:

    using Base::Base;

    Result(const Result& other) = default;
    Result(Result&& other) = default;
    Result& operator=(const Result& other) = default;
    Result& operator=(Result&& other) = default;

    constexpr T& value() &
    {
        return std::get<T>(*this);
    }
    constexpr const T& value() const&
    {
        return std::get<T>(*this);
    }
    constexpr T&& value() &&
    {
        return std::move(std::get<T>(*this));
    }
    constexpr const T&& value() const&&
    {
        return std::move(std::get<T>(*this));
    }

    constexpr T& operator*() & { return value(); }
    constexpr const T& operator*() const& { return value(); }
    constexpr T&& operator*() && { return std::move(value()); }
    constexpr const T&& operator*() const&& { return std::move(value()); }

    constexpr T* operator->() { return &value(); }
    constexpr const T* operator->() const { return &value(); }

    constexpr Error& error() & { return std::get<Error>(*this); }
    constexpr const Error& error() const& { return std::get<Error>(*this); }
    constexpr Error&& error() && { return std::move(std::get<Error>(*this)); }
    constexpr const Error&& error() const&& { return std::move(std::get<Error>(*this)); }

    bool has_value() const { return std::holds_alternative<T>(*this); }

    bool has_error() const { return std::holds_alternative<Error>(*this); }

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

    template<typename... Args>
    static inline std::string Format(std::format_string<Args...> fmt, Args&&... args)
    {
        return std::format(fmt, std::forward<Args>(args)...);
    }

    static inline std::string Format()
    {
        static std::string empty = "";
        return empty;
    }

    static inline const std::string& Format(const std::string& str)
    {
        return str;
    }

    static inline std::string_view Format(const std::string_view str)
    {
        return str;
    }

    static inline const char* Format(const char* str)
    {
        return str;
    }

    static inline const imstring& Format(const Error& error)
    {
        return error.GetMessage();
    }
};

#define expect(expr, ...)                                               \
    {                                                                   \
        if(!static_cast<bool>(expr))                                    \
        {                                                               \
            logError("[{}:{}]:{}", __FILE__, __LINE__, Result<>::Format(__VA_ARGS__));    \
            return Error("");               \
        }                                                               \
    }

// Like expect but also calls verify and pops an assert if false.
#define expectv(expr, ...)                                              \
    {                                                                   \
        if(!everify(expr, ##__VA_ARGS__))                               \
        {                                                               \
            logError("[{}:{}]:{}", __FILE__, __LINE__, Result<>::Format(__VA_ARGS__));    \
            return Error("");               \
        }                                                               \
    }
