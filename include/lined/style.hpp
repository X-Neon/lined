#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "utf8.hpp"

namespace lined {

namespace detail {

class style_impl;

}

class color
{
    friend detail::style_impl;

public:
    constexpr color() : m_color{0, 0, 0, 0} {}
    constexpr color(uint8_t color_code) : m_color{1, color_code, 0, 0} {}
    constexpr color(uint8_t r, uint8_t g, uint8_t b) : m_color{2, r, g, b} {}

    constexpr bool operator==(const color& other) const {
        for (auto i = 0UL; i < m_color.size(); ++i) {
            if (m_color[i] != other.m_color[i]) {
                return false;
            }
        }

        return true;
    }
    constexpr bool operator!=(const color& other) const { return !operator==(other); }

    constexpr static color black() { return color(0); }
    constexpr static color red() { return color(1); }
    constexpr static color green() { return color(2); }
    constexpr static color yellow() { return color(3); }
    constexpr static color blue() { return color(4); }
    constexpr static color magenta() { return color(5); }
    constexpr static color cyan() { return color(6); }
    constexpr static color white() { return color(7); }
    constexpr static color gray() { return color(8); }
    constexpr static color bright_red() { return color(9); }
    constexpr static color bright_green() { return color(10); }
    constexpr static color bright_yellow() { return color(11); }
    constexpr static color bright_blue() { return color(12); }
    constexpr static color bright_magenta() { return color(13); }
    constexpr static color bright_cyan() { return color(14); }
    constexpr static color bright_white() { return color(15); }

private:
    std::array<uint8_t, 4> m_color;
};

struct style
{
    bool bold;
    color fg;
    color bg;
};

namespace detail {

class style_impl
{
public:
    style_impl() : m_style{} {}

    style_impl(style s) {
        if (s.bold) {
            m_style[0] = 1;
        }

        if (s.fg.m_color[0] != 0) {
            m_style[1] |= 1 << 3;
            if (s.fg.m_color[0] == 2) {
                m_style[1] |= 1 << 2;
                std::copy_n(&s.fg.m_color[1], 3, &m_style[2]);
            } else {
                m_style[2] = s.fg.m_color[1];
            }
        }

        if (s.bg.m_color[0] != 0) {
            m_style[1] |= 1 << 1;
            if (s.bg.m_color[0] == 2) {
                m_style[1] |= 1 << 0;
                std::copy_n(&s.bg.m_color[1], 3, &m_style[5]);
            } else {
                m_style[5] = s.bg.m_color[1];
            }
        }
    }

    static std::string switch_to(style_impl& from, const style_impl& to) {
        const auto& s = to.m_style;
        if (from.m_style == s) {
            return {};
        }

        std::string out = "\x1b[0;";
        if (s[0] == 1) {
            out += "1;";
        }
        if (s[1] & 1 << 3) {
            if (s[1] & 1 << 2) {
                out += "38;2;" + std::to_string(s[2]) + ';' + std::to_string(s[3]) + ';' + std::to_string(s[4]) + ';';
            } else {
                out += "38;5;" + std::to_string(s[2]) + ';';
            }
        }
        if (s[1] & 1 << 1) {
            if (s[1] & 1 << 0) {
                out += "38;2;" + std::to_string(s[5]) + ';' + std::to_string(s[6]) + ';' + std::to_string(s[7]) + ';';
            } else {
                out += "38;5;" + std::to_string(s[5]) + ';';
            }
        }

        out.back() = 'm';
        from = to;
        return out;
    }

    bool operator==(const style_impl& other) const { return m_style == other.m_style; }
    bool operator!=(const style_impl& other) const { return !operator==(other); }

private:
    std::array<uint8_t, 8> m_style = {};
};

} // namespace detail

class style_iterator
{
public:
    using iterator_category = std::output_iterator_tag;
    using reference = void;
    using pointer = void;
    using value_type = void;
    using difference_type = std::ptrdiff_t;

    style_iterator(const char* str, std::vector<detail::style_impl>& style) : m_str(str), m_out(style.begin()) {}

    style_iterator& operator*() { return *this; }
    style_iterator operator[](int n) const { return *this + n; }
    
    style_iterator& operator=(const style& s) {
        *m_out = s;
        return *this;
    }

    style_iterator& operator++() {
        m_str++;
        if (!detail::is_continuation_byte(*m_str)) {
            m_out++;
        }
        return *this;
    }
    style_iterator operator++(int) {
        auto old = *this;
        operator++();
        return old;
    }
    style_iterator& operator--() {
        if (!detail::is_continuation_byte(*m_str)) {
            m_out--;
        }
        m_str--;
        return *this;
    }
    style_iterator operator--(int) {
        auto old = *this;
        operator--();
        return old;
    }
    style_iterator& operator+=(int n) {
        for (int i = 0; i < n; ++i) {
            operator++();
        }
        return *this;
    }
    style_iterator& operator-=(int n) {
        for (int i = 0; i < n; ++i) {
            operator--();
        }
        return *this;
    }
    style_iterator operator+(int n) const {
        auto s = *this;
        s += n;
        return s;
    }
    style_iterator operator-(int n) const {
        auto s = *this;
        s -= n;
        return s;
    }
    
    bool operator==(const style_iterator& other) const { return m_str == other.m_str; }
    bool operator!=(const style_iterator& other) const { return m_str != other.m_str; }
    bool operator<(const style_iterator& other) const { return m_str < other.m_str; }
    bool operator>(const style_iterator& other) const { return m_str > other.m_str; }
    bool operator<=(const style_iterator& other) const { return m_str <= other.m_str; }
    bool operator>=(const style_iterator& other) const { return m_str >= other.m_str; }

private:
    const char* m_str;
    std::vector<detail::style_impl>::iterator m_out;
};

} // namespace lined