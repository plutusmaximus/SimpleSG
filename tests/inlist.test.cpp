#include <gtest/gtest.h>

#include "Error.h"
#include "inlist.h"

class Item
{
public:

    inlist_node<Item> ListNode1;
    inlist_node<Item> ListNode2;
};

TEST(inlist, PushAndFrontBack)
{
    inlist<Item, &Item::ListNode1> list;
    Item items[3];

    list.push_back(&items[0]);
    EXPECT_EQ(list.front(), &items[0]);
    EXPECT_EQ(list.back(), &items[0]);

    list.push_front(&items[1]);
    EXPECT_EQ(list.front(), &items[1]);
    EXPECT_EQ(list.back(), &items[0]);

    list.push_back(&items[2]);
    EXPECT_EQ(list.front(), &items[1]);
    EXPECT_EQ(list.back(), &items[2]);
}

TEST(inlist, MultipleListsIndependent)
{
    inlist<Item, &Item::ListNode1> list1;
    inlist<Item, &Item::ListNode2> list2;
    Item items[2];

    list1.push_back(&items[0]);
    list2.push_back(&items[0]);
    list2.push_back(&items[1]);

    EXPECT_EQ(list1.size(), 1u);
    EXPECT_EQ(list2.size(), 2u);
    EXPECT_EQ(list1.front(), &items[0]);
    EXPECT_EQ(list2.front(), &items[0]);
    EXPECT_EQ(list2.back(), &items[1]);

    list1.erase(list1.begin());
    EXPECT_TRUE(list1.empty());
    EXPECT_EQ(list2.size(), 2u);
    EXPECT_EQ(list2.front(), &items[0]);
    EXPECT_EQ(list2.back(), &items[1]);

    auto it = list2.begin();
    ++it; // items[1]
    list2.erase(it);
    EXPECT_EQ(list2.size(), 1u);
    EXPECT_EQ(list2.front(), &items[0]);
    EXPECT_EQ(list2.back(), &items[0]);
}

TEST(inlist, SizeAndEmpty)
{
    inlist<Item, &Item::ListNode1> list;
    Item items[3];

    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0u);

    list.push_back(&items[0]);
    EXPECT_FALSE(list.empty());
    EXPECT_EQ(list.size(), 1u);

    list.push_front(&items[1]);
    EXPECT_EQ(list.size(), 2u);

    list.push_back(&items[2]);
    EXPECT_EQ(list.size(), 3u);
}

TEST(inlist, IterationOrder)
{
    inlist<Item, &Item::ListNode1> list;
    Item items[3];

    list.push_back(&items[0]);
    list.push_back(&items[1]);
    list.push_back(&items[2]);

    int index = 0;
    for(auto it = list.begin(); it != list.end(); ++it)
    {
        EXPECT_EQ(&(*it), &items[index]);
        ++index;
    }
    EXPECT_EQ(index, 3);
}

TEST(inlist, ConstIteration)
{
    inlist<Item, &Item::ListNode1> list;
    Item items[2];

    list.push_back(&items[0]);
    list.push_back(&items[1]);

    const auto& clist = list;
    auto it = clist.cbegin();
    EXPECT_EQ(&(*it), &items[0]);
    ++it;
    EXPECT_EQ(&(*it), &items[1]);
    ++it;
    EXPECT_EQ(it, clist.cend());
}

TEST(inlist, EraseByIterator)
{
    inlist<Item, &Item::ListNode1> list;
    Item items[4];

    list.push_back(&items[0]);
    list.push_back(&items[1]);
    list.push_back(&items[2]);
    list.push_back(&items[3]);

    auto it = list.begin();
    ++it; // items[1]
    it = list.erase(it);

    EXPECT_EQ(list.size(), 3u);
    EXPECT_EQ(list.front(), &items[0]);
    EXPECT_EQ(list.back(), &items[3]);
    EXPECT_EQ(&(*it), &items[2]);

    list.erase(list.begin());
    EXPECT_EQ(list.size(), 2u);
    EXPECT_EQ(list.front(), &items[2]);

    auto tailIt = list.begin();
    ++tailIt; // items[3]
    tailIt = list.erase(tailIt);
    EXPECT_EQ(list.size(), 1u);
    EXPECT_EQ(list.back(), &items[2]);
    EXPECT_EQ(tailIt, list.end());
}

TEST(inlist, EraseByConstIterator)
{
    inlist<Item, &Item::ListNode1> list;
    Item items[3];

    list.push_back(&items[0]);
    list.push_back(&items[1]);
    list.push_back(&items[2]);

    const auto& clist = list;
    auto it = clist.cbegin();
    ++it; // items[1]

    auto next = list.erase(it);
    EXPECT_EQ(list.size(), 2u);
    EXPECT_EQ(&(*next), &items[2]);
}