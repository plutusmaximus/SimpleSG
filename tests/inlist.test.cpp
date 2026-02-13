#include <gtest/gtest.h>

#include "Error.h"
#include "inlist.h"

class Foo
{
public:

    inlist_node<Foo> ListNode1;
    inlist_node<Foo> ListNode2;
};

// Test default constructor
TEST(inlist, TestAll)
{
    inlist<Foo, &Foo::ListNode1> list1;
    inlist<Foo, &Foo::ListNode2> list2;
    EXPECT_EQ(list1.front(), nullptr);
    EXPECT_EQ(list2.front(), nullptr);

    Foo foos[3];

    list1.push_back(&foos[0]);
    EXPECT_EQ(list1.front(), &foos[0]);
    list1.push_back(&foos[1]);
    EXPECT_EQ(list1.front(), &foos[0]);
    EXPECT_EQ(list1.back(), &foos[1]);
    list1.push_front(&foos[2]);
    EXPECT_EQ(list1.front(), &foos[2]);
}