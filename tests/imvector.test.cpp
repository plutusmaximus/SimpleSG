#include <gtest/gtest.h>
#include <vector>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iterator>

#include "../src/imvector.h"

// Test default constructor
TEST(ImvectorTest, DefaultConstructor)
{
    imvector<int> v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0);
    EXPECT_EQ(v.capacity(), 0);
    EXPECT_NE(v.data(), nullptr);  // data() is non-null even when empty
}

// Test initializer_list constructor
TEST(ImvectorTest, InitializerListConstructor)
{
    imvector<int> v = {1, 2, 3, 4, 5};
    EXPECT_EQ(v.size(), 5);
    EXPECT_FALSE(v.empty());
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 3);
    EXPECT_EQ(v[3], 4);
    EXPECT_EQ(v[4], 5);
}

// Test empty initializer_list
TEST(ImvectorTest, EmptyInitializerList)
{
    imvector<int> v = {};
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0);
    EXPECT_NE(v.data(), nullptr);  // data() is non-null even when empty
}

// Test iterator constructor
TEST(ImvectorTest, IteratorConstructor)
{
    std::vector<int> src = {10, 20, 30};
    imvector<int> v(src.begin(), src.end());
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v[1], 20);
    EXPECT_EQ(v[2], 30);
}

// Test span constructor
TEST(ImvectorTest, SpanConstructor)
{
    std::vector<int> src = {5, 10, 15, 20};
    std::span<const int> sp(src);
    imvector<int> v(sp);
    EXPECT_EQ(v.size(), 4);
    EXPECT_EQ(v[0], 5);
    EXPECT_EQ(v[3], 20);
}

// Test std::vector constructor
TEST(ImvectorTest, StdVectorConstructor)
{
    std::vector<int> src = {100, 200, 300};
    imvector<int> v(src);
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0], 100);
    EXPECT_EQ(v[1], 200);
    EXPECT_EQ(v[2], 300);
}

// Test fill constructor
TEST(ImvectorTest, FillConstructor)
{
    imvector<int> v(5, 42);
    EXPECT_EQ(v.size(), 5);
    for (size_t i = 0; i < v.size(); ++i)
    {
        EXPECT_EQ(v[i], 42);
    }
}

// Test fill constructor with zero count
TEST(ImvectorTest, FillConstructorZeroCount)
{
    imvector<int> v(0, 42);
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0);
    EXPECT_NE(v.data(), nullptr);  // data() is non-null even when empty
}

// Test copy constructor
TEST(ImvectorTest, CopyConstructor)
{
    imvector<int> v1 = {1, 2, 3};
    imvector<int> v2(v1);
    
    EXPECT_EQ(v1.size(), v2.size());
    EXPECT_EQ(v1[0], v2[0]);
    EXPECT_EQ(v1[1], v2[1]);
    EXPECT_EQ(v1[2], v2[2]);
    
    // Should share the same data pointer (reference counting)
    EXPECT_EQ(v1.data(), v2.data());
}

// Test copy assignment
TEST(ImvectorTest, CopyAssignment)
{
    imvector<int> v1 = {10, 20, 30};
    imvector<int> v2;
    v2 = v1;
    
    EXPECT_EQ(v1.size(), v2.size());
    EXPECT_EQ(v1[0], v2[0]);
    EXPECT_EQ(v1[1], v2[1]);
    EXPECT_EQ(v1[2], v2[2]);
    
    // Should share the same data pointer
    EXPECT_EQ(v1.data(), v2.data());
}

// Test self-assignment
TEST(ImvectorTest, SelfAssignment)
{
    imvector<int> v = {1, 2, 3};
    v = v;
    
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 3);
}

// Test move constructor
TEST(ImvectorTest, MoveConstructor)
{
    imvector<int> v1 = {1, 2, 3};
    const int* original_data = v1.data();
    
    imvector<int> v2(std::move(v1));
    
    EXPECT_EQ(v2.size(), 3);
    EXPECT_EQ(v2[0], 1);
    EXPECT_EQ(v2[1], 2);
    EXPECT_EQ(v2[2], 3);
    EXPECT_EQ(v2.data(), original_data);
    
    // v1 should be empty after move
    EXPECT_TRUE(v1.empty());
    EXPECT_EQ(v1.size(), 0);
}

