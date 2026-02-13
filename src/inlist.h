#pragma once

#include "Error.h"

#include <cstddef>
#include <iterator>

/// @brief Intrusive doubly-linked list that stores existing objects without allocating.
///
/// Usage:
/// - Add an inlist_node<T> member to your type.
/// - Instantiate inlist<T, &T::YourNodeMember>.
/// - Use push_front()/push_back() to add nodes and erase()/pop_front()/pop_back() to remove.
/// - Iterate from head to tail with begin()/end().
///
/// Example:
/// struct Item { inlist_node<Item> Node; int id; };
/// inlist<Item, &Item::Node> items;
/// Item a{ {}, 1 }, b{ {}, 2 };
/// items.push_back(&a);
/// items.push_back(&b);
/// items.pop_front(); // a
///
/// Notes:
/// - Nodes must not be in multiple lists using the same node member.
/// - A node is considered linked when its Next or Prev pointer is non-null.

/// @brief Node for an intrusive linked list
template<typename T>
class inlist_node
{
public:

    bool IsLinked() const { return Next != nullptr || Prev != nullptr; }

    T* Next{ nullptr };
    T* Prev{ nullptr };
};

/// @brief Intrusive linked list
/// T is the type of the objects stored in the list, and NodeMember is a pointer to the inlist_node
/// member of T that will be used for the list links.
template<typename T, inlist_node<T> T::* NodeMember>
class inlist
{
public:
    inlist() = default;
    inlist(const inlist&) = delete;
    inlist& operator=(const inlist&) = delete;
    inlist(inlist&& other) noexcept
    {
        m_Head = other.m_Head;
        m_Tail = other.m_Tail;
        m_Size = other.m_Size;

        other.m_Head = nullptr;
        other.m_Tail = nullptr;
        other.m_Size = 0;
    }
    inlist& operator=(inlist&& other) noexcept
    {
        if(this != &other)
        {
            m_Head = other.m_Head;
            m_Tail = other.m_Tail;
            m_Size = other.m_Size;

            other.m_Head = nullptr;
            other.m_Tail = nullptr;
            other.m_Size = 0;
        }
        return *this;
    }

    class iterator
    {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
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

        iterator& operator--()
        {
            eassert(m_Node != nullptr && "Cannot decrement end iterator");
            m_Node = m_Node ? (m_Node->*NodeMember).Prev : nullptr;
            return *this;
        }

        iterator operator--(int)
        {
            iterator tmp(*this);
            --(*this);
            return tmp;
        }

        bool operator==(const iterator& other) const { return m_Node == other.m_Node; }
        bool operator!=(const iterator& other) const { return m_Node != other.m_Node; }

    private:
        friend class inlist;
        T* node() const { return m_Node; }
        T* m_Node{ nullptr };
    };

    class const_iterator
    {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
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

        const_iterator& operator--()
        {
            eassert(m_Node != nullptr && "Cannot decrement end iterator");
            m_Node = m_Node ? (const_cast<T*>(m_Node)->*NodeMember).Prev : nullptr;
            return *this;
        }

        const_iterator operator--(int)
        {
            const_iterator tmp(*this);
            --(*this);
            return tmp;
        }

        bool operator==(const const_iterator& other) const { return m_Node == other.m_Node; }
        bool operator!=(const const_iterator& other) const { return m_Node != other.m_Node; }

    private:
        friend class inlist;
        const T* node() const { return m_Node; }
        const T* m_Node{ nullptr };
    };

    void push_back(T* node)
    {
        eassert(node != nullptr && "Node cannot be null");
        auto& listNode = node->*NodeMember;
        eassert(
            listNode.Next == nullptr && listNode.Prev == nullptr && "Node is already in a list");

        listNode.Next = nullptr;
        listNode.Prev = m_Tail;

        if(m_Tail)
        {
            (m_Tail->*NodeMember).Next = node;
        }

        m_Tail = node;

        if(!m_Head)
        {
            m_Head = node;
        }

        ++m_Size;
    }

    void push_front(T* node)
    {
        eassert(node != nullptr && "Node cannot be null");
        auto& listNode = node->*NodeMember;
        eassert(
            listNode.Next == nullptr && listNode.Prev == nullptr && "Node is already in a list");

        listNode.Next = m_Head;
        listNode.Prev = nullptr;

        if(m_Head)
        {
            (m_Head->*NodeMember).Prev = node;
        }

        m_Head = node;

        if(!m_Tail)
        {
            m_Tail = node;
        }

        ++m_Size;
    }

    T* pop_front()
    {
        if(!m_Head)
        {
            return nullptr;
        }

        T* node = m_Head;
        erase(iterator(node));
        return node;
    }

    T* pop_back()
    {
        if(!m_Tail)
        {
            return nullptr;
        }

        T* node = m_Tail;
        erase(iterator(node));
        return node;
    }

    T* front() const { return m_Head; }

    T* back() const { return m_Tail; }

    iterator erase(iterator pos)
    {
        T* node = pos.node();
        eassert(node != nullptr && "Cannot erase end iterator");

        auto& listNode = node->*NodeMember;
        T* next = listNode.Next;
        T* prev = listNode.Prev;

        if(prev)
        {
            (prev->*NodeMember).Next = next;
        }
        else
        {
            m_Head = next;
        }

        if(next)
        {
            (next->*NodeMember).Prev = prev;
        }
        else
        {
            m_Tail = prev;
        }

        listNode.Next = nullptr;
        listNode.Prev = nullptr;
        --m_Size;

        return iterator(next);
    }

    iterator erase(T* node) { return erase(iterator(node)); }

    iterator erase(const_iterator pos) { return erase(iterator(const_cast<T*>(pos.node()))); }

    std::size_t size() const { return m_Size; }

    bool empty() const { return m_Size == 0; }

    iterator begin() { return iterator(m_Head); }
    iterator end() { return iterator(nullptr); }
    const_iterator begin() const { return const_iterator(m_Head); }
    const_iterator end() const { return const_iterator(nullptr); }
    const_iterator cbegin() const { return const_iterator(m_Head); }
    const_iterator cend() const { return const_iterator(nullptr); }

private:
    T* m_Head{ nullptr };
    T* m_Tail{ nullptr };
    std::size_t m_Size{ 0 };
};