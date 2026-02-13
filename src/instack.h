#pragma once

#include "Error.h"

#include <cstddef>
#include <iterator>

/// @brief Intrusive stack (LIFO) that stores existing objects without allocating.
///
/// Usage:
/// - Add an instack_node<T> member to your type.
/// - Instantiate instack<T, &T::YourNodeMember>.
/// - Use push() to add nodes and pop() to remove the top node.
/// - Iterate from top to bottom with begin()/end().
///
/// Example:
/// struct Job { instack_node<Job> Node; int id; };
/// instack<Job, &Job::Node> jobs;
/// Job a{ {}, 1 }, b{ {}, 2 };
/// jobs.push(&a);
/// jobs.push(&b);
/// Job* top = jobs.pop(); // b
///
/// Notes:
/// - Nodes must not be in multiple stacks using the same node member.
/// - A node is considered linked when its Next pointer is non-null.

/// @brief Node for an intrusive stack
template<typename T>
class instack_node
{
public:
    bool IsLinked() const { return Next != nullptr; }

    T* Next{ nullptr };
};

/// @brief Intrusive stack (LIFO)
/// T is the type of the objects stored in the stack, and NodeMember is a pointer to the
/// instack_node member of T that will be used for the stack links.
template<typename T, instack_node<T> T::* NodeMember>
class instack
{
public:
    instack() = default;
    instack(const instack&) = delete;
    instack& operator=(const instack&) = delete;
    instack(instack&& other) noexcept
    {
        m_Top = other.m_Top;
        m_Size = other.m_Size;

        other.m_Top = nullptr;
        other.m_Size = 0;
    }
    instack& operator=(instack&& other) noexcept
    {
        if(this != &other)
        {
            m_Top = other.m_Top;
            m_Size = other.m_Size;

            other.m_Top = nullptr;
            other.m_Size = 0;
        }
        return *this;
    }

    class iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        iterator() = default;
        explicit iterator(T* node)
            : m_Node(node)
        {
        }

        reference operator*() const { return *m_Node; }
        pointer operator->() const { return m_Node; }

        iterator& operator++()
        {
            eassert(m_Node != nullptr && "Cannot increment end iterator");
            m_Node = m_Node ? (m_Node->*NodeMember).Next : nullptr;
            return *this;
        }

        iterator operator++(int)
        {
            iterator tmp(*this);
            ++(*this);
            return tmp;
        }

        bool operator==(const iterator& other) const { return m_Node == other.m_Node; }
        bool operator!=(const iterator& other) const { return m_Node != other.m_Node; }

    private:
        friend class instack;
        T* node() const { return m_Node; }
        T* m_Node{ nullptr };
    };

    class const_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator() = default;
        explicit const_iterator(const T* node)
            : m_Node(node)
        {
        }
        const_iterator(const iterator& it)
            : m_Node(it.operator->())
        {
        }

        reference operator*() const { return *m_Node; }
        pointer operator->() const { return m_Node; }

        const_iterator& operator++()
        {
            eassert(m_Node != nullptr && "Cannot increment end iterator");
            m_Node = m_Node ? (const_cast<T*>(m_Node)->*NodeMember).Next : nullptr;
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator tmp(*this);
            ++(*this);
            return tmp;
        }

        bool operator==(const const_iterator& other) const { return m_Node == other.m_Node; }
        bool operator!=(const const_iterator& other) const { return m_Node != other.m_Node; }

    private:
        friend class instack;
        const T* node() const { return m_Node; }
        const T* m_Node{ nullptr };
    };

    void push(T* node)
    {
        eassert(node != nullptr && "Node cannot be null");
        auto& stackNode = node->*NodeMember;
        eassert(stackNode.Next == nullptr && "Node is already in a stack");

        stackNode.Next = m_Top;

        m_Top = node;
        ++m_Size;
    }

    T* pop()
    {
        if(!m_Top)
        {
            return nullptr;
        }

        T* node = m_Top;
        auto& stackNode = node->*NodeMember;
        T* next = stackNode.Next;

        m_Top = next;

        stackNode.Next = nullptr;
        --m_Size;

        return node;
    }

    T* top() const { return m_Top; }

    std::size_t size() const { return m_Size; }

    bool empty() const { return m_Size == 0; }

    iterator begin() { return iterator(m_Top); }
    iterator end() { return iterator(nullptr); }
    const_iterator begin() const { return const_iterator(m_Top); }
    const_iterator end() const { return const_iterator(nullptr); }
    const_iterator cbegin() const { return const_iterator(m_Top); }
    const_iterator cend() const { return const_iterator(nullptr); }

private:
    T* m_Top{ nullptr };
    std::size_t m_Size{ 0 };
};