// Test move assignment
TEST(ImvectorTest, MoveAssignment)
{
    imvector<int> v1 = {10, 20, 30};
    const int* original_data = v1.data();
    imvector<int> v2;
    
    v2 = std::move(v1);
    
    EXPECT_EQ(v2.size(), 3);
    EXPECT_EQ(v2[0], 10);
    EXPECT_EQ(v2[1], 20);
    EXPECT_EQ(v2[2], 30);
    EXPECT_EQ(v2.data(), original_data);
    
    // v1 should be empty after move
    EXPECT_TRUE(v1.empty());
}

// Test element access with operator[]
TEST(ImvectorTest, ElementAccess)
{
    imvector<int> v = {5, 10, 15, 20, 25};
    EXPECT_EQ(v[0], 5);
    EXPECT_EQ(v[1], 10);
    EXPECT_EQ(v[2], 15);
    EXPECT_EQ(v[3], 20);
    EXPECT_EQ(v[4], 25);
}

// Test at() method with valid index
TEST(ImvectorTest, AtMethod)
{
    imvector<int> v = {10, 20, 30};
    EXPECT_EQ(v.at(0), 10);
    EXPECT_EQ(v.at(1), 20);
    EXPECT_EQ(v.at(2), 30);
}

// Test at() method with invalid index triggers fail-fast
TEST(ImvectorTest, AtMethodOutOfRange)
{
    imvector<int> v = {10, 20, 30};
    
    // at() uses require() which calls IMVECTOR_FAIL_FAST (std::abort by default)
    EXPECT_DEATH(v.at(3), "");
    EXPECT_DEATH(v.at(100), "");
}

// Test front() method
TEST(ImvectorTest, FrontMethod)
{
    imvector<int> v = {100, 200, 300};
    EXPECT_EQ(v.front(), 100);
}

// Test front() on empty vector triggers fail-fast
TEST(ImvectorTest, FrontMethodEmpty)
{
    imvector<int> v;
    EXPECT_DEATH(v.front(), "");
}

// Test back() method
TEST(ImvectorTest, BackMethod)
{
    imvector<int> v = {100, 200, 300};
    EXPECT_EQ(v.back(), 300);
}

// Test back() on empty vector triggers fail-fast
TEST(ImvectorTest, BackMethodEmpty)
{
    imvector<int> v;
    EXPECT_DEATH(v.back(), "");
}

// Test data() method
TEST(ImvectorTest, DataMethod)
{
    imvector<int> v = {1, 2, 3};
    const int* p = v.data();
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(p[0], 1);
    EXPECT_EQ(p[1], 2);
    EXPECT_EQ(p[2], 3);
}

// Test data() on empty vector is non-null
TEST(ImvectorTest, DataMethodEmpty)
{
    imvector<int> v;
    const int* p = v.data();
    EXPECT_NE(p, nullptr);  // data() is non-null even when empty
}

// Test span() method
TEST(ImvectorTest, SpanMethod)
{
    imvector<int> v = {5, 10, 15};
    auto sp = v.span();
    EXPECT_EQ(sp.size(), 3);
    EXPECT_EQ(sp[0], 5);
    EXPECT_EQ(sp[1], 10);
    EXPECT_EQ(sp[2], 15);
}

// Test explicit span conversion operator
TEST(ImvectorTest, ExplicitSpanConversion)
{
    imvector<int> v = {7, 14, 21};
    std::span<const int> sp = static_cast<std::span<const int>>(v);
    EXPECT_EQ(sp.size(), 3);
    EXPECT_EQ(sp[0], 7);
    EXPECT_EQ(sp[1], 14);
    EXPECT_EQ(sp[2], 21);
}

// Test iterators
TEST(ImvectorTest, Iterators)
{
    imvector<int> v = {1, 2, 3, 4, 5};
    
    std::vector<int> collected;
    for (auto it = v.begin(); it != v.end(); ++it)
    {
        collected.push_back(*it);
    }
    
    EXPECT_EQ(collected.size(), 5);
    EXPECT_EQ(collected[0], 1);
    EXPECT_EQ(collected[4], 5);
}

