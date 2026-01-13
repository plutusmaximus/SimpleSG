#pragma once

#include <atomic>

class RefCount
{
public:
    RefCount() : m_RefCount(0) {}

    RefCount(const RefCount&) = delete;
    RefCount& operator=(const RefCount&) = delete;
    RefCount(RefCount&&) = delete;
    RefCount& operator=(RefCount&&) = delete;

    //
    // Returns the resulting ref count.
    //
    int AddRef() const { return m_RefCount.fetch_add(1, std::memory_order_relaxed) + 1; }

    //
    // Returns the resulting ref count.
    //
    int Release() const { return m_RefCount.fetch_sub(1, std::memory_order_acq_rel) - 1; }

private:
    mutable std::atomic_int m_RefCount;
};

/// @brief Concept to constrain types that may be reference counted.
template<typename T>
concept RefCountable = requires(const T& t)
{
    { t.AddRef() } -> std::same_as<void>;
    { t.Release() } -> std::same_as<void>;
    { t.PreventMultipleInclusion() } -> std::same_as<void>;
};

template <RefCountable T>
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
    template <typename U>
    requires std::is_convertible_v<U*, T*>
    RefPtr(const RefPtr<U>& r) : RefPtr(r.m_Ptr) {}

    // Move constructor.
    RefPtr(RefPtr&& r) noexcept : m_Ptr(r.m_Ptr) { r.m_Ptr = nullptr; }

    // Move conversion constructor.
    template <typename U>
    requires std::is_convertible_v<U*, T*>
    RefPtr(RefPtr<U>&& r) noexcept : RefPtr(r.m_Ptr) { r.m_Ptr = nullptr; }

    // Move assignment operator.
    RefPtr& operator=(RefPtr&& r) noexcept
    {
        std::swap(m_Ptr, r.m_Ptr);
        return *this;
    }

    // Move conversion assignment operator.
    template <typename U>
    requires std::is_convertible_v<U*, T*>
    RefPtr& operator=(RefPtr<U>&& r) noexcept
    {
        std::swap(m_Ptr, r.m_Ptr);
        return *this;
    }

    ~RefPtr()
    {
        if (m_Ptr)
        {
            m_Ptr->Release();
        }
    }

    // Delete rvalue overloads of methods that return
    // the raw pointer because they could lead to dangling pointers.

    T* Get(this RefPtr& r) { return r.m_Ptr; }
    T* Get(this RefPtr&& r) = delete;

    const T* Get(this const RefPtr& r) { return r.m_Ptr; }
    const T* Get(this const RefPtr&& r) = delete;

    /// @brief Get the underlying pointer as a different type.
    template<typename U>
    requires std::is_convertible_v<U*, T*>
    U* Get(this RefPtr<T>& r) { return static_cast<U*>(r.m_Ptr); }
    template<typename U>
    requires std::is_convertible_v<U*, T*>
    U* Get(this RefPtr<T>&& r) = delete;

    template<typename U>
    requires std::is_convertible_v<U*, T*>
    const U* Get(this const RefPtr<T>& r) { return static_cast<const U*>(r.m_Ptr); }
    template<typename U>
    requires std::is_convertible_v<U*, T*>
    const U* Get(this const RefPtr<T>&& r) = delete;

    T& operator*(this RefPtr& r) { return *r.Get(); }
    T& operator*(this RefPtr&& r) = delete;

    const T& operator*(this const RefPtr& r) { return *r.Get(); }
    const T& operator*(this const RefPtr&& r) = delete;

    T* operator->(this RefPtr& r) { return r.Get(); }
    T* operator->(this RefPtr&& r) = delete;

    const T* operator->(this const RefPtr& r) { return r.Get(); }
    const T* operator->(this const RefPtr&& r) = delete;

    RefPtr& operator=(std::nullptr_t) { Clear(); return *this; }

    RefPtr& operator=(T* p) { return *this = RefPtr(p); }

    RefPtr& operator=(RefPtr r) { std::swap(m_Ptr, r.m_Ptr); return *this; }

    void Clear() { m_Ptr = nullptr; }

    explicit operator bool() const { return m_Ptr != nullptr; }

    template <typename U>
    requires std::is_convertible_v<U*, T*>
    bool operator==(const RefPtr<U>& rhs) const { return m_Ptr == rhs.Get(); }

    template <typename U>
    requires std::is_convertible_v<U*, T*>
    bool operator!=(const RefPtr<U>& rhs) const { return !operator==(rhs); }

    template <typename U>
    requires std::is_convertible_v<U*, T*>
    bool operator<(const RefPtr<U>& rhs) const { return m_Ptr < rhs.Get(); }

private:
    T* m_Ptr = nullptr;

    // Friend required for conversion constructors.
    template <RefCountable U>
    friend class RefPtr;
};

template <typename T, typename U>
requires std::is_convertible_v<U*, T*>
bool operator==(const RefPtr<T>& lhs, const U* rhs) { return lhs.Get() == rhs; }

template <typename T, typename U>
requires std::is_convertible_v<U*, T*>
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
  virtual void PreventMultipleInclusion() const noexcept final {}  \
 private:                                           \
  RefCount m_RefCount;                              \
IMPLEMENT_NON_COPYABLE(ClassName);