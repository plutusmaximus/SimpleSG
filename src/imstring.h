#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <compare>
#include <format>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

/// @brief Immutable string class with copy-on-write semantics.
/// Designed for efficient storage and comparison of strings.
/// On copy only the reference count is incremented, no string data is duplicated.
class imstring final
{
    friend struct std::hash<imstring>;

public:
    using size_type = std::size_t;
    static constexpr size_type npos = static_cast<size_type>(-1);

private:
    struct block
    {
        std::atomic_uint32_t refs;
        size_type            size;
        std::size_t          hashCode;
        char                 data[1];
    };

    block* m_blk;

private:
    template<size_type N>
    static block* make_block(const std::array<const char*, N>& ss, const std::array<size_type, N>& nn)
    {
        size_type n = 0;
        for(size_type i = 0; i < N; ++i)
        {
            n += nn[i];
        }

        if (n == 0)
        {
            static block empty{ std::atomic_uint32_t{UINT32_MAX}, 0, { '\0' } };
            return &empty;
        }

        auto* mem = ::operator new(sizeof(block) + n);
        auto* b = new (mem) block{ std::atomic_uint32_t{1}, n, { 0 } };

        char* p = b->data;
        for(size_type i = 0; i < N; ++i)
        {
            const char* s = ss[i];
            if(!s) continue;
            const size_type n = nn[i];
            for(size_type j = 0; j < n; ++j, ++p, ++s)
            {
                *p = *s;
            }
        }

        *p = '\0';

        b->hashCode = std::hash<std::string_view>{}(std::string_view(b->data, n));
        return b;
    }

    static void retain(block* b)
    {
        if (b && b->refs != UINT32_MAX)
        {
            b->refs.fetch_add(1, std::memory_order_relaxed);
        }
    }

    static void release(block* b)
    {
        if (b && b->refs != UINT32_MAX &&
            b->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            ::operator delete(b);
        }
    }

public:
    constexpr imstring() noexcept : m_blk(nullptr) {}

    imstring(const char* s)
        : m_blk(make_block(std::array<const char*, 1>{ s }, std::array<size_type, 1>{ s ? std::char_traits<char>::length(s) : 0 }))
    {
    }

    imstring(const char* s, size_type n)
        : m_blk(make_block(std::array<const char*, 1>{ s }, std::array<size_type, 1>{ n }))
    {
        if (n && !s) throw std::invalid_argument("imstring");
    }

    imstring(std::string_view sv)
        : m_blk(make_block(std::array<const char*, 1>{ sv.data() }, std::array<size_type, 1>{ sv.size() }))
    {
    }

    imstring(const std::string& s)
        : m_blk(make_block(std::array<const char*, 1>{ s.data() }, std::array<size_type, 1>{ s.size() }))
    {
    }

    imstring(const imstring& o) noexcept : m_blk(o.m_blk)
    {
        retain(m_blk);
    }

    imstring(imstring&& o) noexcept : m_blk(o.m_blk)
    {
        o.m_blk = nullptr;
    }

    imstring& operator=(const imstring& o)
    {
        if (this != &o)
        {
            release(m_blk);
            m_blk = o.m_blk;
            retain(m_blk);
        }
        return *this;
    }

    imstring& operator=(imstring&& o) noexcept
    {
        if (this != &o)
        {
            release(m_blk);
            m_blk = o.m_blk;
            o.m_blk = nullptr;
        }
        return *this;
    }

    imstring& operator=(const std::string& s)
    {
        release(m_blk);
        m_blk = make_block(std::array<const char*, 1>{ s.data() }, std::array<size_type, 1>{ s.size() });
        return *this;
    }

    ~imstring()
    {
        release(m_blk);
    }

public:
    const char* data() const noexcept
    {
        return m_blk ? m_blk->data : "";
    }

    const char* c_str() const noexcept { return data(); }

    size_type size() const noexcept
    {
        return m_blk ? m_blk->size : 0;
    }

    bool empty() const noexcept { return size() == 0; }

    std::string_view view() const noexcept
    {
        return { data(), size() };
    }

    operator std::string_view() const noexcept { return view(); }

public:
    char operator[](size_type i) const noexcept { return data()[i]; }

    char at(size_type i) const
    {
        if (i >= size()) throw std::out_of_range("imstring");
        return data()[i];
    }

public:
    bool starts_with(std::string_view p) const noexcept
    {
        return size() >= p.size() && view().substr(0, p.size()) == p;
    }

    bool ends_with(std::string_view s) const noexcept
    {
        return size() >= s.size() &&
               view().substr(size() - s.size()) == s;
    }

    bool contains(std::string_view n) const noexcept
    {
        return find(n) != npos;
    }

public:
    size_type find(char c, size_type pos = 0) const noexcept
    {
        for (size_type i = pos; i < size(); ++i)
            if (data()[i] == c) return i;
        return npos;
    }

    size_type find(std::string_view n, size_type pos = 0) const noexcept
    {
        if (n.empty()) return pos <= size() ? pos : npos;
        for (size_type i = pos; i + n.size() <= size(); ++i)
            if (view().substr(i, n.size()) == n) return i;
        return npos;
    }

    size_type rfind(char c, size_type pos = npos) const noexcept
    {
        if (size() == 0) return npos;
        size_type i = (pos >= size()) ? size() - 1 : pos;
        for (;;)
        {
            if (data()[i] == c) return i;
            if (i == 0) break;
            --i;
        }
        return npos;
    }

    size_type rfind(std::string_view n, size_type pos = npos) const noexcept
    {
        if (n.empty()) return size();
        size_type i = (pos > size() - n.size()) ? size() - n.size() : pos;
        for (;;)
        {
            if (view().substr(i, n.size()) == n) return i;
            if (i == 0) break;
            --i;
        }
        return npos;
    }

    imstring substr(size_type pos, size_type cnt = npos) const
    {
        if (pos > size()) throw std::out_of_range("imstring");
        size_type len = (cnt == npos || pos + cnt > size()) ? size() - pos : cnt;
        return imstring(data() + pos, len);
    }

public:
    friend imstring operator+(const imstring& a, const imstring& b)
    {
        if (a.empty()) return b;
        if (b.empty()) return a;

        const size_type n = a.size() + b.size();

        // allocate via make_block, then fill
        block* blk = make_block(std::array{a.data(), b.data()}, std::array{a.size(), b.size()});

        imstring r;
        r.m_blk = blk;
        return r;
    }

    friend bool operator==(const imstring& a, const imstring& b) noexcept
    {
        return a.view() == b.view();
    }

    friend std::strong_ordering
    operator<=>(const imstring& a, const imstring& b) noexcept
    {
        return a.view() <=> b.view();
    }
};

inline std::ostream& operator<<(std::ostream& os, const imstring& s)
{
    return os << s.view();
}

template <>
struct std::formatter<imstring, char> : std::formatter<std::string_view, char>
{
    template <class Ctx>
    auto format(const imstring& s, Ctx& ctx) const
    {
        return std::formatter<std::string_view, char>::format(s.view(), ctx);
    }
};

/// @brief Enable hashing of CacheKey for use in unordered containers.
template<>
struct std::hash<imstring>
{
    std::size_t operator()(const imstring& s) const noexcept
    {
        return s.m_blk ? s.m_blk->hashCode : 0;
    }
};

inline imstring operator"" _is(const char* s, std::size_t n)
{
    return imstring(s, n);
}
