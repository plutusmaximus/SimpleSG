#include <gtest/gtest.h>

#include "scope_exit.h"

TEST(scope_exit, RunsOnScopeExit)
{
    bool called = false;
    {
        auto guard = scope_exit{ [&called]() { called = true; } };
        EXPECT_FALSE(called);
    }
    EXPECT_TRUE(called);
}

TEST(scope_exit, ReleasePreventsExecution)
{
    bool called = false;
    {
        auto guard = scope_exit{ [&called]() { called = true; } };
        guard.release();
    }
    EXPECT_FALSE(called);
}

TEST(scope_exit, MoveTransfersResponsibility)
{
    int counter = 0;
    {
        auto guard1 = scope_exit{ [&counter]() { ++counter; } };
        {
            auto guard2 = std::move(guard1);
            EXPECT_EQ(counter, 0);
        }
        EXPECT_EQ(counter, 1);
    }
    EXPECT_EQ(counter, 1);
}