// Test const iterators
TEST(ImvectorTest, ConstIterators)
{
    imvector<int> v = {10, 20, 30};
    
    std::vector<int> collected;
    for (auto it = v.cbegin(); it != v.cend(); ++it)
    {
        collected.push_back(*it);
    }
    
    EXPECT_EQ(collected.size(), 3);
    EXPECT_EQ(collected[0], 10);
    EXPECT_EQ(collected[2], 30);
}

// Test range-based for loop
TEST(ImvectorTest, RangeBasedFor)
{
    imvector<int> v = {2, 4, 6, 8};
    
    std::vector<int> collected;
    for (const auto& val : v)
    {
        collected.push_back(val);
    }
    
    EXPECT_EQ(collected.size(), 4);
    EXPECT_EQ(collected[0], 2);
    EXPECT_EQ(collected[3], 8);
}

// Test reverse iterators
TEST(ImvectorTest, ReverseIterators)
{
    imvector<int> v = {1, 2, 3, 4};
    
    std::vector<int> collected;
    for (auto it = v.rbegin(); it != v.rend(); ++it)
    {
        collected.push_back(*it);
    }
    
    EXPECT_EQ(collected.size(), 4);
    EXPECT_EQ(collected[0], 4);
    EXPECT_EQ(collected[1], 3);
    EXPECT_EQ(collected[2], 2);
    EXPECT_EQ(collected[3], 1);
}

// Test empty() and size()
TEST(ImvectorTest, EmptyAndSize)
{
    imvector<int> v1;
    EXPECT_TRUE(v1.empty());
    EXPECT_EQ(v1.size(), 0);
    
    imvector<int> v2 = {1};
    EXPECT_FALSE(v2.empty());
    EXPECT_EQ(v2.size(), 1);
    
    imvector<int> v3 = {1, 2, 3};
    EXPECT_FALSE(v3.empty());
    EXPECT_EQ(v3.size(), 3);
}

// Test capacity() equals size()
TEST(ImvectorTest, Capacity)
{
    imvector<int> v = {1, 2, 3, 4};
    EXPECT_EQ(v.capacity(), v.size());
    EXPECT_EQ(v.capacity(), 4);
}

// Test with non-trivial types (strings)
TEST(ImvectorTest, StringType)
{
    imvector<std::string> v = {"hello", "world", "test"};
    
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0], "hello");
    EXPECT_EQ(v[1], "world");
    EXPECT_EQ(v[2], "test");
}

// Test copy shares storage
TEST(ImvectorTest, CopySharesStorage)
{
    imvector<int> v1 = {1, 2, 3};
    const int* p1 = v1.data();
    
    imvector<int> v2 = v1;
    const int* p2 = v2.data();
    
    // Both should point to the same data
    EXPECT_EQ(p1, p2);
    
    imvector<int> v3;
    v3 = v1;
    const int* p3 = v3.data();
    
    EXPECT_EQ(p1, p3);
}

// Test move transfers ownership
TEST(ImvectorTest, MoveTransfersOwnership)
{
    imvector<int> v1 = {1, 2, 3};
    const int* p1 = v1.data();
    
    imvector<int> v2 = std::move(v1);
    
    // v2 should have the original data
    EXPECT_EQ(v2.data(), p1);
    
    // v1 should be empty after move
    EXPECT_EQ(v1.size(), 0);
    EXPECT_TRUE(v1.empty());
}

// Test with std::algorithm functions
TEST(ImvectorTest, StdAlgorithms)
{
    imvector<int> v = {5, 2, 8, 1, 9, 3};
    
    // Test std::find
    auto it = std::find(v.begin(), v.end(), 8);
    EXPECT_NE(it, v.end());
    EXPECT_EQ(*it, 8);
    
    // Test std::count
    size_t count = std::count(v.begin(), v.end(), 2);
    EXPECT_EQ(count, 1);
    
    // Test std::accumulate
    int sum = std::accumulate(v.begin(), v.end(), 0);
    EXPECT_EQ(sum, 5 + 2 + 8 + 1 + 9 + 3);
    
    // Test std::max_element
    auto max_it = std::max_element(v.begin(), v.end());
    EXPECT_EQ(*max_it, 9);
}

