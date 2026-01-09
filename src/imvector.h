#pragma once

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdlib>      // std::abort
#include <initializer_list>
#include <iterator>
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

/// If you want a different fail-fast behavior, redefine this macro before including imvector.h.
#ifndef IMVECTOR_FAIL_FAST
#define IMVECTOR_FAIL_FAST() std::abort()
#endif  // IMVECTOR_FAIL_FAST

/// @brief Immutable vector with ref-counted shared storage.
/// Copies are cheap (refcount bump). No mutation APIs are provided.
///
/// data() is NON-NULL even when empty.
/// - Explicit conversion to std::span<const T>.
template <class T>
class imvector final
{
public:
    using value_type             = T;
    using size_type              = std::size_t;
    using difference_type        = std::ptrdiff_t;

    using reference              = const T&;
    using const_reference        = const T&;
    using pointer                = const T*;
    using const_pointer          = const T*;

    using iterator               = const T*;
    using const_iterator         = const T*;
    using reverse_iterator       = std::reverse_iterator<const_iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
    struct block
    {
        std::atomic_uint32_t refs;
        size_type            size;
        // T elems[size] follow (placement-new)
    };

    block* m_blk;

    static block* empty_block() noexcept
    {
        // Never freed. No T instances are constructed in this block.
        static block empty{ std::atomic_uint32_t{ UINT32_MAX }, 0 };
        return &empty;
    }

    static const T* empty_data_ptr() noexcept
    {
        // Non-null, aligned, and stable for the lifetime of the program.
        // Must not be dereferenced (same rule-of-thumb as std::vector::data() for empty).
        alignas(T) static unsigned char storage[(sizeof(T) > 0) ? sizeof(T) : 1];
        return reinterpret_cast<const T*>(storage);
    }

    static void retain(block* b) noexcept
    {
        if (b && b->refs != UINT32_MAX)
        {
            b->refs.fetch_add(1, std::memory_order_relaxed);
        }
    }

    static T* elements_ptr(block* b) noexcept
    {
        return reinterpret_cast<T*>(reinterpret_cast<unsigned char*>(b) + sizeof(block));
    }

    static const T* elements_ptr(const block* b) noexcept
    {
        return reinterpret_cast<const T*>(reinterpret_cast<const unsigned char*>(b) + sizeof(block));
    }

    static constexpr std::size_t bytes_for(size_type n) noexcept
    {
        return sizeof(block) + n * sizeof(T);
    }

    static block* allocate_block(size_type n) noexcept
    {
        if (n == 0)
        {
            return empty_block();
        }

        void* mem = nullptr;

        // Support over-aligned T.
        if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
        {
            mem = ::operator new(bytes_for(n), std::align_val_t(alignof(T)));
        }
        else
        {
            mem = ::operator new(bytes_for(n));
        }

        return new (mem) block{ std::atomic_uint32_t{ 1 }, n };
    }

    static void deallocate_block_raw(block* b) noexcept
    {
        if (!b) return;
        if (b->refs == UINT32_MAX) return;

        if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
        {
            ::operator delete(b, std::align_val_t(alignof(T)));
        }
        else
        {
            ::operator delete(b);
        }
    }

