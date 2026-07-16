#pragma once

#include "AssertHelper.h"

#include <type_traits>
#include <utility>

/**
 * @brief Move-only wrapper for a pointer owned through an external API.
 *
 * foreign_ptr does not allocate, destroy, or release the pointed-to resource.
 * The pointer must be explicitly removed with release() before this wrapper is
 * destroyed.
 *
 * This is useful for resources returned by external libraries that require
 * library-specific destruction functions rather than delete.
 *
 * T may be either an element type or a pointer type:
 *
 *     foreign_ptr<Widget>
 *     foreign_ptr<Widget*>
 *     foreign_ptr<void*>
 */
template<class T>
class foreign_ptr
{
public:
    /** @brief Pointer type stored by this wrapper. */
    using pointer = std::conditional_t<std::is_pointer_v<T>, T, T*>;

    /** @brief Type of the pointed-to object. */
    using element_type = std::remove_pointer_t<pointer>;

    /** @brief Constructs an empty foreign_ptr. */
    foreign_ptr() = default;

    /**
     * @brief Takes ownership responsibility for an existing pointer.
     *
     * @param ptr Pointer returned by an external API.
     */
    explicit foreign_ptr(pointer ptr) noexcept
        : m_Ptr(ptr)
    {
    }

    /**
     * @brief Verifies that the pointer was explicitly released.
     *
     * The pointed-to resource is not destroyed.
     */
    ~foreign_ptr() { MLG_ASSERT(!m_Ptr && "foreign_ptr must be released before destruction"); }

    /** @brief Copy construction is disabled. */
    foreign_ptr(const foreign_ptr&) = delete;

    /** @brief Copy assignment is disabled. */
    foreign_ptr& operator=(const foreign_ptr&) = delete;

    /**
     * @brief Transfers ownership from another foreign_ptr.
     *
     * @param other Source wrapper, which becomes empty.
     */
    foreign_ptr(foreign_ptr&& other) noexcept
        : m_Ptr(std::exchange(other.m_Ptr, nullptr))
    {
    }

    /**
     * @brief Transfers ownership from another foreign_ptr.
     *
     * The destination must already be empty. Self-move assignment is permitted.
     *
     * @param other Source wrapper, which becomes empty.
     * @return Reference to this wrapper.
     */
    foreign_ptr& operator=(foreign_ptr&& other) noexcept
    {
        MLG_ASSERT(!m_Ptr || this == &other);
        m_Ptr = std::exchange(other.m_Ptr, nullptr);
        return *this;
    }

    /**
     * @brief Returns the stored pointer without transferring ownership.
     *
     * @return Stored pointer.
     */
    [[nodiscard]] pointer get() const noexcept { return m_Ptr; }

    /**
     * @brief Transfers the stored pointer out of this wrapper.
     *
     * The wrapper becomes empty.
     *
     * @return Previously stored pointer.
     */
    [[nodiscard]] pointer release() noexcept { return std::exchange(m_Ptr, nullptr); }

    /**
     * @brief Dereferences the stored pointer.
     *
     * This operation is unavailable when element_type is void.
     *
     * @return Reference to the pointed-to object.
     */
    template<class U = element_type>
    U& operator*() const noexcept
        requires(!std::is_void_v<U>)
    {
        return *m_Ptr;
    }

    /**
     * @brief Provides pointer-style member access.
     *
     * This operation is unavailable when element_type is void.
     *
     * @return Stored pointer.
     */
    pointer operator->() const noexcept
        requires(!std::is_void_v<element_type>)
    {
        return m_Ptr;
    }

    /**
     * @brief Returns whether this wrapper currently contains a pointer.
     *
     * @return true when the stored pointer is non-null.
     */
    explicit operator bool() const noexcept { return m_Ptr != nullptr; }

private:
    pointer m_Ptr{ nullptr };
};
