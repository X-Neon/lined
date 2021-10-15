#pragma once

#include "style.hpp"
#include "wcwidth9.hpp"
#include <numeric>
#include <string_view>
#include <vector>

namespace lined::detail {

using width_t = int8_t;

class terminal_string
{
public:
    terminal_string() : m_total_width(0) {};

    terminal_string(std::string_view str, style default_style = {}) :
        terminal_string(decode_utf8(str), default_style) {}

    terminal_string(std::u32string_view str, style default_style = {}) {
        m_buf = str;
        m_total_width = 0;
        m_width.reserve(str.size());
        for (auto wc : str) {
            auto w = wcwidth9_norm(wc);
            m_width.push_back(w);
            m_total_width += w;
        }
        m_style = std::vector<style_impl>(str.size(), default_style);
    }

    const char32_t& operator[](std::size_t i) const { return m_buf[i]; }

    terminal_string operator+(const terminal_string& other) const {
        terminal_string result;
        result.m_buf = m_buf + other.m_buf;
        result.m_width = m_width;
        result.m_width.insert(result.m_width.end(), other.m_width.begin(), other.m_width.end());
        result.m_style.insert(result.m_style.end(), other.m_style.begin(), other.m_style.end());
        result.m_total_width = m_total_width + other.m_total_width;
        return result;
    }

    terminal_string& operator+=(const terminal_string& other) {
        m_buf.insert(m_buf.end(), other.m_buf.begin(), other.m_buf.end());
        m_width.insert(m_width.end(), other.m_width.begin(), other.m_width.end());
        m_style.insert(m_style.end(), other.m_style.begin(), other.m_style.end());
        m_total_width += other.m_total_width;
        return *this;
    }

    const auto& buf() const { return m_buf; }
    const auto& width() const { return m_width; }
    auto& style() { return m_style; }
    auto total_width() const { return m_total_width; }
    std::string to_string() const { return encode_utf8(m_buf); }
    std::size_t size() const { return m_buf.size(); }
    bool empty() const { return m_buf.empty(); }

    void clear() {
        m_buf.clear();
        m_width.clear();
        m_style.clear();
    }

    terminal_string substr(std::size_t begin, std::size_t end) const {
        terminal_string sub;
        sub.m_buf = m_buf.substr(begin, end - begin);
        sub.m_width = std::vector<width_t>(m_width.begin() + begin, m_width.begin() + end);
        sub.m_style = std::vector<style_impl>(m_style.begin() + begin, m_style.begin() + end);
        sub.m_total_width = std::accumulate(sub.m_width.begin(), sub.m_width.end(), 0);
        return sub;
    }

    void insert(std::size_t i, char32_t c) {
        m_buf.insert(m_buf.begin() + i, c);

        auto w = wcwidth9_norm(c);
        m_width.insert(m_width.begin() + i, w);
        m_style.insert(m_style.begin() + i, style_impl{});
        m_total_width += w;
    }

    void erase(std::size_t begin, std::size_t end) {
        auto w = std::accumulate(m_width.begin() + begin, m_width.begin() + end, 0);
        m_total_width -= w;
        m_buf.erase(m_buf.begin() + begin, m_buf.begin() + end);
        m_width.erase(m_width.begin() + begin, m_width.begin() + end);
        m_style.erase(m_style.begin() + begin, m_style.begin() + end);
    }

    void swap(std::size_t a, std::size_t b) {
        std::swap(m_buf[a], m_buf[b]);
        std::swap(m_width[a], m_width[b]);
        std::swap(m_style[a], m_style[b]);
    }

private:
    std::u32string m_buf;
    std::vector<width_t> m_width;
    std::vector<style_impl> m_style;
    int m_total_width;
};

} // namespace lined::detail
