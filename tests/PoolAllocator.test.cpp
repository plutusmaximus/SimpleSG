#include <gtest/gtest.h>

#include "../src/PoolAllocator.h"

namespace
{
    struct Tracked
    {
        static int ctorCount;
        static int dtorCount;
        static int valueSum;

        int value;

        explicit Tracked(int v)
            : value(v)
        {
            ++ctorCount;
            valueSum += v;
        }

        ~Tracked()
        {
            ++dtorCount;
            valueSum -= value;
        }

        static void Reset()
        {
            ctorCount = 0;
            dtorCount = 0;
            valueSum = 0;
        }
    };

    int Tracked::ctorCount = 0;
    int Tracked::dtorCount = 0;
    int Tracked::valueSum = 0;
}

TEST(PoolAllocator, AllocConstructsAndFreeDestroys)
{
    Tracked::Reset();

    PoolAllocator<Tracked, 4> pool;
    Tracked* obj = pool.New(5);

    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(Tracked::ctorCount, 1);
    EXPECT_EQ(Tracked::dtorCount, 0);
    EXPECT_EQ(Tracked::valueSum, 5);

    pool.Delete(obj);

    EXPECT_EQ(Tracked::ctorCount, 1);
    EXPECT_EQ(Tracked::dtorCount, 1);
    EXPECT_EQ(Tracked::valueSum, 0);
}

TEST(PoolAllocator, FreeNullptrIsNoOp)
{
    Tracked::Reset();

    PoolAllocator<Tracked, 2> pool;
    pool.Delete(nullptr);

    EXPECT_EQ(Tracked::ctorCount, 0);
    EXPECT_EQ(Tracked::dtorCount, 0);
    EXPECT_EQ(Tracked::valueSum, 0);
}

TEST(PoolAllocator, ReusesFreedChunk)
{
    Tracked::Reset();

    PoolAllocator<Tracked, 4> pool;
    Tracked* first = pool.New(1);
    Tracked* second = pool.New(2);

    pool.Delete(second);

    Tracked* reused = pool.New(3);

    EXPECT_EQ(reused, second);

    pool.Delete(reused);
    pool.Delete(first);

    EXPECT_EQ(Tracked::ctorCount, 3);
    EXPECT_EQ(Tracked::dtorCount, 3);
    EXPECT_EQ(Tracked::valueSum, 0);
}

TEST(PoolAllocator, AllocatesAdditionalHeapWhenExhausted)
{
    Tracked::Reset();

    PoolAllocator<Tracked, 2> pool;
    Tracked* a = pool.New(10);
    Tracked* b = pool.New(20);
    Tracked* c = pool.New(30);

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(b, c);

    pool.Delete(c);
    pool.Delete(b);
    pool.Delete(a);

    EXPECT_EQ(Tracked::ctorCount, 3);
    EXPECT_EQ(Tracked::dtorCount, 3);
    EXPECT_EQ(Tracked::valueSum, 0);
}
