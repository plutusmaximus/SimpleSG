#include <gtest/gtest.h>
#include <sstream>
#include <unordered_map>

#include "../src/imstring.h"

// Test default constructor
TEST(ImstringTest, DefaultConstructor)
{
    imstring s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0);
    EXPECT_STREQ(s.c_str(), "");
    EXPECT_STREQ(s.data(), "");
}

// Test C-string constructor
TEST(ImstringTest, CStringConstructor)
{
    imstring s("hello");
    EXPECT_EQ(s.size(), 5);
    EXPECT_STREQ(s.c_str(), "hello");
    EXPECT_FALSE(s.empty());
}

// Test C-string constructor with nullptr
TEST(ImstringTest, CStringConstructorNull)
{
    imstring s(nullptr);
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0);
}

// Test C-string constructor with explicit length
TEST(ImstringTest, CStringConstructorWithLength)
{
    const char* str = "hello world";
    imstring s(str, 5);
    EXPECT_EQ(s.size(), 5);
    EXPECT_STREQ(s.c_str(), "hello");
}

// Test C-string constructor with length but null pointer throws
TEST(ImstringTest, CStringConstructorWithLengthAndNullThrows)
{
    EXPECT_THROW(imstring(nullptr, 5), std::invalid_argument);
}

// Test string_view constructor
TEST(ImstringTest, StringViewConstructor)
{
    std::string_view sv("test");
    imstring s(sv);
    EXPECT_EQ(s.size(), 4);
    EXPECT_STREQ(s.c_str(), "test");
}

// Test std::string constructor
TEST(ImstringTest, StdStringConstructor)
{
    std::string str("content");
    imstring s(str);
    EXPECT_EQ(s.size(), 7);
    EXPECT_STREQ(s.c_str(), "content");
}

// Test copy constructor
TEST(ImstringTest, CopyConstructor)
{
    imstring s1("original");
    const char* original = s1.c_str();
    imstring s2(s1);
    EXPECT_EQ(s1, s2);
    EXPECT_EQ(s2.c_str(), original);
    EXPECT_STREQ(s2.c_str(), "original");
}

// Test move constructor
TEST(ImstringTest, MoveConstructor)
{
    imstring s1("original");
    const char* original = s1.c_str();
    imstring s2(std::move(s1));
    EXPECT_STREQ(s2.c_str(), "original");
    EXPECT_EQ(s2.c_str(), original);
    EXPECT_EQ(s2.size(), 8);
    EXPECT_TRUE(s1.empty());
}

// Test copy assignment
TEST(ImstringTest, CopyAssignment)
{
    imstring s1("first");
    const char* original = s1.c_str();
    imstring s2("second");
    s2 = s1;
    EXPECT_EQ(s1, s2);
    EXPECT_EQ(s2.c_str(), original);
    EXPECT_STREQ(s2.c_str(), "first");
}

// Test move assignment
TEST(ImstringTest, MoveAssignment)
{
    imstring s1("original");
    imstring s2("other");
    const char* original = s1.c_str();
    s2 = std::move(s1);
    EXPECT_STREQ(s2.c_str(), "original");
    EXPECT_EQ(s2.c_str(), original);
    EXPECT_TRUE(s1.empty());
}

// Test string assignment
TEST(ImstringTest, StringAssignment)
{
    imstring s;
    std::string str("assigned");
    s = str;
    EXPECT_STREQ(s.c_str(), "assigned");
    EXPECT_EQ(s.size(), 8);
}

// Test operator[] access
TEST(ImstringTest, OperatorBrackets)
{
    imstring s("hello");
    EXPECT_EQ(s[0], 'h');
    EXPECT_EQ(s[1], 'e');
    EXPECT_EQ(s[4], 'o');
}

// Test at() method
TEST(ImstringTest, AtMethod)
{
    imstring s("hello");
    EXPECT_EQ(s.at(0), 'h');
    EXPECT_EQ(s.at(4), 'o');
}

