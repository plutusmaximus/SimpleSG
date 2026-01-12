#pragma once

#include <atomic>

/// @brief Thread-safe reference counting base class.
/// Uses atomic operations for thread-safe reference counting.
/// AddRef uses relaxed ordering for performance, Release uses acquire-release semantics.
class RefCount
{
public:
    RefCount() : m_RefCount(0) {}

    RefCount(const RefCount&) = delete;
    RefCount& operator=(const RefCount&) = delete;

    /// @brief Atomically increments the reference count.
    /// @return The resulting reference count after increment.
    /// @note Thread-safe. Uses relaxed memory ordering for performance.
    int AddRef() const { return m_RefCount.fetch_add(1, std::memory_order_relaxed) + 1; }

    /// @brief Atomically decrements the reference count.
    /// @return The resulting reference count after decrement.
    /// @note Thread-safe. Uses acquire-release ordering to ensure proper cleanup.
    int Release() const { return m_RefCount.fetch_sub(1, std::memory_order_acq_rel) - 1; }

private:
    mutable std::atomic_int m_RefCount;
};

template <typename T>
class RefPtr
{
public:
    constexpr RefPtr() = default;

    // Allow implicit construction from nullptr.
    constexpr RefPtr(std::nullptr_t) {}

    RefPtr(T* p) : m_Ptr(p)
    {
        if (m_Ptr)
        {
            m_Ptr->AddRef();
        }
    }

    // Copy constructor.
    RefPtr(const RefPtr& r) : RefPtr(r.m_Ptr) {}

    // Copy conversion constructor.
    template <typename U,
        typename = typename std::enable_if<
        std::is_convertible<U*, T*>::value>::type>
    RefPtr(const RefPtr<U>& r) : RefPtr(r.m_Ptr) {}

    // Move constructor.
    RefPtr(RefPtr&& r) noexcept : m_Ptr(r.m_Ptr) { r.m_Ptr = nullptr; }

    // Move conversion constructor.
    template <typename U,
        typename = typename std::enable_if<
        std::is_convertible<U*, T*>::value>::type>
    RefPtr(RefPtr<U>&& r) noexcept : RefPtr(r.m_Ptr) { r.m_Ptr = nullptr; }

    ~RefPtr()
    {
        if (m_Ptr)
        {
            m_Ptr->Release();
        }
    }

    [[nodiscard]] T* Get() noexcept { return m_Ptr; }

    [[nodiscard]] const T* Get() const noexcept { return m_Ptr; }

    template<typename U>
    [[nodiscard]] U* Get() noexcept { return static_cast<U*>(m_Ptr); }

    template<typename U>
    [[nodiscard]] const U* Get() const noexcept { return static_cast<const U*>(m_Ptr); }

    [[nodiscard]] T& operator*() noexcept
    {
        eassert(m_Ptr != nullptr, "Dereferencing null RefPtr");
        return *m_Ptr;
    }

    [[nodiscard]] const T& operator*() const noexcept
    {
        eassert(m_Ptr != nullptr, "Dereferencing null RefPtr");
        return *m_Ptr;
    }

    [[nodiscard]] T* operator->() noexcept
    {
        eassert(m_Ptr != nullptr, "Accessing null RefPtr");
        return m_Ptr;
    }

    [[nodiscard]] const T* operator->() const noexcept
    {
        eassert(m_Ptr != nullptr, "Accessing null RefPtr");
        return m_Ptr;
    }

    RefPtr& operator=(std::nullptr_t) { Clear(); return *this; }

    RefPtr& operator=(T* p) { return *this = RefPtr(p); }

    RefPtr& operator=(RefPtr r) { std::swap(m_Ptr, r.m_Ptr); return *this; }

    void Clear()
    {
        if (m_Ptr)
        {
            m_Ptr->Release();
            m_Ptr = nullptr;
        }
    }

    explicit operator bool() const { return m_Ptr != nullptr; }

    template <typename U>
    bool operator==(const RefPtr<U>& rhs) const { return m_Ptr == rhs.Get(); }

    template <typename U>
    bool operator!=(const RefPtr<U>& rhs) const { return !operator==(rhs); }

    template <typename U>
    bool operator<(const RefPtr<U>& rhs) const { return m_Ptr < rhs.Get(); }

private:
    T* m_Ptr = nullptr;

    // Friend required for conversion constructors.
    template <typename U>
    friend class RefPtr;
};

template <typename T, typename U>
bool operator==(const RefPtr<T>& lhs, const U* rhs) { return lhs.Get() == rhs; }

template <typename T, typename U>
bool operator==(const T* lhs, const RefPtr<U>& rhs) { return lhs == rhs.Get(); }

template <typename T>
bool operator==(const RefPtr<T>& lhs, std::nullptr_t null) { return !static_cast<bool>(lhs); }

template <typename T>
bool operator==(std::nullptr_t null, const RefPtr<T>& rhs) { return !static_cast<bool>(rhs); }

template <typename T, typename U>
bool operator!=(const RefPtr<T>& lhs, const U* rhs) { return !operator==(lhs, rhs); }

template <typename T, typename U>
bool operator!=(const T* lhs, const RefPtr<U>& rhs) { return !operator==(lhs, rhs); }

template <typename T>
bool operator!=(const RefPtr<T>& lhs, std::nullptr_t null) { return !operator==(lhs, null); }

template <typename T>
bool operator!=(std::nullptr_t null, const RefPtr<T>& rhs) { return !operator==(null, rhs); }

#define IMPLEMENT_NON_COPYABLE(ClassName)           \
private:                                            \
ClassName(const ClassName&) = delete;               \
ClassName(ClassName&&) = delete;                    \
ClassName& operator=(const ClassName&) = delete;

//Also implements NON_COPYABLE
#define IMPLEMENT_REFCOUNT(ClassName)               \
 public:                                            \
  void AddRef() const {                             \
    m_RefCount.AddRef();                            \
  }                                                 \
  void Release() const {                            \
    if (!m_RefCount.Release()) {                    \
      delete static_cast<const ClassName*>(this);   \
    }                                               \
  }                                                 \
                                                    \
 private:                                           \
  virtual void PreventMultipleInclusion() final {}  \
  RefCount m_RefCount;                              \
IMPLEMENT_NON_COPYABLE(ClassName);