// Test constructing from empty range
TEST(ImvectorTest, EmptyRangeConstructor)
{
    std::vector<int> empty_vec;
    imvector<int> v(empty_vec.begin(), empty_vec.end());
    
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0);
}

// Test with custom struct
struct Point
{
    int x, y;
    
    bool operator==(const Point& other) const
    {
        return x == other.x && y == other.y;
    }
    
    auto operator<=>(const Point& other) const = default;
};

TEST(ImvectorTest, CustomStruct)
{
    imvector<Point> v = {{1, 2}, {3, 4}, {5, 6}};
    
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0].x, 1);
    EXPECT_EQ(v[0].y, 2);
    EXPECT_EQ(v[1].x, 3);
    EXPECT_EQ(v[1].y, 4);
    EXPECT_EQ(v[2].x, 5);
    EXPECT_EQ(v[2].y, 6);
}

// Test multiple copies share same data
TEST(ImvectorTest, MultipleCopiesShareData)
{
    imvector<int> v1 = {1, 2, 3, 4, 5};
    imvector<int> v2 = v1;
    imvector<int> v3 = v2;
    imvector<int> v4 = v3;
    
    const int* p1 = v1.data();
    const int* p2 = v2.data();
    const int* p3 = v3.data();
    const int* p4 = v4.data();
    
    // All should point to the same memory
    EXPECT_EQ(p1, p2);
    EXPECT_EQ(p2, p3);
    EXPECT_EQ(p3, p4);
    
    // All should have the same values
    for (size_t i = 0; i < 5; ++i)
    {
        EXPECT_EQ(v1[i], v2[i]);
        EXPECT_EQ(v2[i], v3[i]);
        EXPECT_EQ(v3[i], v4[i]);
    }
}

// Test empty vector operations
TEST(ImvectorTest, EmptyVectorOperations)
{
    imvector<int> v1;
    imvector<int> v2;
    
    EXPECT_EQ(v1.begin(), v1.end());
    EXPECT_EQ(v1.rbegin(), v1.rend());
}

// Test iterator on empty vector
TEST(ImvectorTest, EmptyVectorIterator)
{
    imvector<int> v;
    
    int count = 0;
    for (auto val : v)
    {
        (void)val;
        ++count;
    }
    
    EXPECT_EQ(count, 0);
}

// Test with large vector
TEST(ImvectorTest, LargeVector)
{
    std::vector<int> src(10000);
    for (size_t i = 0; i < src.size(); ++i)
    {
        src[i] = static_cast<int>(i);
    }
    
    imvector<int> v(src);
    
    EXPECT_EQ(v.size(), 10000);
    EXPECT_EQ(v[0], 0);
    EXPECT_EQ(v[5000], 5000);
    EXPECT_EQ(v[9999], 9999);
}

// Test with input iterator (istream_iterator) to exercise input-iterator make_block_from_range
TEST(ImvectorTest, InputIteratorConstructor)
{
    // std::istream_iterator is an input iterator (not a forward iterator)
    // This exercises the make_block_from_range overload for input iterators (line 184 of imvector.h)
    std::istringstream iss("10 20 30 40 50");
    std::istream_iterator<int> first(iss);
    std::istream_iterator<int> last;
    
    imvector<int> v(first, last);
    
    EXPECT_EQ(v.size(), 5);
    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v[1], 20);
    EXPECT_EQ(v[2], 30);
    EXPECT_EQ(v[3], 40);
    EXPECT_EQ(v[4], 50);
}

// Test with empty input iterator range
TEST(ImvectorTest, EmptyInputIteratorConstructor)
{
    std::istringstream iss("");
    std::istream_iterator<int> first(iss);
    std::istream_iterator<int> last;
    
    imvector<int> v(first, last);
    
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0);
    EXPECT_NE(v.data(), nullptr);
}