// Test at() out of range throws
TEST(ImstringTest, AtMethodOutOfRangeThrows)
{
    imstring s("hello");
    EXPECT_THROW(s.at(5), std::out_of_range);
    EXPECT_THROW(s.at(10), std::out_of_range);
}

// Test starts_with method
TEST(ImstringTest, StartsWithMethod)
{
    imstring s("hello world");
    EXPECT_TRUE(s.starts_with("hello"));
    EXPECT_TRUE(s.starts_with("hello world"));
    EXPECT_FALSE(s.starts_with("world"));
    EXPECT_FALSE(s.starts_with("hello world!"));
}

// Test starts_with with empty string
TEST(ImstringTest, StartsWithEmpty)
{
    imstring s("hello");
    EXPECT_TRUE(s.starts_with(""));
}

// Test ends_with method
TEST(ImstringTest, EndsWithMethod)
{
    imstring s("hello world");
    EXPECT_TRUE(s.ends_with("world"));
    EXPECT_TRUE(s.ends_with("hello world"));
    EXPECT_FALSE(s.ends_with("hello"));
    EXPECT_FALSE(s.ends_with("!world"));
}

// Test ends_with with empty string
TEST(ImstringTest, EndsWithEmpty)
{
    imstring s("hello");
    EXPECT_TRUE(s.ends_with(""));
}

// Test contains method
TEST(ImstringTest, ContainsMethod)
{
    imstring s("hello world");
    EXPECT_TRUE(s.contains("world"));
    EXPECT_TRUE(s.contains("lo wo"));
    EXPECT_TRUE(s.contains("hello world"));
    EXPECT_FALSE(s.contains("xyz"));
    EXPECT_FALSE(s.contains("HELLO"));
}

// Test find character method
TEST(ImstringTest, FindCharMethod)
{
    imstring s("hello world");
    EXPECT_EQ(s.find('h'), 0);
    EXPECT_EQ(s.find('o'), 4);
    EXPECT_EQ(s.find('x'), imstring::npos);
}

// Test find character with position
TEST(ImstringTest, FindCharWithPosition)
{
    imstring s("hello world");
    EXPECT_EQ(s.find('o', 5), 7);
    EXPECT_EQ(s.find('h', 1), imstring::npos);
}

// Test find string method
TEST(ImstringTest, FindStringMethod)
{
    imstring s("hello world");
    EXPECT_EQ(s.find("world"), 6);
    EXPECT_EQ(s.find("hello"), 0);
    EXPECT_EQ(s.find("xyz"), imstring::npos);
}

// Test find empty string
TEST(ImstringTest, FindEmptyString)
{
    imstring s("hello");
    EXPECT_EQ(s.find(""), 0);
}

// Test find with position
TEST(ImstringTest, FindWithPosition)
{
    imstring s("hello hello");
    EXPECT_EQ(s.find("hello", 0), 0);
    EXPECT_EQ(s.find("hello", 1), 6);
}

// Test rfind character method
TEST(ImstringTest, RfindCharMethod)
{
    imstring s("hello world");
    EXPECT_EQ(s.rfind('o'), 7);
    EXPECT_EQ(s.rfind('h'), 0);
    EXPECT_EQ(s.rfind('x'), imstring::npos);
}

// Test rfind character with position
TEST(ImstringTest, RfindCharWithPosition)
{
    imstring s("hello world");
    EXPECT_EQ(s.rfind('o', 4), 4);
    EXPECT_EQ(s.rfind('l', 2), 2);
}

// Test rfind string method
TEST(ImstringTest, RfindStringMethod)
{
    imstring s("hello hello");
    EXPECT_EQ(s.rfind("hello"), 6);
    EXPECT_EQ(s.rfind("xyz"), imstring::npos);
}

// Test rfind empty string
TEST(ImstringTest, RfindEmptyString)
{
    imstring s("hello");
    EXPECT_EQ(s.rfind(""), 5);
}

// Test substr method
TEST(ImstringTest, SubstrMethod)
{
    imstring s("hello world");
    imstring sub = s.substr(0, 5);
    EXPECT_STREQ(sub.c_str(), "hello");
    EXPECT_EQ(sub.size(), 5);
}

