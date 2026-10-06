#pragma once

#include "AssertHelper.h"

#include <cstddef>
#include <new>
#include <memory>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)

/// A contiguous container whose capacity is chosen at construction and never grows.
/// Elements are constructed when inserted and never relocated.
/// Supports types that are neither copyable nor movable through emplace_back().
/// Moving the container transfers its allocation without moving its elements.
/// Inserting beyond capacity aborts.
template<typename T>
class BoundedVector
{
public:
    using value_type = T;
    using size_type = size_t;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using difference_type = std::ptrdiff_t;

    BoundedVector() = delete;
    ~BoundedVector() { Destruct(); }
    BoundedVector(const BoundedVector&) = delete;
    BoundedVector& operator=(const BoundedVector&) = delete;
    BoundedVector(BoundedVector&& other) noexcept
        : m_Storage(other.m_Storage),
          m_Capacity(other.m_Capacity),
          m_Begin(other.m_Begin),
          m_End(other.m_End)
    {
        other.m_Storage = nullptr;
        other.m_Capacity = 0;
        other.m_Begin = nullptr;
        other.m_End = nullptr;
    }
    BoundedVector& operator=(BoundedVector&& other) noexcept
    {
        if(this != &other)
        {
            Destruct();

            m_Storage = other.m_Storage;
            m_Capacity = other.m_Capacity;
            m_Begin = other.m_Begin;
            m_End = other.m_End;

            other.m_Storage = nullptr;
            other.m_Capacity = 0;
            other.m_Begin = nullptr;
            other.m_End = nullptr;
        }
        return *this;
    }

    explicit BoundedVector(const size_type size)
        : m_Capacity(size)
    {
        MLG_ABORTIF(size > max_size(), "Size exceeds maximum allowed size");

        void* p = ::operator new(size * sizeof(T), std::align_val_t{ alignof(T) });

        m_Storage = static_cast<std::byte*>(p);

        m_Begin = m_End = static_cast<T*>(p);
    }

    constexpr size_type max_size() const noexcept
    {
        static_assert(sizeof(T) >= 1, "Element size must be greater than zero");
        static_assert(sizeof(T) <= std::vector<std::byte>().max_size(), "Element size too large");

        return std::vector<std::byte>().max_size() / sizeof(T);
    }

    void push_back(const value_type& value)
    {
        MLG_ABORTIF(size() >= capacity());

        std::construct_at(m_End, value);
        ++m_End;
    }

    void push_back(value_type&& value)
    {
        MLG_ABORTIF(size() >= capacity());

        std::construct_at(m_End, std::move(value));
        ++m_End;
    }

    template<typename... Args>
    reference emplace_back(Args&&... args)
    {
        MLG_ABORTIF(size() >= capacity());

        T* element = std::construct_at(m_End, std::forward<Args>(args)...);
        ++m_End;
        return *element;
    }

    size_type size() const noexcept { return static_cast<size_type>(m_End - m_Begin); }

    bool empty() const noexcept { return size() == 0; }

    size_type capacity() const noexcept { return m_Capacity; }

    reference operator[](size_type index) noexcept
    {
        MLG_ABORTIF(index >= size(), "Index out of bounds");
        return *(begin() + index);
    }

    const_reference operator[](size_type index) const noexcept
    {
        MLG_ABORTIF(index >= size(), "Index out of bounds");
        return *(begin() + index);
    }

    iterator begin() noexcept { return m_End > m_Begin ? m_Begin : nullptr; }

    const_iterator begin() const noexcept { return m_End > m_Begin ? m_Begin : nullptr; }

    iterator end() noexcept { return m_End > m_Begin ? m_End : nullptr; }

    const_iterator end() const noexcept { return m_End > m_Begin ? m_End : nullptr; }

    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }

    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }

    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

    value_type* data() noexcept { return begin(); }

    const value_type* data() const noexcept { return begin(); }

    value_type& front() noexcept
    {
        MLG_ABORTIF(empty(), "Vector is empty");
        return *begin();
    }

    const value_type& front() const noexcept
    {
        MLG_ABORTIF(empty(), "Vector is empty");
        return *begin();
    }

    value_type& back() noexcept
    {
        MLG_ABORTIF(empty(), "Vector is empty");
        return *(end() - 1);
    }

    const value_type& back() const noexcept
    {
        MLG_ABORTIF(empty(), "Vector is empty");
        return *(end() - 1);
    }

private:
    void Destruct()
    {
        if(m_Storage)
        {
            for(size_type i = 0; i < size(); ++i)
            {
                (&(*this)[i])->~T();
            }

            ::operator delete(m_Storage, std::align_val_t{alignof(T)});
            m_Storage = nullptr;
            m_Begin = m_End = nullptr;
            m_Capacity = 0;
        }
    }
    std::byte* m_Storage{ nullptr };
    size_type m_Capacity = 0;
    value_type* m_Begin = nullptr;
    value_type* m_End = nullptr;
};

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)