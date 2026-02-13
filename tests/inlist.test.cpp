#include <gtest/gtest.h>

#include "Error.h"
#include "inlist.h"

class Foo
{
public:

    inlist_node<Foo> ListNode1;
    inlist_node<Foo> ListNode2;
};

TEST(inlist, PushAndFrontBack)
{
    inlist<Foo, &Foo::ListNode1> list;
    Foo foos[3];

    list.push_back(&foos[0]);
    EXPECT_EQ(list.front(), &foos[0]);
    EXPECT_EQ(list.back(), &foos[0]);

    list.push_front(&foos[1]);
    EXPECT_EQ(list.front(), &foos[1]);
    EXPECT_EQ(list.back(), &foos[0]);

    list.push_back(&foos[2]);
    EXPECT_EQ(list.front(), &foos[1]);
    EXPECT_EQ(list.back(), &foos[2]);
}

TEST(inlist, MultipleListsIndependent)
{
    inlist<Foo, &Foo::ListNode1> list1;
    inlist<Foo, &Foo::ListNode2> list2;
    Foo foos[2];

    list1.push_back(&foos[0]);
    list2.push_back(&foos[0]);
    list2.push_back(&foos[1]);

    EXPECT_EQ(list1.size(), 1u);
    EXPECT_EQ(list2.size(), 2u);
    EXPECT_EQ(list1.front(), &foos[0]);
    EXPECT_EQ(list2.front(), &foos[0]);
    EXPECT_EQ(list2.back(), &foos[1]);

    list1.erase(list1.begin());
    EXPECT_TRUE(list1.empty());
    EXPECT_EQ(list2.size(), 2u);
    EXPECT_EQ(list2.front(), &foos[0]);
    EXPECT_EQ(list2.back(), &foos[1]);

    auto it = list2.begin();
    ++it; // foos[1]
    list2.erase(it);
    EXPECT_EQ(list2.size(), 1u);
    EXPECT_EQ(list2.front(), &foos[0]);
    EXPECT_EQ(list2.back(), &foos[0]);
}

TEST(inlist, SizeAndEmpty)
{
    inlist<Foo, &Foo::ListNode1> list;
    Foo foos[3];

    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0u);

    list.push_back(&foos[0]);
    EXPECT_FALSE(list.empty());
    EXPECT_EQ(list.size(), 1u);

    list.push_front(&foos[1]);
    EXPECT_EQ(list.size(), 2u);

    list.push_back(&foos[2]);
    EXPECT_EQ(list.size(), 3u);
}

TEST(inlist, IterationOrder)
{
    inlist<Foo, &Foo::ListNode1> list;
    Foo foos[3];

    list.push_back(&foos[0]);
    list.push_back(&foos[1]);
    list.push_back(&foos[2]);

    int index = 0;
    for(auto it = list.begin(); it != list.end(); ++it)
    {
        EXPECT_EQ(&(*it), &foos[index]);
        ++index;
    }
    EXPECT_EQ(index, 3);
}

TEST(inlist, ConstIteration)
{
    inlist<Foo, &Foo::ListNode1> list;
    Foo foos[2];

    list.push_back(&foos[0]);
    list.push_back(&foos[1]);

    const auto& clist = list;
    auto it = clist.cbegin();
    EXPECT_EQ(&(*it), &foos[0]);
    ++it;
    EXPECT_EQ(&(*it), &foos[1]);
    ++it;
    EXPECT_EQ(it, clist.cend());
}

TEST(inlist, EraseByIterator)
{
    inlist<Foo, &Foo::ListNode1> list;
    Foo foos[4];

    list.push_back(&foos[0]);
    list.push_back(&foos[1]);
    list.push_back(&foos[2]);
    list.push_back(&foos[3]);

    auto it = list.begin();
    ++it; // foos[1]
    it = list.erase(it);

    EXPECT_EQ(list.size(), 3u);
    EXPECT_EQ(list.front(), &foos[0]);
    EXPECT_EQ(list.back(), &foos[3]);
    EXPECT_EQ(&(*it), &foos[2]);

    list.erase(list.begin());
    EXPECT_EQ(list.size(), 2u);
    EXPECT_EQ(list.front(), &foos[2]);

    auto tailIt = list.begin();
    ++tailIt; // foos[3]
    tailIt = list.erase(tailIt);
    EXPECT_EQ(list.size(), 1u);
    EXPECT_EQ(list.back(), &foos[2]);
    EXPECT_EQ(tailIt, list.end());
}

TEST(inlist, EraseByConstIterator)
{
    inlist<Foo, &Foo::ListNode1> list;
    Foo foos[3];

    list.push_back(&foos[0]);
    list.push_back(&foos[1]);
    list.push_back(&foos[2]);

    const auto& clist = list;
    auto it = clist.cbegin();
    ++it; // foos[1]

    auto next = list.erase(it);
    EXPECT_EQ(list.size(), 2u);
    EXPECT_EQ(&(*next), &foos[2]);
}