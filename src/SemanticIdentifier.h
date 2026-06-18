#pragma once

/// @brief A helper class for creating strongly-typed identifiers for various semantics, such as
/// models, meshes, materials, etc.
/// @tparam Tag A unique type used to differentiate between different semantics. The Tag type itself
/// is not important, only that it is unique for each semantic.
template<typename Tag>
class SemanticIdentifier
{
public:
    SemanticIdentifier() = default;
    explicit SemanticIdentifier(const size_t value)
        : m_Value(value)
    {
        MLG_ASSERT(value != kInvalidValue,
            "SemanticIdentifier cannot be created with invalid value");
    }

    bool IsValid() const { return m_Value != kInvalidValue; }

    explicit operator bool() const { return IsValid(); }

    size_t GetValue() const
    {
        MLG_ASSERT(IsValid(), "Cannot get value of invalid SemanticIdentifier");
        return m_Value;
    }

    auto operator<=>(const SemanticIdentifier& other) const = default;

private:
    constexpr static size_t kInvalidValue = static_cast<size_t>(-1);

    size_t m_Value{ kInvalidValue };
};