#include <gtest/gtest.h>

#include "Error.h"
#include "instack.h"

namespace
{
    class Item
    {
    public:
        instack_node<Item> StackNode;
    };
}

TEST(instack, PushAndTop)
{
    instack<Item, &Item::StackNode> stack;
    Item items[3];

    stack.push(&items[0]);
    EXPECT_EQ(stack.top(), &items[0]);

    stack.push(&items[1]);
    EXPECT_EQ(stack.top(), &items[1]);

    stack.push(&items[2]);
    EXPECT_EQ(stack.top(), &items[2]);
}

TEST(instack, SizeAndEmpty)
{
    instack<Item, &Item::StackNode> stack;
    Item items[2];

    EXPECT_TRUE(stack.empty());
    EXPECT_EQ(stack.size(), 0u);

    stack.push(&items[0]);
    EXPECT_FALSE(stack.empty());
    EXPECT_EQ(stack.size(), 1u);

    stack.push(&items[1]);
    EXPECT_EQ(stack.size(), 2u);
}

TEST(instack, PopOrder)
{
    instack<Item, &Item::StackNode> stack;
    Item items[3];

    stack.push(&items[0]);
    stack.push(&items[1]);
    stack.push(&items[2]);

    EXPECT_EQ(stack.pop(), &items[2]);
    EXPECT_EQ(stack.top(), &items[1]);
    EXPECT_EQ(stack.size(), 2u);

    EXPECT_EQ(stack.pop(), &items[1]);
    EXPECT_EQ(stack.top(), &items[0]);
    EXPECT_EQ(stack.size(), 1u);

    EXPECT_EQ(stack.pop(), &items[0]);
    EXPECT_TRUE(stack.empty());
    EXPECT_EQ(stack.top(), nullptr);
}

TEST(instack, IterationOrder)
{
    instack<Item, &Item::StackNode> stack;
    Item items[3];

    stack.push(&items[0]);
    stack.push(&items[1]);
    stack.push(&items[2]);

    Item* expected[] = { &items[2], &items[1], &items[0] };
    int index = 0;
    for(auto it = stack.begin(); it != stack.end(); ++it)
    {
        EXPECT_EQ(&(*it), expected[index]);
        ++index;
    }
    EXPECT_EQ(index, 3);
}

TEST(instack, ConstIteration)
{
    instack<Item, &Item::StackNode> stack;
    Item items[2];

    stack.push(&items[0]);
    stack.push(&items[1]);

    const auto& cstack = stack;
    auto it = cstack.cbegin();
    EXPECT_EQ(&(*it), &items[1]);
    ++it;
    EXPECT_EQ(&(*it), &items[0]);
    ++it;
    EXPECT_EQ(it, cstack.cend());
}

TEST(instack, PopEmpty)
{
    instack<Item, &Item::StackNode> stack;
    EXPECT_EQ(stack.pop(), nullptr);
    EXPECT_TRUE(stack.empty());
}

TEST(instack, MultipleStacksIndependent)
{
    instack<Item, &Item::StackNode> stack1;
    instack<Item, &Item::StackNode> stack2;
    Item items[2];

    stack1.push(&items[0]);
    stack2.push(&items[1]);

    EXPECT_EQ(stack1.top(), &items[0]);
    EXPECT_EQ(stack2.top(), &items[1]);

    EXPECT_EQ(stack1.pop(), &items[0]);
    EXPECT_EQ(stack2.pop(), &items[1]);
    EXPECT_TRUE(stack1.empty());
    EXPECT_TRUE(stack2.empty());
}
