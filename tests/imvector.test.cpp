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

// ========================================
// Builder Tests: Data Transfer and Ownership
// ========================================

// Test builder push_back and build transfers data and ownership
TEST(ImvectorBuilderTest, BuildTransfersData)
{
    imvector<int>::builder b;
    b.push_back(10);
    b.push_back(20);
    b.push_back(30);
    
    // Capture builder's data pointer before build
    const int* builder_data_before = b.data();
    EXPECT_NE(builder_data_before, nullptr);
    EXPECT_EQ(b.size(), 3);
    
    imvector<int> v = b.build();
    
    // Verify imvector received the data (same pointer)
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v[1], 20);
    EXPECT_EQ(v[2], 30);
    EXPECT_EQ(v.data(), builder_data_before);
    
    // Verify ownership transferred: builder data pointer is now null or empty placeholder
    EXPECT_EQ(b.size(), 0);
    EXPECT_EQ(b.capacity(), 0);
    EXPECT_TRUE(b.empty());
    EXPECT_NE(b.data(), builder_data_before);
}

// Test builder can be reused after build() and ownership transfers each time
TEST(ImvectorBuilderTest, BuilderReuseAfterBuild)
{
    imvector<int>::builder b;
    
    // First build
    b.push_back(1);
    b.push_back(2);
    const int* first_builder_data = b.data();
    imvector<int> v1 = b.build();
    
    EXPECT_EQ(v1.size(), 2);
    EXPECT_EQ(v1[0], 1);
    EXPECT_EQ(v1[1], 2);
    EXPECT_EQ(v1.data(), first_builder_data);
    EXPECT_NE(b.data(), first_builder_data);
    
    // Builder can be reused immediately
    b.push_back(10);
    b.push_back(20);
    b.push_back(30);
    const int* second_builder_data = b.data();
    imvector<int> v2 = b.build();
    
    EXPECT_EQ(v2.size(), 3);
    EXPECT_EQ(v2[0], 10);
    EXPECT_EQ(v2[1], 20);
    EXPECT_EQ(v2[2], 30);
    EXPECT_EQ(v2.data(), second_builder_data);
    EXPECT_NE(b.data(), second_builder_data);
    
    // Original v1 unchanged, owns different memory from v2
    EXPECT_EQ(v1.size(), 2);
    EXPECT_EQ(v1[0], 1);
    EXPECT_EQ(v1[1], 2);
    EXPECT_NE(v1.data(), v2.data());
}

// Test build with emplace_back and verify ownership transfer via data pointers
TEST(ImvectorBuilderTest, BuildWithEmplaceBack)
{
    imvector<std::string>::builder b;
    b.emplace_back("hello");
    b.emplace_back("world");
    b.emplace_back("test");
    
    EXPECT_EQ(b.size(), 3);
    const std::string* builder_data = b.data();
    
    imvector<std::string> v = b.build();
    
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0], "hello");
    EXPECT_EQ(v[1], "world");
    EXPECT_EQ(v[2], "test");
    
    // Verify ownership transferred: imvector owns the data, builder's pointer changed
    EXPECT_EQ(v.data(), builder_data);
    EXPECT_NE(b.data(), builder_data);
    EXPECT_EQ(b.size(), 0);
    EXPECT_EQ(b.capacity(), 0);
}

// Test build with append and verify ownership transfer
TEST(ImvectorBuilderTest, BuildWithAppend)
{
    imvector<int>::builder b;
    
    std::vector<int> data = {5, 10, 15};
    b.append(std::span<const int>(data));
    
    EXPECT_EQ(b.size(), 3);
    const int* builder_data = b.data();
    
    imvector<int> v = b.build();
    
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0], 5);
    EXPECT_EQ(v[1], 10);
    EXPECT_EQ(v[2], 15);
    
    // Verify ownership transferred: imvector owns the data, builder's pointer changed
    EXPECT_EQ(v.data(), builder_data);
    EXPECT_NE(b.data(), builder_data);
    EXPECT_EQ(b.size(), 0);
    EXPECT_EQ(b.capacity(), 0);
}

// Test build with reserve and capacity growth, verify ownership transfer
TEST(ImvectorBuilderTest, BuildWithReserveAndGrowth)
{
    imvector<int>::builder b;
    b.reserve(10);
    
    EXPECT_EQ(b.capacity(), 10);
    EXPECT_EQ(b.size(), 0);
    
    b.push_back(1);
    b.push_back(2);
    b.push_back(3);
    
    const int* builder_data = b.data();
    imvector<int> v = b.build();
    
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 3);
    
    // Verify ownership transferred: imvector owns the data, builder's pointer changed
    EXPECT_EQ(v.data(), builder_data);
    EXPECT_NE(b.data(), builder_data);
    EXPECT_EQ(b.size(), 0);
    EXPECT_EQ(b.capacity(), 0);
}

// Test build with empty builder returns empty imvector
TEST(ImvectorBuilderTest, BuildEmptyBuilder)
{
    imvector<int>::builder b;
    
    EXPECT_EQ(b.size(), 0);
    EXPECT_TRUE(b.empty());
    
    imvector<int> v = b.build();
    
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0);
    EXPECT_NE(v.data(), nullptr);
}

