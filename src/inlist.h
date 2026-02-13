#pragma once

/// @brief Node for an intrusive linked list
template<typename T>
class inlist_node
{
public:

    T* Next{nullptr};
    T* Prev{nullptr};
};

/// @brief Intrusive linked list
/// T is the type of the objects stored in the list, and NodeMember is a pointer to the inlist_node
/// member of T that will be used for the list links.
template<typename T, inlist_node<T> T::* NodeMember>
class inlist
{
public:

    void push_back(T* node)
    {
        auto& listNode = node->*NodeMember;
        eassert(listNode.Next == nullptr && listNode.Prev == nullptr && "Node is already in a list");

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
    }

    void push_front(T* node)
    {
        auto& listNode = node->*NodeMember;
        eassert(listNode.Next == nullptr && listNode.Prev == nullptr && "Node is already in a list");

        listNode.Next = m_Head;
        listNode.Prev = nullptr;

        if(m_Head)
        {
            (m_Head->*NodeMember).Prev = node;
        }

        m_Head = node;
    }

    T* front() const
    {
        return m_Head;
    }

    T* back() const
    {
        return m_Tail;
    }
private:

    T* m_Head{nullptr};
    T* m_Tail{nullptr};
};