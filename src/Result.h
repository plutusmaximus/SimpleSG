#pragma once

#include "AssertHelper.h"
#include "Log.h"

#include <variant>

struct ResultFail final
{
};

struct ResultOk final
{
};

/// Representation of a result that can either be a value of type T or an Error.
template<typename SuccessType = ResultOk, typename ErrorType = ResultFail>
class [[nodiscard]] Result final
{
public:
    static constexpr ResultOk Ok;

    static constexpr ResultFail Fail;

    Result() = default;
    // NOLINTNEXTLINE(google-explicit-constructor)
    Result(const SuccessType& value)
        : m_Value(value)
    {
    }
    // NOLINTNEXTLINE(google-explicit-constructor)
    Result(SuccessType&& value)
        : m_Value(std::move(value))
    {
    }
    // NOLINTNEXTLINE(google-explicit-constructor)
    Result(const ErrorType& error)
        : m_Value(error)
    {
    }
    // NOLINTNEXTLINE(google-explicit-constructor)
    Result(ErrorType&& error)
        : m_Value(std::move(error))
    {
    }

    Result(const Result& other) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) = default;
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

    bool Succeeded() const { return std::holds_alternative<SuccessType>(m_Value); }
    bool Failed() const { return !Succeeded(); }

    void Reset() { m_Value = ErrorType{}; }

    explicit operator bool() const { return std::holds_alternative<SuccessType>(m_Value); }

private:
    std::variant<ErrorType, SuccessType> m_Value;
};

#define MLG_CHECK(expr, ...)                                                                       \
    while(!static_cast<bool>(expr))                                                                \
    {                                                                                              \
        __VA_OPT__(MLG_ERROR(__VA_ARGS__));                                                        \
        return Result<>::Fail;                                                                     \
    }

// Like MLG_CHECK but also calls verify and pops an assert if false.
#define MLG_CHECKV(expr, ...)                                                                      \
    while(!MLG_VERIFY(expr __VA_OPT__(, ) __VA_ARGS__))                                            \
    {                                                                                              \
        __VA_OPT__(MLG_ERROR(__VA_ARGS__));                                                        \
        return Result<>::Fail;                                                                     \
    }