// Test multiple imvectors built from same builder data (after separate builds)
TEST(ImvectorBuilderTest, MultipleBuildCycles)
{
    imvector<int>::builder b;
    
    // First cycle
    b.push_back(100);
    b.push_back(200);
    imvector<int> v1 = b.build();
    
    // Second cycle
    b.push_back(1000);
    b.push_back(2000);
    b.push_back(3000);
    imvector<int> v2 = b.build();
    
    // Third cycle
    b.push_back(11);
    imvector<int> v3 = b.build();
    
    // Verify all imvectors have correct independent data
    EXPECT_EQ(v1.size(), 2);
    EXPECT_EQ(v1[0], 100);
    EXPECT_EQ(v1[1], 200);
    
    EXPECT_EQ(v2.size(), 3);
    EXPECT_EQ(v2[0], 1000);
    EXPECT_EQ(v2[1], 2000);
    EXPECT_EQ(v2[2], 3000);
    
    EXPECT_EQ(v3.size(), 1);
    EXPECT_EQ(v3[0], 11);
    
    // Builder should be empty after last build
    EXPECT_EQ(b.size(), 0);
    EXPECT_EQ(b.capacity(), 0);
}

// Test build with non-trivial types and verify ownership transfer
TEST(ImvectorBuilderTest, BuildWithNonTrivialTypes)
{
    imvector<std::string>::builder b;
    
    std::string hello("hello");
    std::string world("world");
    std::string test("test");
    
    b.push_back(hello);
    b.push_back(world);
    b.push_back(test);
    
    EXPECT_EQ(b.size(), 3);
    const std::string* builder_data = b.data();
    
    imvector<std::string> v = b.build();
    
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0], "hello");
    EXPECT_EQ(v[1], "world");
    EXPECT_EQ(v[2], "test");
    
    // Verify ownership transferred: imvector owns the data, builder's pointer changed
    EXPECT_EQ(v.data(), builder_data);
    EXPECT_NE(b.data(), builder_data);
    EXPECT_EQ(b.size(), 0);
    EXPECT_EQ(b.capacity(), 0);
}

// Test build preserves data after multiple reallocations and verify ownership transfer
TEST(ImvectorBuilderTest, BuildAfterMultipleReallocations)
{
    imvector<int>::builder b;
    
    // Add elements that will trigger multiple reallocations
    // (assuming growth strategy: 1.5x, at least 8)
    for (int i = 0; i < 100; ++i)
    {
        b.push_back(i);
    }
    
    EXPECT_EQ(b.size(), 100);
    const int* builder_data = b.data();
    
    imvector<int> v = b.build();
    
    EXPECT_EQ(v.size(), 100);
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_EQ(v[i], i);
    }
    
    // Verify ownership transferred: imvector owns the data, builder's pointer changed
    EXPECT_EQ(v.data(), builder_data);
    EXPECT_NE(b.data(), builder_data);
    EXPECT_EQ(b.size(), 0);
    EXPECT_EQ(b.capacity(), 0);
}

// Test builder move ownership transfer and build transfer
TEST(ImvectorBuilderTest, BuilderMoveOwnershipTransfer)
{
    imvector<int>::builder b1;
    b1.push_back(10);
    b1.push_back(20);
    b1.push_back(30);
    
    EXPECT_EQ(b1.size(), 3);
    const int* b1_data = b1.data();
    
    // Move to b2
    imvector<int>::builder b2 = std::move(b1);
    
    // b1 should be empty after move, data pointer changed
    EXPECT_EQ(b1.size(), 0);
    EXPECT_EQ(b1.capacity(), 0);
    EXPECT_NE(b1.data(), b1_data);
    
    // b2 should own the data (same pointer as b1 had)
    EXPECT_EQ(b2.size(), 3);
    EXPECT_EQ(b2.data(), b1_data);
    
    imvector<int> v = b2.build();
    
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v[1], 20);
    EXPECT_EQ(v[2], 30);
    
    // Verify ownership transferred from b2 to v
    EXPECT_EQ(v.data(), b1_data);
    EXPECT_NE(b2.data(), b1_data);
}

// Test built imvector shares storage with copies (ref-counting)
TEST(ImvectorBuilderTest, BuildedImvectorRefCounting)
{
    imvector<int>::builder b;
    b.push_back(7);
    b.push_back(14);
    b.push_back(21);
    
    imvector<int> v1 = b.build();
    const int* p1 = v1.data();
    
    // Copy v1: should share storage
    imvector<int> v2 = v1;
    const int* p2 = v2.data();
    
    EXPECT_EQ(p1, p2);
    
    // Assign v1 to v3: should share storage
    imvector<int> v3;
    v3 = v1;
    const int* p3 = v3.data();
    
    EXPECT_EQ(p1, p3);
    
    // All should have same values
    EXPECT_EQ(v1[0], v2[0]);
    EXPECT_EQ(v2[0], v3[0]);
}