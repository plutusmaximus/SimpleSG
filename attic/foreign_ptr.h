#pragma once

#include "AssertHelper.h"

#include <type_traits>
#include <utility>

/// Move-only wrapper for a pointer owned through an external API.
///
/// foreign_ptr does not allocate, destroy, or release the pointed-to resource.
/// The pointer must be explicitly removed with release() before this wrapper is
/// destroyed.
///
/// This is useful for resources returned by external libraries that require
/// library-specific destruction functions rather than delete.
///
/// T may be either an element type or a pointer type:
///
///     foreign_ptr<Widget>
///     foreign_ptr<Widget*>
///     foreign_ptr<void*>
template<class T>
class foreign_ptr
{
public:
    using pointer = std::conditional_t<std::is_pointer_v<T>, T, T*>;
    using element_type = std::remove_pointer_t<pointer>;

    foreign_ptr() = default;

    explicit foreign_ptr(pointer ptr) noexcept
        : m_Ptr(ptr)
    {
    }

    /// Verifies that the pointer was explicitly released.
    /// The pointed-to resource is not destroyed.
    ~foreign_ptr() { MLG_ASSERT(!m_Ptr && "foreign_ptr must be released before destruction"); }

    foreign_ptr(const foreign_ptr&) = delete;
    foreign_ptr& operator=(const foreign_ptr&) = delete;

    foreign_ptr(foreign_ptr&& other) noexcept
        : m_Ptr(std::exchange(other.m_Ptr, nullptr))
    {
    }

    foreign_ptr& operator=(foreign_ptr&& other) noexcept
    {
        // Self-move assignment is permitted.
        if(this != &other)
        {
            m_Ptr = std::exchange(other.m_Ptr, nullptr);
        }
        return *this;
    }

    /// Returns the stored pointer without transferring ownership.
    [[nodiscard]] pointer get() const noexcept { return m_Ptr; }

    /// Releases the stored pointer.
    /// Afterward it is safe to call the destructor.
    void release() noexcept { m_Ptr = nullptr; }

    /// Dereferences the stored pointer.
    ///
    /// This operation is unavailable when element_type is void.
    template<class U = element_type>
    U& operator*() const noexcept
        requires(!std::is_void_v<U>)
    {
        return *m_Ptr;
    }

    /// Provides pointer-style member access.
    ///
    /// This operation is unavailable when element_type is void.
    pointer operator->() const noexcept
        requires(!std::is_void_v<element_type>)
    {
        return m_Ptr;
    }

    /// True if this wrapper currently contains a pointer.
    explicit operator bool() const noexcept { return m_Ptr != nullptr; }

    friend bool operator==(const foreign_ptr& a, const T* b) noexcept { return a.m_Ptr == b; }
    friend bool operator==(const T* a, const foreign_ptr& b) noexcept { return a == b.m_Ptr; }
    friend bool operator!=(const foreign_ptr& a, const T* b) noexcept { return a.m_Ptr != b; }
    friend bool operator!=(const T* a, const foreign_ptr& b) noexcept { return a != b.m_Ptr; }

private:
    pointer m_Ptr{ nullptr };
};
