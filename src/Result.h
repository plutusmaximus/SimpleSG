#pragma once

#include "AssertHelper.h"
#include "Log.h"
#include "imstring.h"

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

struct ResultFail final {};

struct ResultOk final {};

template<typename T> class ResultBase{};

template<>
class ResultBase<ResultOk>
{
public:
    static constexpr ResultOk Ok;

    static constexpr ResultFail Fail;
};

/// @brief Representation of a result that can either be a value of type T or an Error.
template<typename SuccessType = ResultOk, typename ErrorType = ResultFail>
class Result final : public ResultBase<SuccessType>, private std::variant<ErrorType, SuccessType>
{
    using Base = std::variant<ErrorType, SuccessType>;
public:

    using Base::Base;

    Result(const Result& other) = default;
    Result(Result&& other) = default;
    Result& operator=(const Result& other) = default;
    Result& operator=(Result&& other) = default;

    constexpr SuccessType& operator*() & { return std::get<SuccessType>(*this); }
    constexpr const SuccessType& operator*() const& { return std::get<SuccessType>(*this); }
    constexpr SuccessType&& operator*() && { return std::move(std::get<SuccessType>(*this)); }
    constexpr const SuccessType&& operator*() const&& { return std::move(std::get<SuccessType>(*this)); }

    constexpr SuccessType* operator->() { return &std::get<SuccessType>(*this); }
    constexpr const SuccessType* operator->() const { return &std::get<SuccessType>(*this); }

    operator bool() const { return std::holds_alternative<SuccessType>(*this); }

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
};

#define MLG_CHECK(expr, ...) \
    do{ \
        if(!static_cast<bool>(expr)) \
        { \
            MLG_ERROR("[{}:{}]:{}", __FILE__, __LINE__, Result<>::Format(__VA_ARGS__)); \
            return Result<>::Fail; \
        } \
    } while(0)

// Like MLG_CHECK but also calls verify and pops an assert if false.
#define MLG_CHECKV(expr, ...) \
    do{ \
        if(!MLG_VERIFY(expr, ##__VA_ARGS__)) \
        { \
            MLG_ERROR("[{}:{}]:{}", __FILE__, __LINE__, Result<>::Format(__VA_ARGS__)); \
            return Result<>::Fail; \
        } \
    } while(0)
