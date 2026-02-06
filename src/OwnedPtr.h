#pragma once

#include "Error.h"

#include <atomic>

template <typename T>
class OwnedPtr;

template <typename T>
class BorrowedPtr
{
    friend class OwnedPtr<T>;
public:
    BorrowedPtr() = default;

    BorrowedPtr(const BorrowedPtr& other)
    {
        *this = std::move(other.m_Owner->Borrow());
    }

    BorrowedPtr& operator=(const BorrowedPtr& other)
    {
        *this = std::move(other.m_Owner->Borrow());
    }

    BorrowedPtr(BorrowedPtr&& other) noexcept
        : m_Owner(other.m_Owner)
    {
        other.m_Owner = nullptr;
    }

    BorrowedPtr& operator=(BorrowedPtr&& other) noexcept
    {
        if (this != &other)
        {
            Return();
            m_Owner = other.m_Owner;
            other.m_Owner = nullptr;
        }
        return *this;
    }

    void Return()
    {
        if (m_Owner)
        {
            m_Owner->Return(*this);
            m_Owner = nullptr;
        }
    }

    T* Get() const noexcept
    {
        return m_Owner->Get();
    }

    T& operator*() const noexcept
    {
        return *m_Owner->Get();
    }

    T* operator->() const noexcept
    {
        return m_Owner->Get();
    }

    bool IsValid() const noexcept
    {
        return m_Owner != nullptr && m_Owner->Get() != nullptr;
    }

    explicit operator bool() const noexcept
    {
        return m_Owner != nullptr && m_Owner->Get() != nullptr;
    }

private:

    explicit BorrowedPtr(OwnedPtr<T>* owner) noexcept
        : m_Owner(owner)
    {
    }

    OwnedPtr<T>* m_Owner = nullptr;
};

//TODO(KB) - add a deleter
template <typename T>
class OwnedPtr
{
public:
    OwnedPtr() = default;

    explicit OwnedPtr(T* ptr) noexcept
        : m_Ptr(ptr)
    {
    }

    ~OwnedPtr()
    {
        everify(m_BorrowCount.load(std::memory_order_acquire) == 0);
        delete m_Ptr;
    }

    OwnedPtr(const OwnedPtr&) = delete;
    OwnedPtr& operator=(const OwnedPtr&) = delete;

    OwnedPtr(OwnedPtr&& other) noexcept
    {
        if(everify(other.m_BorrowCount.load(std::memory_order_acquire) == 0))
        {
            m_Ptr = other.Release();
        }
    }

    OwnedPtr& operator=(OwnedPtr&& other) noexcept
    {
        if (this != &other)
        {
            if(everify(other.m_BorrowCount.load(std::memory_order_acquire) == 0))
            {
                m_Ptr = other.Release();
            }
        }
        return *this;
    }

    BorrowedPtr<T> Borrow() noexcept
    {
        m_BorrowCount.fetch_add(1, std::memory_order_acq_rel);
        return BorrowedPtr<T>(this);
    }

    BorrowedPtr<const T> Borrow() const noexcept
    {
        m_BorrowCount.fetch_add(1, std::memory_order_acq_rel);
        return BorrowedPtr<const T>(this);
    }

    void Return(BorrowedPtr<T>& borrowed)
    {
        if (everify(borrowed.m_Owner == this))
        {
            eassert(m_BorrowCount.load(std::memory_order_acquire) > 0);

            m_BorrowCount.fetch_sub(1, std::memory_order_acq_rel);
            borrowed.m_Owner = nullptr;
        }
    }

    T* Get() const noexcept
    {
        return m_Ptr;
    }

    T& operator*() const noexcept
    {
        return *m_Ptr;
    }

    T* operator->() const noexcept
    {
        return m_Ptr;
    }

    bool IsValid() const noexcept
    {
        return m_Ptr != nullptr;
    }

    explicit operator bool() const noexcept
    {
        return m_Ptr != nullptr;
    }

private:

    T* Release() noexcept
    {
        if(everify(m_BorrowCount.load(std::memory_order_acquire) == 0))
        {
            return std::exchange(m_Ptr, nullptr);
        }
        return nullptr;
    }

    T* m_Ptr{nullptr};
    mutable std::atomic<uint32_t> m_BorrowCount{0};
};