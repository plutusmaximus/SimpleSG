#pragma once

#include "AssertHelper.h"
#include "Log.h"

#include <format>
#include <string>
#include <string_view>
#include <variant>

struct ResultFail final {};

struct ResultOk final {};

/// @brief Representation of a result that can either be a value of type T or an Error.
template<typename SuccessType = ResultOk, typename ErrorType = ResultFail>
class [[nodiscard]] Result final
{
public:
    static constexpr ResultOk Ok;

    static constexpr ResultFail Fail;

    Result() = default;
    Result(const SuccessType& value) : m_Value(value) {} // NOLINT(google-explicit-constructor)
    Result(SuccessType&& value) : m_Value(std::move(value)) {} // NOLINT(google-explicit-constructor)
    Result(const ErrorType& error) : m_Value(error) {} // NOLINT(google-explicit-constructor)
    Result(ErrorType&& error) : m_Value(std::move(error)) {} // NOLINT(google-explicit-constructor)

    Result(const Result& other) = default;
    Result(Result&& other) = default;
    Result& operator=(const Result& other) = default;
    Result& operator=(Result&& other) = default;
    ~Result() = default;

    constexpr SuccessType& Value() &
    {
        MLG_ASSERT(*this, "Attempted to access value of a failed Result");
        return std::get<SuccessType>(m_Value);
    }

    constexpr const SuccessType& Value() const&
    {
        MLG_ASSERT(*this, "Attempted to access value of a failed Result");
        return std::get<SuccessType>(m_Value);
    }

    constexpr SuccessType&& Value() &&
    {
        MLG_ASSERT(*this, "Attempted to access value of a failed Result");
        return std::move(std::get<SuccessType>(m_Value));
    }

    constexpr const SuccessType&& Value() const&&
    {
        MLG_ASSERT(*this, "Attempted to access value of a failed Result");
        return std::move(std::get<SuccessType>(m_Value));
    }

    constexpr SuccessType& operator*() & { return Value(); }
    constexpr const SuccessType& operator*() const& { return Value(); }
    constexpr SuccessType&& operator*() && { return std::move(Value()); }
    constexpr const SuccessType&& operator*() const&& { return std::move(Value()); }
    constexpr SuccessType* operator->() { return &Value(); }
    constexpr const SuccessType* operator->() const { return &Value(); }

    constexpr SuccessType operator->()
        requires std::is_pointer_v<SuccessType>
    {
        return Value();
    }
    constexpr SuccessType operator->() const
        requires std::is_pointer_v<SuccessType>
    {
        return Value();
    }

    explicit operator bool() const { return std::holds_alternative<SuccessType>(m_Value); }

    template<typename... Args>
    static std::string Format(std::format_string<Args...> fmt, Args&&... args)
    {
        return std::format(fmt, std::forward<Args>(args)...);
    }

    static std::string Format()
    {
        constexpr static const std::string empty;
        return empty;
    }

    static const std::string& Format(const std::string& str)
    {
        return str; // NOLINT(bugprone-return-const-ref-from-parameter)
    }

    static std::string_view Format(const std::string_view str)
    {
        return str;
    }

    static const char* Format(const char* str)
    {
        return str;
    }

private:

    std::variant<ErrorType, SuccessType> m_Value;
};

#define MLG_CHECK(expr, ...) \
    while(!static_cast<bool>(expr)) \
    { \
        __VA_OPT__(const std::string errorMessage = Result<>::Format(__VA_ARGS__);) \
        __VA_OPT__(MLG_ERROR("[{}:{}]:{}", __FILE__, __LINE__, errorMessage)); \
        return Result<>::Fail; \
    }

// Like MLG_CHECK but also calls verify and pops an assert if false.
#define MLG_CHECKV(expr, ...) \
    while(!MLG_VERIFY(expr __VA_OPT__(,) __VA_ARGS__)) \
    { \
        __VA_OPT__(const std::string errorMessage = Result<>::Format(__VA_ARGS__);) \
        __VA_OPT__(MLG_ERROR("[{}:{}]:{}", __FILE__, __LINE__, errorMessage)); \
        return Result<>::Fail; \
    }
