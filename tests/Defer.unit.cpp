#include <gtest/gtest.h>

#include "Defer.h"

TEST(Defer, RunsOnScopeExit)
{
    bool called = false;
    {
        auto guard = Defer{ [&called]() { called = true; } };
        EXPECT_FALSE(called);
    }
    EXPECT_TRUE(called);
}

TEST(Defer, ReleasePreventsExecution)
{
    bool called = false;
    {
        auto guard = Defer{ [&called]() { called = true; } };
        guard.release();
    }
    EXPECT_FALSE(called);
}

TEST(Defer, MoveTransfersResponsibility)
{
    int counter = 0;
    {
        auto guard1 = Defer{ [&counter]() { ++counter; } };
        {
            auto guard2 = std::move(guard1);
            EXPECT_EQ(counter, 0);
        }
        EXPECT_EQ(counter, 1);
    }
    EXPECT_EQ(counter, 1);
}
