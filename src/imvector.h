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

    static const T* empty_data_ptr_const() noexcept
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
        if (!m_blk || m_blk->size == 0) return empty_data_ptr_const();
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

// ------------------------------
// imvector<T>::builder (ownership-transfer build)
// Paste this inside class imvector<T> (public section).
//
// Properties:
// - builder constructs elements directly into an internal block buffer.
// - build() transfers the block pointer to the returned imvector (no element copy on build()).
// - Growth reallocates a new block and move-constructs existing elements (like vector growth).
// - No exceptions: uses IMVECTOR_FAIL_FAST() on invariant failures.
// ------------------------------
public:
    class builder final
    {
    public:
        using size_type  = typename imvector::size_type;
        using value_type = typename imvector::value_type;

    private:
        block*    m_blk  = nullptr; // owned by builder until build()
        size_type m_size = 0;       // constructed elements
        size_type m_cap  = 0;       // allocated capacity (in elements)        

        static T* empty_data_ptr() noexcept
        {
            // Non-null, aligned, and stable for the lifetime of the program.
            // Must not be dereferenced (same rule-of-thumb as std::vector::data() for empty).
            alignas(T) static unsigned char storage[(sizeof(T) > 0) ? sizeof(T) : 1];
            return reinterpret_cast<T*>(storage);
        }

        static void require(bool ok) noexcept
        {
            if (!ok) IMVECTOR_FAIL_FAST();
        }

        static void destroy_n(T* p, size_type n) noexcept
        {
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                for (size_type i = 0; i < n; ++i)
                {
                    p[i].~T();
                }
            }
        }

        static size_type grow_capacity(size_type cur, size_type need) noexcept
        {
            // Standard-ish growth: 1.5x, at least 8, and at least need.
            size_type cap = (cur < 8) ? 8 : (cur + (cur >> 1));
            if (cap < need) cap = need;
            return cap;
        }

        void ensure_capacity(size_type need) noexcept
        {
            if (need <= m_cap) return;

            const size_type newCap = grow_capacity(m_cap, need);

            // Allocate a new block with capacity newCap; we will set size explicitly.
            block* newBlk = allocate_block(newCap);
            newBlk->size  = newCap; // while building, header holds capacity; finalized size set on build()

            T* newDst = elements_ptr(newBlk);
            T* oldSrc = m_blk ? const_cast<T*>(elements_ptr(m_blk)) : nullptr;

            // Move-construct existing elements into new storage
            for (size_type i = 0; i < m_size; ++i)
            {
                new (newDst + i) T(std::move(oldSrc[i]));
            }

            // Destroy old elements + free old storage
            if (m_blk && m_cap > 0)
            {
                destroy_n(oldSrc, m_size);
                deallocate_block_raw(m_blk);
            }

            m_blk = newBlk;
            m_cap = newCap;
        }

    public:
        builder() noexcept = default;

        explicit builder(size_type reserveCount) noexcept
        {
            reserve(reserveCount);
        }

        builder(const builder&)            = delete;
        builder& operator=(const builder&) = delete;

        builder(builder&& o) noexcept
            : m_blk(o.m_blk), m_size(o.m_size), m_cap(o.m_cap)
        {
            o.m_blk  = nullptr;
            o.m_size = 0;
            o.m_cap  = 0;
        }

        builder& operator=(builder&& o) noexcept
        {
            if (this != &o)
            {
                clear();
                if (m_blk && m_cap > 0)
                {
                    deallocate_block_raw(m_blk);
                }

                m_blk  = o.m_blk;
                m_size = o.m_size;
                m_cap  = o.m_cap;

                o.m_blk  = nullptr;
                o.m_size = 0;
                o.m_cap  = 0;
            }
            return *this;
        }

        ~builder() noexcept
        {
            clear();
            if (m_blk && m_cap > 0)
            {
                deallocate_block_raw(m_blk);
            }
        }

        size_type size() const noexcept { return m_size; }
        bool      empty() const noexcept { return m_size == 0; }
        size_type capacity() const noexcept { return m_cap; }

        void reserve(size_type n) noexcept
        {
            ensure_capacity(n);
        }

        void clear() noexcept
        {
            if (m_blk && m_size > 0)
            {
                T* p = const_cast<T*>(elements_ptr(m_blk));
                destroy_n(p, m_size);
            }
            m_size = 0;
        }

        // Add elements
        void push_back(const T& v) noexcept
        {
            ensure_capacity(m_size + 1);
            T* p = const_cast<T*>(elements_ptr(m_blk));
            new (p + m_size) T(v);
            ++m_size;
        }

        void push_back(T&& v) noexcept
        {
            ensure_capacity(m_size + 1);
            T* p = const_cast<T*>(elements_ptr(m_blk));
            new (p + m_size) T(std::move(v));
            ++m_size;
        }

        template <class... Args>
        T& emplace_back(Args&&... args) noexcept
        {
            ensure_capacity(m_size + 1);
            T* p = const_cast<T*>(elements_ptr(m_blk));
            T* e = new (p + m_size) T(std::forward<Args>(args)...);
            ++m_size;
            return *e;
        }

        void append(std::span<const T> s) noexcept
        {
            if (s.size() == 0) return;
            ensure_capacity(m_size + static_cast<size_type>(s.size()));
            T* p = const_cast<T*>(elements_ptr(m_blk));
            for (size_type i = 0; i < static_cast<size_type>(s.size()); ++i)
            {
                new (p + m_size + i) T(s[i]);
            }
            m_size += static_cast<size_type>(s.size());
        }

        template <std::input_iterator It, std::sentinel_for<It> S>
        void append(It first, S last) noexcept
        {
            for (; first != last; ++first)
            {
                emplace_back(*first);
            }
        }

        T* data() noexcept
        {
            if (!m_blk || m_size == 0)
            {
                return empty_data_ptr();
            }
            return elements_ptr(m_blk);
        }

        const T* data() const noexcept
        {
            if (!m_blk || m_size == 0)
            {
                // Reuse the same empty-data convention as imvector
                return imvector::empty_data_ptr_const();
            }
            return elements_ptr(m_blk);
        }

        // Finalize: transfer ownership of the internal block to an imvector.
        // No element copy occurs in build(); only sets final size and relinquishes builder ownership.
        imvector build() noexcept
        {
            if (m_size == 0)
            {
                // Keep capacity for reuse; matches vector-like builder semantics.
                return imvector();
            }

            require(m_blk != nullptr);

            // Finalize: set block size to actual element count.
            m_blk->size = m_size;

            block* out = m_blk;

            // Relinquish ownership; builder can be reused via reserve()/push_back().
            m_blk  = nullptr;
            m_size = 0;
            m_cap  = 0;

            return imvector(out);
        }
    };
};
