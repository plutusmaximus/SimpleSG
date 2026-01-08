#pragma once

#include <string_view>
#include <cctype>

#include "imstring.h"

/// @brief Represents a parsed URI (Uniform Resource Identifier) according to RFC 3986.
class Uri
{
public:
    Uri() = default;

    explicit Uri(const imstring& uri)
    {
        assign(uri);
    }

    explicit Uri(imstring&& uri)
    {
        assign(std::move(uri));
    }

    const imstring& str() const noexcept
    {
        return storage_;
    }

    std::string_view scheme() const noexcept
    {
        return scheme_;
    }

    std::string_view authority() const noexcept
    {
        return authority_;
    }

    std::string_view userinfo() const noexcept
    {
        return userinfo_;
    }

    std::string_view host() const noexcept
    {
        return host_;
    }

    std::string_view port_str() const noexcept
    {
        return port_;
    }

    int port() const noexcept
    {
        return port_num_;
    }

    std::string_view path() const noexcept
    {
        return path_;
    }

    bool has_strict_scheme() const noexcept
    {
        return has_strict_scheme_;
    }

    bool has_authority() const noexcept
    {
        return !authority_.empty();
    }

    Uri& operator=(const Uri& other)
    {
        if (this != &other)
        {
            storage_ = other.storage_;
            scheme_ = other.scheme_;
            authority_ = other.authority_;
            userinfo_ = other.userinfo_;
            host_ = other.host_;
            port_ = other.port_;
            port_num_ = other.port_num_;
            path_ = other.path_;
            has_strict_scheme_ = other.has_strict_scheme_;
        }

        return *this;
    }

    Uri& operator=(Uri&& other) noexcept
    {
        if (this != &other)
        {
            storage_ = std::move(other.storage_);
            parse_views(static_cast<std::string_view>(storage_));
        }

        return *this;
    }

    bool operator==(const Uri& rhs) const noexcept
    {
        // Logical equality of parsed components (not necessarily original string equality).
        return scheme_ == rhs.scheme_
            && authority_ == rhs.authority_
            && userinfo_ == rhs.userinfo_
            && host_ == rhs.host_
            && port_ == rhs.port_
            && path_ == rhs.path_
            && has_strict_scheme_ == rhs.has_strict_scheme_;
    }

    bool operator!=(const Uri& rhs) const noexcept
    {
        return !(*this == rhs);
    }

    static bool is_valid_scheme(std::string_view sch) noexcept
    {
        // RFC 3986-like: ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
        if (sch.empty())
        {
            return false;
        }

        const unsigned char c0 = static_cast<unsigned char>(sch.front());
        if (!std::isalpha(c0))
        {
            return false;
        }

        for (const unsigned char c : sch)
        {
            if (std::isalnum(c))
            {
                continue;
            }

            if (c == '+' || c == '-' || c == '.')
            {
                continue;
            }

            return false;
        }

        return true;
    }

private:
    imstring storage_;

    std::string_view scheme_{};
    std::string_view authority_{};
    std::string_view userinfo_{};
    std::string_view host_{};
    std::string_view port_{};
    int port_num_ = -1;
    std::string_view path_{};
    bool has_strict_scheme_ = false;

    static bool all_digits(std::string_view s) noexcept
    {
        if (s.empty())
        {
            return false;
        }

        for (const unsigned char c : s)
        {
            if (!std::isdigit(c))
            {
                return false;
            }
        }

        return true;
    }

    static int parse_port(std::string_view s) noexcept
    {
        if (!all_digits(s))
        {
            return -1;
        }

        int v = 0;
        for (const unsigned char c : s)
        {
            const int d = static_cast<int>(c - '0');

            // Minimal overflow/validity guard.
            if (v > 65535)
            {
                return -1;
            }

            v = v * 10 + d;
        }

        if (v < 0 || v > 65535)
        {
            return -1;
        }

        return v;
    }

    void assign(const imstring& uri)
    {
        storage_ = uri;
        parse_views(static_cast<std::string_view>(storage_));
    }

    void assign(imstring&& uri)
    {
        storage_ = std::move(uri);
        parse_views(static_cast<std::string_view>(storage_));
    }

    void parse(std::string_view uri)
    {
        storage_ = imstring(uri);
        parse_views(static_cast<std::string_view>(storage_));
    }

    void clear_views() noexcept
    {
        scheme_ = {};
        authority_ = {};
        userinfo_ = {};
        host_ = {};
        port_ = {};
        path_ = {};
        port_num_ = -1;
        has_strict_scheme_ = false;
    }

    void parse_views(std::string_view s)
    {
        clear_views();

        // Cut query/fragment (ignored).
        const auto cut = s.find_first_of("?#");
        if (cut != std::string_view::npos)
        {
            s = s.substr(0, cut);
        }

        // Find "scheme:"
        const auto colon = s.find(':');
        if (colon == std::string_view::npos)
        {
            // No scheme -> treat all as path.
            path_ = s;
            return;
        }

        const auto cand_scheme = s.substr(0, colon);
        if (!is_valid_scheme(cand_scheme))
        {
            // Not a strict scheme -> treat all as path.
            path_ = s;
            return;
        }

        scheme_ = cand_scheme;
        has_strict_scheme_ = true;

        std::string_view rest = s.substr(colon + 1);

        // Optional authority: "//authority"
        if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/')
        {
            rest = rest.substr(2);

            const auto slash = rest.find('/');
            authority_ = (slash == std::string_view::npos) ? rest : rest.substr(0, slash);

            parse_authority(authority_);

            rest = (slash == std::string_view::npos) ? std::string_view{} : rest.substr(slash);
        }

        // Remaining is the path (may be empty).
        path_ = rest;
    }

    void parse_authority(std::string_view auth)
    {
        // [userinfo@]host[:port]
        std::string_view hostport = auth;

        const auto at = auth.find('@');
        if (at != std::string_view::npos)
        {
            userinfo_ = auth.substr(0, at);
            hostport = auth.substr(at + 1);
        }

        // IPv6 bracket form: [....](:port)?
        if (!hostport.empty() && hostport.front() == '[')
        {
            const auto rb = hostport.find(']');
            if (rb != std::string_view::npos)
            {
                host_ = hostport.substr(0, rb + 1); // keep brackets

                if (rb + 1 < hostport.size() && hostport[rb + 1] == ':')
                {
                    port_ = hostport.substr(rb + 2);
                    port_num_ = parse_port(port_);
                }

                return;
            }
            // Malformed; fall through.
        }

        // Split on last ':' (IPv6 should be bracketed above).
        const auto last_colon = hostport.rfind(':');
        if (last_colon != std::string_view::npos)
        {
            host_ = hostport.substr(0, last_colon);
            port_ = hostport.substr(last_colon + 1);
            port_num_ = parse_port(port_);
            return;
        }

        host_ = hostport;
    }
};
