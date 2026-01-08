#include <gtest/gtest.h>

#include "../src/Uri.h"

// Scheme validation per RFC 3986-like rules
TEST(UriTest, SchemeValidation)
{
    EXPECT_TRUE(Uri::is_valid_scheme("http"));
    EXPECT_TRUE(Uri::is_valid_scheme("a"));
    EXPECT_TRUE(Uri::is_valid_scheme("a1+.-"));

    EXPECT_FALSE(Uri::is_valid_scheme(""));
    EXPECT_FALSE(Uri::is_valid_scheme("-http"));
    EXPECT_FALSE(Uri::is_valid_scheme("1abc"));
    EXPECT_FALSE(Uri::is_valid_scheme("h*t"));
}

TEST(UriTest, ParseHttpWithAuthorityAndPath)
{
    Uri u("http://user:pass@example.com:8080/path");

    EXPECT_EQ(u.scheme(), "http");
    EXPECT_TRUE(u.has_strict_scheme());

    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.authority(), "user:pass@example.com:8080");
    EXPECT_EQ(u.userinfo(), "user:pass");
    EXPECT_EQ(u.host(), "example.com");

    EXPECT_EQ(u.port_str(), "8080");
    EXPECT_EQ(u.port(), 8080);

    EXPECT_EQ(u.path(), "/path");
}

TEST(UriTest, NoSchemeAllPath)
{
    Uri u("/just/path");

    EXPECT_EQ(u.scheme(), "");
    EXPECT_FALSE(u.has_strict_scheme());

    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.userinfo(), "");
    EXPECT_EQ(u.host(), "");
    EXPECT_EQ(u.port_str(), "");
    EXPECT_EQ(u.port(), -1);

    EXPECT_EQ(u.path(), "/just/path");
}

TEST(UriTest, NonStrictSchemeTreatedAsPath)
{
    Uri u("1abc:rest");

    EXPECT_FALSE(u.has_strict_scheme());
    EXPECT_EQ(u.scheme(), "");
    EXPECT_EQ(u.path(), "1abc:rest");
}

TEST(UriTest, QueryAndFragmentAreCut)
{
    Uri u("http://a/b?c#d");

    EXPECT_EQ(u.scheme(), "http");
    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.host(), "a");
    EXPECT_EQ(u.path(), "/b");
}

TEST(UriTest, Ipv6WithPort)
{
    Uri u("http://[2001:db8::1]:443/abc");

    EXPECT_EQ(u.scheme(), "http");
    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.host(), "[2001:db8::1]");
    EXPECT_EQ(u.port_str(), "443");
    EXPECT_EQ(u.port(), 443);
    EXPECT_EQ(u.path(), "/abc");
}

TEST(UriTest, AuthorityWithoutPort)
{
    Uri u("http://example.com");

    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.host(), "example.com");
    EXPECT_EQ(u.port_str(), "");
    EXPECT_EQ(u.port(), -1);
    EXPECT_EQ(u.path(), "");
}

TEST(UriTest, WithUserInfo)
{
    Uri u("http://user@example.com");

    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.userinfo(), "user");
    EXPECT_EQ(u.host(), "example.com");
    EXPECT_EQ(u.port(), -1);
}

TEST(UriTest, NonNumericPort)
{
    Uri u("http://a:abc");

    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.host(), "a");
    EXPECT_EQ(u.port_str(), "abc");
    EXPECT_EQ(u.port(), -1);
}

TEST(UriTest, SchemeAndPathNoAuthority)
{
    Uri u("file:/local/path");

    EXPECT_EQ(u.scheme(), "file");
    EXPECT_TRUE(u.has_strict_scheme());

    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.userinfo(), "");
    EXPECT_EQ(u.host(), "");
    EXPECT_EQ(u.port_str(), "");
    EXPECT_EQ(u.port(), -1);

    EXPECT_EQ(u.path(), "/local/path");
}

TEST(UriTest, SchemeTwoSlashesCreatesAuthority)
{
    // Double slash after scheme forces authority parsing, not path-only
    Uri u("file://local/path");

    EXPECT_EQ(u.scheme(), "file");
    EXPECT_TRUE(u.has_strict_scheme());

    // Has authority because of the //
    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.authority(), "local");
    EXPECT_EQ(u.host(), "local");
    EXPECT_EQ(u.path(), "/path");
}

TEST(UriTest, SchemeThreeSlashesEmptyAuthority)
{
    // file:///local/path: RFC 8089 compliant - empty authority, path is /local/path
    Uri u("file:///local/path");

    EXPECT_EQ(u.scheme(), "file");
    EXPECT_TRUE(u.has_strict_scheme());

    // Empty authority (/// means // for authority, nothing between //, then /)
    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.authority(), "");
    EXPECT_EQ(u.host(), "");
    EXPECT_EQ(u.path(), "/local/path");
}

TEST(UriTest, SchemeThreeSlashesEmptyAuthority2)
{
    // file:///local/path: RFC 8089 compliant - empty authority, path is /local/path
    Uri u("file:////local/path");

    EXPECT_EQ(u.scheme(), "file");
    EXPECT_TRUE(u.has_strict_scheme());

    // Empty authority (/// means // for authority, nothing between //, then /)
    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.authority(), "");
    EXPECT_EQ(u.host(), "");
    EXPECT_EQ(u.path(), "//local/path");
}

TEST(UriTest, EqualityByComponents)
{
    Uri u1("http://host:80/path");
    Uri u2("http://host:80/path");
    Uri u3("http://host:81/path");

    EXPECT_TRUE(u1 == u2);
    EXPECT_FALSE(u1 != u2);
    EXPECT_TRUE(u1 != u3);
}
