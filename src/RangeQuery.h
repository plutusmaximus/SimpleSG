#pragma once

#include <functional>
#include <ranges>
#include <tuple>
#include <vector>

namespace RangeQuery
{
template<class Pred>
struct Where
{
    Pred pred;

    template<class T>
    bool operator()(T& value)
    {
        return std::invoke(pred, value);
    }
};

template<class Func>
struct Apply
{
    Func func;

    template<class T>
    decltype(auto) operator()(T& value)
    {
        std::invoke(func, value); // mutate current object
        return value;             // pass same object downstream
    }
};

template<class Func>
struct Transform
{
    Func func;

    template<class T>
    decltype(auto) operator()(T& value)
    {
        return std::invoke(func, value);
    }
};

template<class T>
inline constexpr bool is_where_v = false;

template<class Pred>
inline constexpr bool is_where_v<Where<Pred>> = true;

template<class T>
inline constexpr bool is_apply_v = false;

template<class Func>
inline constexpr bool is_apply_v<Apply<Func>> = true;

template<class T>
inline constexpr bool is_transform_v = false;

template<class Func>
inline constexpr bool is_transform_v<Transform<Func>> = true;

/// A simple query system for filtering, mutating, and transforming a source range of data.
template<typename Source, typename... Stages>
class Query
{
public:
    Query(Source source, std::tuple<Stages...> stages)
        : m_Source(std::move(source)),
          m_Stages(std::move(stages))
    {
    }

    Query() = delete;
    ~Query() = default;
    Query(const Query&) = delete;
    Query& operator=(const Query&) = delete;
    Query(Query&&) = default;
    Query& operator=(Query&&) = default;

    /// Add a filtering stage to the query.
    template<class Pred>
    auto where(Pred pred) &&
    {
        using SourceElement = std::ranges::range_reference_t<Source>;

        static_assert(std::is_invocable_v<Pred&, SourceElement>,
            "where() predicate must accept exactly one argument compatible with Source element type");
        static_assert(std::is_same_v<std::invoke_result_t<Pred&, SourceElement>, bool>,
            "where() predicate must return bool");

        return std::move(*this).AppendStage(Where<Pred>{ std::move(pred) });
    }

    /// Add a mutating stage to the query.  This variant accepts a function that takes a pointer to
    /// the source element type and returns a pointer to the same type.
    template<class Func>
        requires std::is_pointer_v<std::remove_cvref_t<std::ranges::range_reference_t<Source>>>
    auto apply(Func func) &&
    {
        using SourceElement = std::ranges::range_reference_t<Source>;
        using PointerElement = std::remove_cvref_t<SourceElement>;
        using Result = std::invoke_result_t<Func&, PointerElement>;

        static_assert(std::is_invocable_v<Func&, PointerElement>,
            "apply() pointer source function must accept exactly one pointer argument of the source element type");
        static_assert(std::is_same_v<std::remove_cvref_t<Result>, PointerElement> && !std::is_reference_v<Result>,
            "apply() pointer source must return a non-reference pointer of the same type");

        return std::move(*this).AppendStage(Apply<Func>{ std::move(func) });
    }

    /// Add a mutating stage to the query.  This variant accepts a function that takes a reference
    /// to the source element type and returns a reference to the same type.
    template<class Func>
        requires(!std::is_pointer_v<std::remove_cvref_t<std::ranges::range_reference_t<Source>>>)
    auto apply(Func func) &&
    {
        using SourceElement = std::ranges::range_reference_t<Source>;

        static_assert(std::is_invocable_v<Func&, SourceElement>,
            "apply() value source function must accept exactly one reference argument of the source element type");
        static_assert(std::is_same_v<std::invoke_result_t<Func&, SourceElement>, SourceElement>,
            "apply() value source must return the same reference-qualified value type");

        return std::move(*this).AppendStage(Apply<Func>{ std::move(func) });
    }

    /// Add a transforming stage to the query.
    template<class Func>
    auto transform(Func func) &&
    {
        using SourceElement = std::ranges::range_reference_t<Source>;

        static_assert(std::is_invocable_v<Func&, SourceElement>,
            "transform() function must accept exactly one argument compatible with Source element type");
        static_assert(!std::is_void_v<std::invoke_result_t<Func&, SourceElement>>,
            "transform() function must return non-void");

        return std::move(*this).AppendStage(Transform<Func>{ std::move(func) });
    }

    /// Execute the query on the source data.
    /// This function can only be called on an rvalue (temporary) Query object.
    /// IOW query objects stored to an lvalue cannot be executed.
    ///
    /// Valid usage:
    /// from(data).where(...).apply(...).exec();
    ///
    /// Invalid usage:
    /// auto q = from(data).where(...).apply(...);
    /// q.exec(); // ERROR: cannot call exec() on lvalue
    ///
    /// Valid usage:
    /// auto p = from(data).where(...).apply(...);
    /// std::move(p).exec(); // OK: move p to rvalue and call exec()
    void exec() &&
    {
        auto discard = [](auto&&){};

        for(auto&& item : m_Source)
        {
            RunStage<0>(std::forward<decltype(item)>(item), discard);
        }
    }

    /// Execute the query on the source data and return the number of items that passed through all stages.
    size_t count() &&
    {
        size_t count = 0;

        auto increment = [&count](auto&&){ ++count; };

        for(auto&& item : m_Source)
        {
            RunStage<0>(std::forward<decltype(item)>(item), increment);
        }

        return count;
    }

    /// Execute the query on the source data and append the results to the provided output vector.
    void append_to(std::vector<std::remove_cvref_t<Source>>& out) &&
    {
        auto append = [&out](auto&& item)
        {
            out.emplace_back(std::forward<decltype(item)>(item));
        };

        for(auto&& item : m_Source)
        {
            RunStage<0>(std::forward<decltype(item)>(item), append);
        }
    }

    std::vector<std::remove_cvref_t<Source>> to_vector() &&
    {
        std::vector<std::remove_cvref_t<Source>> result;
        std::move(*this).append_to(result);
        return result;
    }

    void exec() & = delete;

private:

    template<class Stage>
    auto AppendStage(Stage stage) &&
    {
        using NewQuery = Query<Source, Stages..., Stage>;

        auto newStages = std::tuple_cat(std::move(m_Stages), std::tuple<Stage>{ std::move(stage) });

        return NewQuery{ std::move(m_Source), std::move(newStages) };
    }

    template<std::size_t I, class T, typename Sink>
    void RunStage(T& value, Sink sink)
    {
        if constexpr(I == sizeof...(Stages))
        {
            sink(value);
        }
        else
        {
            auto& stage = std::get<I>(m_Stages);

            using StageT = std::remove_cvref_t<decltype(stage)>;

            if constexpr(is_where_v<StageT>)
            {
                if(!stage(value))
                {
                    return;
                }

                RunStage<I + 1>(value, sink);
            }
            else if constexpr(is_apply_v<StageT>)
            {
                decltype(auto) next = stage(value);
                RunStage<I + 1>(next, sink);
            }
            else if constexpr(is_transform_v<StageT>)
            {
                decltype(auto) next = stage(value);
                RunStage<I + 1>(next, sink);
            }
        }
    }

    Source m_Source;
    std::tuple<Stages...> m_Stages;
};

template<class Source>
auto
from(Source&& source)
{
    using View = std::views::all_t<Source>;

    return Query<View>{ std::views::all(std::forward<Source>(source)), std::tuple<>{} };
}

} // namespace RangeQuery
