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

    // The // signals an authority component is present (but empty)
    // However, our URI parser treats this as no authority and a path of /local/path
    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.authority(), "");
    EXPECT_EQ(u.host(), "");
    EXPECT_EQ(u.path(), "/local/path");
}

// ========== IPv6 Edge Cases ==========

TEST(UriTest, Ipv6WithoutPort)
{
    Uri u("http://[::1]/path");

    EXPECT_EQ(u.scheme(), "http");
    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.host(), "[::1]");
    EXPECT_EQ(u.port_str(), "");
    EXPECT_EQ(u.port(), -1);
    EXPECT_EQ(u.path(), "/path");
}

TEST(UriTest, Ipv6FullForm)
{
    Uri u("http://[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:8080/");

    EXPECT_EQ(u.scheme(), "http");
    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.host(), "[2001:0db8:85a3:0000:0000:8a2e:0370:7334]");
    EXPECT_EQ(u.port(), 8080);
}

TEST(UriTest, Ipv6MalformedMissingCloseBracket)
{
    Uri u("http://[::1/path");

    EXPECT_EQ(u.scheme(), "http");
    // Malformed; parser falls through, treats rest as host:port
    EXPECT_TRUE(u.has_authority());
}

// ========== Userinfo Edge Cases ==========

TEST(UriTest, UserinfoWithoutPassword)
{
    Uri u("http://user@example.com:8080/path");

    EXPECT_EQ(u.userinfo(), "user");
    EXPECT_EQ(u.host(), "example.com");
    EXPECT_EQ(u.port(), 8080);
}

TEST(UriTest, UserinfoWithSpecialCharacters)
{
    Uri u("http://user%40name:pass%3Aword@host.com/");

    EXPECT_EQ(u.userinfo(), "user%40name:pass%3Aword");
    EXPECT_EQ(u.host(), "host.com");
}

TEST(UriTest, EmptyUserinfoWithAt)
{
    // ":@host" - empty userinfo (colon with nothing before it)
    Uri u("http://:@example.com/path");

    EXPECT_EQ(u.userinfo(), ":");
    EXPECT_EQ(u.host(), "example.com");
}

// ========== Host Edge Cases ==========

TEST(UriTest, LocalhostHost)
{
    Uri u("http://localhost:3000/api");

    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.host(), "localhost");
    EXPECT_EQ(u.port(), 3000);
}

TEST(UriTest, Ipv4Address)
{
    Uri u("http://192.168.1.1:8080/path");

    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.host(), "192.168.1.1");
    EXPECT_EQ(u.port(), 8080);
}

TEST(UriTest, EmptyHostWithAuthority)
{
    // "http://" - authority marker present but host is empty
    Uri u("http:///path");

    // Even though technically an authority marker is present,
    // our URI parser treats this as no authority and path of /path
    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.authority(), "");
    EXPECT_EQ(u.host(), "");
    EXPECT_EQ(u.path(), "/path");
}

// ========== Empty/Minimal URIs ==========

TEST(UriTest, CompletelyEmpty)
{
    Uri u("");

    EXPECT_EQ(u.scheme(), "");
    EXPECT_FALSE(u.has_strict_scheme());
    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.path(), "");
}

TEST(UriTest, JustScheme)
{
    Uri u("http:");

    EXPECT_EQ(u.scheme(), "http");
    EXPECT_TRUE(u.has_strict_scheme());
    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.path(), "");
}

TEST(UriTest, SchemeWithEmptyDoubleSlash)
{
    Uri u("http://");

    EXPECT_EQ(u.scheme(), "http");
    // Even though technically an authority marker is present,
    // our URI parser treats this as no authority and empty path.
    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.authority(), "");
    EXPECT_EQ(u.host(), "");
    EXPECT_EQ(u.path(), "");
}

// ========== Move/Assignment Semantics ==========

TEST(UriTest, MoveConstruction)
{
    Uri u1("http://example.com:8080/path");
    Uri u2(std::move(u1));

    EXPECT_EQ(u2.scheme(), "http");
    EXPECT_EQ(u2.host(), "example.com");
    EXPECT_EQ(u2.port(), 8080);
    EXPECT_EQ(u2.path(), "/path");
}

TEST(UriTest, MoveAssignment)
{
    Uri u1("http://example.com:8080/path");
    Uri u2("https://other.com/other");
    u2 = std::move(u1);

    EXPECT_EQ(u2.scheme(), "http");
    EXPECT_EQ(u2.host(), "example.com");
    EXPECT_EQ(u2.port(), 8080);
    EXPECT_EQ(u2.path(), "/path");
}

TEST(UriTest, CopyConstruction)
{
    Uri u1("http://example.com:8080/path");
    Uri u2(u1);

    EXPECT_EQ(u1, u2);
    EXPECT_EQ(u2.scheme(), "http");
    EXPECT_EQ(u2.host(), "example.com");
}

TEST(UriTest, CopyAssignment)
{
    Uri u1("http://example.com:8080/path");
    Uri u2("https://other.com/other");
    u2 = u1;

    EXPECT_EQ(u1, u2);
    EXPECT_EQ(u2.scheme(), "http");
}

// ========== Data Scheme URIs ==========

TEST(UriTest, DataSchemeTextPlain)
{
    Uri u("data:text/plain,hello%20world");

    EXPECT_EQ(u.scheme(), "data");
    EXPECT_TRUE(u.has_strict_scheme());
    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.path(), "text/plain,hello%20world");
}

TEST(UriTest, MailtoScheme)
{
    Uri u("mailto:user@example.com?subject=test");

    EXPECT_EQ(u.scheme(), "mailto");
    EXPECT_TRUE(u.has_strict_scheme());
    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.path(), "user@example.com");
}

// ========== RFC 8089 File Scheme Variations ==========

TEST(UriTest, FileSchemeWithSingleSlash)
{
    // Minimal file URI per RFC 8089
    Uri u("file:/path/to/file");

    EXPECT_EQ(u.scheme(), "file");
    EXPECT_TRUE(u.has_strict_scheme());
    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.path(), "/path/to/file");
}

TEST(UriTest, FileSchemeWithHost)
{
    // Non-local file with explicit host
    Uri u("file://remote.example.com/path/to/file");

    EXPECT_EQ(u.scheme(), "file");
    EXPECT_TRUE(u.has_authority());
    EXPECT_EQ(u.host(), "remote.example.com");
    EXPECT_EQ(u.path(), "/path/to/file");
}

TEST(UriTest, FileUncPath)
{
    // UNC-style paths (file:////) are handled as normal URIs
    Uri u("file:////server/share/file.txt");

    EXPECT_TRUE(u.has_strict_scheme());
    EXPECT_EQ(u.scheme(), "file");
    EXPECT_FALSE(u.has_authority());
    EXPECT_EQ(u.path(), "//server/share/file.txt");
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