    static void destroy_elements(block* b) noexcept
    {
        if (!b) return;
        if (b->size == 0) return;

        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            T* p = elements_ptr(b);
            for (size_type i = 0; i < b->size; ++i)
            {
                p[i].~T();
            }
        }
    }

    static void release(block* b) noexcept
    {
        if (!b) return;
        if (b->refs == UINT32_MAX) return;

        if (b->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            destroy_elements(b);
            deallocate_block_raw(b);
        }
    }

    // Forward (or stronger) iterators: multi-pass.
    // We can measure distance without consuming the range and allocate once.
    template <std::forward_iterator It, std::sentinel_for<It> S>
    static block* make_block_from_range(It first, S last) noexcept
    {
        const size_type n = static_cast<size_type>(std::distance(first, last));
        if (n == 0)
        {
            return empty_block();
        }

        block* b = allocate_block(n);
        T*     p = elements_ptr(b);

        size_type i = 0;
        for (; first != last; ++first, ++i)
        {
            new (p + i) T(*first);
        }

        return b;
    }

    // Input iterators are single-pass (e.g. std::istream_iterator).
    // Buffer elements first, then build the immutable block in one allocation.
    template <std::input_iterator It, std::sentinel_for<It> S>
        requires (!std::forward_iterator<It>)
    static block* make_block_from_range(It first, S last) noexcept
    {
        // Input iterators cannot be measured without consuming; buffer first.
        std::vector<T> tmp;
        for (; first != last; ++first)
        {
            tmp.emplace_back(*first);
        }
        return make_block_from_range(tmp.begin(), tmp.end());
    }

    static block* make_block_filled(size_type n, const T& value) noexcept
    {
        if (n == 0)
        {
            return empty_block();
        }

        block* b = allocate_block(n);
        T*     p = elements_ptr(b);

        for (size_type i = 0; i < n; ++i)
        {
            new (p + i) T(value);
        }

        return b;
    }

    explicit imvector(block* b) noexcept : m_blk(b) {}

    static void require(bool ok) noexcept
    {
        if (!ok) IMVECTOR_FAIL_FAST();
    }

public:
    constexpr imvector() noexcept : m_blk(nullptr) {}

    imvector(std::initializer_list<T> init) noexcept
        : m_blk(make_block_from_range(init.begin(), init.end()))
    {
    }

    template <std::input_iterator It, std::sentinel_for<It> S>
    imvector(It first, S last) noexcept
        : m_blk(make_block_from_range(first, last))
    {
    }

    explicit imvector(std::span<const T> s) noexcept
        : m_blk(make_block_from_range(s.begin(), s.end()))
    {
    }

    explicit imvector(const std::vector<T>& v) noexcept
        : m_blk(make_block_from_range(v.begin(), v.end()))
    {
    }

    imvector(size_type n, const T& value) noexcept
        : m_blk(make_block_filled(n, value))
    {
    }

    imvector(const imvector& o) noexcept : m_blk(o.m_blk)
    {
        retain(m_blk);
    }

    imvector(imvector&& o) noexcept : m_blk(o.m_blk)
    {
        o.m_blk = nullptr;
    }

    imvector& operator=(const imvector& o) noexcept
    {
        if (this != &o)
        {
            release(m_blk);
            m_blk = o.m_blk;
            retain(m_blk);
        }
        return *this;
    }

    imvector& operator=(imvector&& o) noexcept
    {
        if (this != &o)
        {
            release(m_blk);
            m_blk = o.m_blk;
            o.m_blk = nullptr;
        }
        return *this;
    }

    ~imvector() noexcept
    {
        release(m_blk);
    }

    size_type size() const noexcept
    {
        return m_blk ? m_blk->size : 0;
    }

    bool empty() const noexcept { return size() == 0; }

    // Immutable storage: capacity always equals size (kept for std::vector familiarity).
    size_type capacity() const noexcept { return size(); }

    const T* data() const noexcept
    {
        if (!m_blk || m_blk->size == 0) return empty_data_ptr();
        return elements_ptr(m_blk);
    }

    std::span<const T> span() const noexcept
    {
        return { data(), size() };
    }

    explicit operator std::span<const T>() const noexcept
    {
        return span();
    }

    const_iterator begin() const noexcept { return data(); }
    const_iterator end() const noexcept { return data() + size(); }

    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }

    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

    const T& operator[](size_type i) const noexcept
    {
        // Same as std::vector: unchecked.
        return data()[i];
    }

    const T& at(size_type i) const noexcept
    {
        require(i < size());
        return data()[i];
    }

    const T& front() const noexcept
    {
        require(!empty());
        return (*this)[0];
    }

    const T& back() const noexcept
    {
        require(!empty());
        return (*this)[size() - 1];
    }
};