// Test substr with no length
TEST(ImstringTest, SubstrNoLength)
{
    imstring s("hello world");
    imstring sub = s.substr(6);
    EXPECT_STREQ(sub.c_str(), "world");
}

// Test substr out of range throws
TEST(ImstringTest, SubstrOutOfRangeThrows)
{
    imstring s("hello");
    EXPECT_THROW(s.substr(10), std::out_of_range);
}

// Test operator+ concatenation
TEST(ImstringTest, OperatorPlus)
{
    imstring s1("hello");
    imstring s2(" world");
    imstring result = s1 + s2;
    EXPECT_STREQ(result.c_str(), "hello world");
    EXPECT_EQ(result.size(), 11);
}

// Test operator+ with empty strings
TEST(ImstringTest, OperatorPlusEmpty)
{
    imstring s1("hello");
    imstring s2;
    imstring result1 = s1 + s2;
    EXPECT_STREQ(result1.c_str(), "hello");
    
    imstring result2 = s2 + s1;
    EXPECT_STREQ(result2.c_str(), "hello");
}

// Test operator== equality
TEST(ImstringTest, OperatorEquality)
{
    imstring s1("test");
    imstring s2("test");
    imstring s3("different");
    EXPECT_TRUE(s1 == s2);
    EXPECT_FALSE(s1 == s3);
}

// Test operator<=> comparison
TEST(ImstringTest, OperatorSpaceship)
{
    imstring s1("abc");
    imstring s2("abc");
    imstring s3("xyz");
    EXPECT_TRUE((s1 <=> s2) == std::strong_ordering::equal);
    EXPECT_TRUE((s1 <=> s3) == std::strong_ordering::less);
    EXPECT_TRUE((s3 <=> s1) == std::strong_ordering::greater);
}

// Test operator<< for ostream
TEST(ImstringTest, OperatorStreamOut)
{
    imstring s("hello");
    std::ostringstream oss;
    oss << s;
    EXPECT_EQ(oss.str(), "hello");
}

// Test string_view conversion
TEST(ImstringTest, StringViewConversion)
{
    imstring s("test");
    std::string_view sv = s.view();
    EXPECT_EQ(sv, "test");
    
    std::string_view sv2 = static_cast<std::string_view>(s);
    EXPECT_EQ(sv2, "test");
}

// Test hashing
TEST(ImstringTest, Hashing)
{
    imstring s1("hash test");
    imstring s2("hash test");
    imstring s3("different");
    
    std::hash<imstring> hasher;
    EXPECT_EQ(hasher(s1), hasher(s2));
    EXPECT_NE(hasher(s1), hasher(s3));
}

// Test hash in unordered_map
TEST(ImstringTest, UnorderedMapWithHash)
{
    std::unordered_map<imstring, int> map;
    imstring key1("test");
    imstring key2("test");
    
    map[key1] = 42;
    EXPECT_EQ(map[key2], 42);
}

// Test user-defined literal
TEST(ImstringTest, UserDefinedLiteral)
{
    auto s = "literal test"_is;
    EXPECT_STREQ(s.c_str(), "literal test");
    EXPECT_EQ(s.size(), 12);
}

// Test empty imstring
TEST(ImstringTest, EmptyImstring)
{
    imstring s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0);
    EXPECT_TRUE(s.starts_with(""));
    EXPECT_TRUE(s.ends_with(""));
    EXPECT_EQ(s.find(""), 0);
}

// Test copy-on-write semantics
TEST(ImstringTest, CopyOnWriteSemantics)
{
    imstring s1("original");
    imstring s2 = s1;
    
    // Both should have the same content
    EXPECT_EQ(s1, s2);
    
    // Reassign s2 - s1 should be unaffected
    s2 = imstring("modified");
    EXPECT_NE(s1, s2);
    EXPECT_STREQ(s1.c_str(), "original");
    EXPECT_STREQ(s2.c_str(), "modified");
}

