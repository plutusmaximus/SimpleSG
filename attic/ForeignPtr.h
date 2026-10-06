#pragma once

#include <type_traits>
#include <utility>

/// Move-only wrapper for a pointer owned through an external API.
///
/// ForeignPtr does not allocate, destroy, or release the pointed-to resource.
///
/// This is useful for resources returned by external libraries that require
/// library-specific destruction functions rather than delete.
///
/// T may be either an element type or a pointer type:
///
///     ForeignPtr<Widget>
///     ForeignPtr<Widget*>
///     ForeignPtr<void*>
template<class T>
class ForeignPtr
{
public:
    using pointer_type = std::conditional_t<std::is_pointer_v<T>, T, T*>;
    using element_type = std::remove_pointer_t<pointer_type>;
    using const_pointer_type = std::conditional_t<std::is_const_v<element_type>,
        pointer_type,
        std::add_pointer_t<std::add_const_t<element_type>>>;

    ForeignPtr() = default;

    explicit ForeignPtr(pointer_type ptr) noexcept
        : m_Ptr(ptr)
    {
    }

    /// The pointed-to resource is not destroyed.
    ~ForeignPtr() { m_Ptr = nullptr; }

    ForeignPtr(const ForeignPtr&) = delete;
    ForeignPtr& operator=(const ForeignPtr&) = delete;

    ForeignPtr(ForeignPtr&& other) noexcept
        : m_Ptr(std::exchange(other.m_Ptr, nullptr))
    {
    }

    ForeignPtr& operator=(ForeignPtr&& other) noexcept
    {
        // Self-move assignment is permitted.
        if(this != &other)
        {
            m_Ptr = std::exchange(other.m_Ptr, nullptr);
        }
        return *this;
    }

    /// Returns the stored pointer without transferring ownership.
    [[nodiscard]] pointer_type get() const noexcept { return m_Ptr; }

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
    pointer_type operator->() const noexcept
        requires(!std::is_void_v<element_type>)
    {
        return m_Ptr;
    }

    /// True if this wrapper currently contains a pointer.
    explicit operator bool() const noexcept { return m_Ptr != nullptr; }

    friend bool operator==(const ForeignPtr& a, const_pointer_type b) noexcept { return a.m_Ptr == b; }
    friend bool operator==(const_pointer_type a, const ForeignPtr& b) noexcept { return a == b.m_Ptr; }
    friend bool operator!=(const ForeignPtr& a, const_pointer_type b) noexcept { return a.m_Ptr != b; }
    friend bool operator!=(const_pointer_type a, const ForeignPtr& b) noexcept { return a != b.m_Ptr; }

private:
    pointer_type m_Ptr{ nullptr };
};
