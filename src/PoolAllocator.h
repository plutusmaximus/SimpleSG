#pragma once

#include "Error.h"

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

class Allocator
{
public:

    virtual ~Allocator() = 0;

    /// @brief Deletes an object and returns it back to the pool.
    /// The object must have been allocated from this pool.
    /// The destructor of the object will be called before it's freed.
    virtual void Delete(void* voidPtr) = 0;
};

inline Allocator::~Allocator() = default;

template<typename T>
class PoolAllocatorT : public Allocator
{
    static constexpr uint32_t GUARD_VALUE = 0xFEEDFACE;

public:

    /// @brief Allocate an object from the pool.
    /// Call the constructor with the given arguments.
    template<typename... Args>
    T* New(Args&&... args)
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
        chunk->Next = nullptr;

        T* obj = ::new(static_cast<void*>(chunk->Storage)) T(std::forward<Args>(args)...);
        ++m_AllocatedCount;

        // std::launder is required after placement-new into reused raw storage
        // to obtain a pointer to the new object per the lifetime/aliasing rules.
        return std::launder(obj);
    }

    /// @brief Deletes an object and returns it back to the pool.
    /// The object must have been allocated from this pool.
    /// The destructor of the object will be called before it's freed.
    void Delete(void* voidPtr) override
    {
        if(voidPtr == nullptr)
        {
            return;
        }

        Chunk* chunk = Chunk::FromStorage(voidPtr);
        eassert(this == chunk->Allocator,
            "Pointer being freed was not allocated from this PoolAllocator");
        eassert(nullptr == chunk->Next, "Double free detected in PoolAllocator");
        eassert(chunk->Guard == GUARD_VALUE, "Memory corruption detected in PoolAllocator");

        T* ptr = static_cast<T*>(voidPtr);

        ptr->~T();
        --m_AllocatedCount;

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

    PoolAllocatorT(const PoolAllocatorT&) = delete;
    PoolAllocatorT& operator=(const PoolAllocatorT&) = delete;
    PoolAllocatorT(PoolAllocatorT&&) = delete;
    PoolAllocatorT& operator=(PoolAllocatorT&&) = delete;

    struct Chunk;

    static constexpr std::size_t kChunkAlignment = alignof(T) > alignof(Chunk*)
        ? alignof(T)
        : alignof(Chunk*);

    struct Chunk
    {
        Chunk* Next;
        // Allocator that owns this chunk.
        Allocator* Allocator;
        alignas(kChunkAlignment) char Storage[sizeof(T)];
        uint32_t Guard{GUARD_VALUE}; // Used to detect memory corruption and double frees.

        static Chunk* FromStorage(void* p) noexcept
        {
            // p must point to Storage[0] of some Chunk
            auto base = reinterpret_cast<std::byte*>(p) - offsetof(Chunk, Storage);
            return reinterpret_cast<Chunk*>(base);
        }

        static const Chunk* FromStorage(const void* p) noexcept
        {
            auto base = reinterpret_cast<const std::byte*>(p) - offsetof(Chunk, Storage);
            return reinterpret_cast<const Chunk*>(base);
        }
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
        // Do not do this:
        //  Heap* heap = new Heap();
        // As that will value-initialize (zero-initialize) Heap.
        Heap* heap = new Heap;

        // Push onto the free-list in index order so the head ends up being the last element:
        // LIFO allocation will then walk backward through the array (often slightly nicer
        // locality).
        Chunk* chunk = &heap->m_Chunks[0];
        for(std::size_t i = 0; i < ItemsPerHeap; ++i, ++chunk)
        {
            chunk->Next = Base::m_FreeList;
            chunk->Allocator = this;
            Base::m_FreeList = chunk;
        }

        heap->m_Next = m_Heaps;
        m_Heaps = heap;
        ++m_HeapCount;
    }

    Heap* m_Heaps{ nullptr };
    std::size_t m_HeapCount{ 0 };
};