// Test case sensitivity
TEST(ImstringTest, CaseSensitivity)
{
    imstring s1("Hello");
    imstring s2("hello");
    EXPECT_FALSE(s1 == s2);
    EXPECT_FALSE(s1.starts_with("hello"));
    EXPECT_FALSE(s1.contains("HELLO"));
}

// Test special characters
TEST(ImstringTest, SpecialCharacters)
{
    imstring s("hello\nworld\t!");
    EXPECT_EQ(s.size(), 13);
    EXPECT_EQ(s[5], '\n');
    EXPECT_EQ(s[11], '\t');
}

// Test very long string
TEST(ImstringTest, LongString)
{
    std::string longStr(10000, 'a');
    imstring s(longStr);
    EXPECT_EQ(s.size(), 10000);
    EXPECT_EQ(s[0], 'a');
    EXPECT_EQ(s[9999], 'a');
}

// Test substr of substr
TEST(ImstringTest, SubstrOfSubstr)
{
    imstring s("hello world");
    imstring sub1 = s.substr(0, 8);
    imstring sub2 = sub1.substr(0, 5);
    EXPECT_STREQ(sub2.c_str(), "hello");
}

// Test multiple copies
TEST(ImstringTest, MultipleCopies)
{
    imstring s1("original");
    imstring s2 = s1;
    imstring s3 = s1;
    imstring s4 = s2;
    
    EXPECT_EQ(s1, s2);
    EXPECT_EQ(s1, s3);
    EXPECT_EQ(s1, s4);
}

// Test std::format basic
TEST(ImstringTest, FormatBasic)
{
    imstring s("hello");
    std::string result = std::format("{}", s);
    EXPECT_EQ(result, "hello");
}

// Test std::format with multiple arguments
TEST(ImstringTest, FormatMultipleArguments)
{
    imstring s1("world");
    imstring s2("C++");
    std::string result = std::format("Hello, {}! Welcome to {}.", s1, s2);
    EXPECT_EQ(result, "Hello, world! Welcome to C++.");
}

// Test std::format with width and alignment
TEST(ImstringTest, FormatWidthAndAlignment)
{
    imstring s("test");
    // Left align
    EXPECT_EQ(std::format("{:<10}", s), "test      ");
    // Right align
    EXPECT_EQ(std::format("{:>10}", s), "      test");
    // Center align
    EXPECT_EQ(std::format("{:^10}", s), "   test   ");
}

// Test std::format with fill character and alignment
TEST(ImstringTest, FormatFillCharacter)
{
    imstring s("hi");
    // Left align with fill
    EXPECT_EQ(std::format("{:*<8}", s), "hi******");
    // Right align with fill
    EXPECT_EQ(std::format("{:*>8}", s), "******hi");
    // Center align with fill
    EXPECT_EQ(std::format("{:*^8}", s), "***hi***");
}

// Test std::format with mixed types
TEST(ImstringTest, FormatMixedTypes)
{
    imstring name("Alice");
    int age = 30;
    double score = 95.5;
    std::string result = std::format("Name: {}, Age: {}, Score: {:.1f}", name, age, score);
    EXPECT_EQ(result, "Name: Alice, Age: 30, Score: 95.5");
}

// Test std::format with empty imstring
TEST(ImstringTest, FormatEmptyImstring)
{
    imstring s;
    std::string result = std::format("Empty: '{}'", s);
    EXPECT_EQ(result, "Empty: ''");
    
    // Empty with alignment
    EXPECT_EQ(std::format("{:>5}", s), "     ");
    EXPECT_EQ(std::format("{:<5}", s), "     ");
}

// Test std::format with escaped braces
TEST(ImstringTest, FormatEscapedBraces)
{
    imstring s("value");
    std::string result = std::format("{{{}}} = {}", "key", s);
    EXPECT_EQ(result, "{key} = value");
}

// Test std::format with special characters
TEST(ImstringTest, FormatSpecialCharacters)
{
    imstring s1("hello\nworld");
    imstring s2("tab\there");
    std::string result = std::format("Line 1: {}\nLine 2: {}", s1, s2);
    EXPECT_EQ(result, "Line 1: hello\nworld\nLine 2: tab\there");
}
