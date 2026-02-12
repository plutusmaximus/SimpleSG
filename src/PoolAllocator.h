#pragma once

#include "Error.h"

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

class PoolAllocatorBase
{
public:

    virtual ~PoolAllocatorBase() = default;

    /// @brief Free an object back to the pool.
    virtual void Free(void* voidPtr) = 0;
};

template<typename T>
class PoolAllocatorT : public PoolAllocatorBase
{
public:

    /// @brief Allocate an object from the pool.
    /// Call the constructor with the given arguments.
    template<typename... Args>
    T* Alloc(Args&&... args)
    {
        if(m_FreeList == nullptr)
        {
            AllocateHeap();

            if(!everify(m_FreeList != nullptr, "Failed to allocate heap for PoolAllocator"))
            {
                return nullptr;
            }
        }

        Chunk* chunk = m_FreeList;
        m_FreeList = m_FreeList->Next;

        T* obj = ::new(static_cast<void*>(chunk->Storage)) T(std::forward<Args>(args)...);
        ++m_AllocatedCount;

        // std::launder is required after placement-new into reused raw storage
        // to obtain a pointer to the new object per the lifetime/aliasing rules.
        return std::launder(obj);
    }

    /// @brief Free an object back to the pool.
    void Free(void* voidPtr) override
    {
        if(voidPtr == nullptr)
        {
            return;
        }

        T* ptr = static_cast<T*>(voidPtr);

#if !defined(NDEBUG)
        if(!ValidatePointer(ptr))
        {
            return;
        }
#endif

        ptr->~T();
        --m_AllocatedCount;

        Chunk* chunk = reinterpret_cast<Chunk*>(ptr);
#if !defined(NDEBUG)
        ::memset(chunk->Storage, 0xFE, sizeof(chunk->Storage));
#endif
        chunk->Next = m_FreeList;
        m_FreeList = chunk;
    }

protected:
    PoolAllocatorT() = default;
    virtual ~PoolAllocatorT()
    {
        // If this trips, something allocated from the pool wasn't freed.
        eassert(m_AllocatedCount == 0,
            "PoolAllocator is being destroyed but there are still {} allocated objects",
            m_AllocatedCount);

    }

    virtual void AllocateHeap() = 0;

    virtual bool ValidatePointer(T* ptr) = 0;

    PoolAllocatorT(const PoolAllocatorT&) = delete;
    PoolAllocatorT& operator=(const PoolAllocatorT&) = delete;
    PoolAllocatorT(PoolAllocatorT&&) = delete;
    PoolAllocatorT& operator=(PoolAllocatorT&&) = delete;

    struct Chunk;

    static constexpr std::size_t kChunkAlignment = alignof(T) > alignof(Chunk*)
        ? alignof(T)
        : alignof(Chunk*);

    struct alignas(kChunkAlignment) Chunk
    {
        union
        {
            Chunk* Next;
            char Storage[sizeof(T)];
        };
    };

    Chunk* m_FreeList{ nullptr };
    std::size_t m_AllocatedCount{ 0 };
};

/// @brief A pool allocator for fixed-size objects.
/// Objects are allocated in heaps of ItemsPerHeap objects.
/// ItemsPerHeap specifies how many objects to allocate per heap.
template<typename T, std::size_t ItemsPerHeap>
class PoolAllocator : public PoolAllocatorT<T>
{
    using Base = PoolAllocatorT<T>;
    using Chunk = typename Base::Chunk;

public:
    PoolAllocator()
    {
        static_assert(ItemsPerHeap > 0, "ItemsPerHeap must be > 0.");
        AllocateHeap();
    }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&) = delete;
    PoolAllocator& operator=(PoolAllocator&&) = delete;

    ~PoolAllocator() override
    {
        while(m_Heaps)
        {
            Heap* nextHeap = m_Heaps->m_Next;
            delete m_Heaps;
            m_Heaps = nextHeap;
        }
    }

private:

    struct Heap
    {
        Heap* m_Next{ nullptr };

        Chunk m_Chunks[ItemsPerHeap];
    };

    void AllocateHeap() override
    {
        Heap* heap = new Heap();

        // Push onto the free-list in index order so the head ends up being the last element:
        // LIFO allocation will then walk backward through the array (often slightly nicer
        // locality).
        for(std::size_t i = 0; i < ItemsPerHeap; ++i)
        {
            heap->m_Chunks[i].Next = Base::m_FreeList;
            Base::m_FreeList = &heap->m_Chunks[i];
        }

        heap->m_Next = m_Heaps;
        m_Heaps = heap;
        ++m_HeapCount;
    }

    /// @brief Check that a pointer being freed was allocated from this PoolAllocator.
    bool ValidatePointer(T* ptr) override
    {
        bool validated = false;
        for(Heap* heap = m_Heaps; heap != nullptr; heap = heap->m_Next)
        {
            if(reinterpret_cast<std::uintptr_t>(ptr) >=
                    reinterpret_cast<std::uintptr_t>(heap->m_Chunks) &&
                reinterpret_cast<std::uintptr_t>(ptr) <
                    reinterpret_cast<std::uintptr_t>(heap->m_Chunks) + ItemsPerHeap * sizeof(Chunk))
            {
                validated = true;
                break;
            }
        }
        eassert(validated, "Pointer being freed was not allocated from this PoolAllocator");

        return validated;
    }

    Heap* m_Heaps{ nullptr };
    std::size_t m_HeapCount{ 0 };
};
