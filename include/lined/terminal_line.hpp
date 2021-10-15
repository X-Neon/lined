#pragma once

#include "fd.hpp"
#include "style.hpp"
#include "terminal_string.hpp"
#include "utf8.hpp"
#include "wcwidth9.hpp"
#include <algorithm>
#include <numeric>
#include <sys/ioctl.h>
#include <vector>

namespace lined {

using hint_callback_t = std::string(std::string_view);
using color_callback_t = void(std::string_view, style_iterator);

namespace detail {

struct term_state
{
    terminal_string buf;
    int column;
};

class terminal_line
{
public:
    terminal_line(int fd, const terminal_string& prompt, const std::function<hint_callback_t>& hint_callback,
                  const std::function<color_callback_t>& color_callback, bool masked, style hint_style) :
        m_fd(fd),
        m_prompt(prompt), m_hint_callback(hint_callback), m_color_callback(color_callback), m_masked(masked),
        m_hint_style(hint_style) {

        winsize ws;
        if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
            m_columns = 79 - m_prompt.total_width();
        } else {
            m_columns = ws.ws_col - m_prompt.total_width() - 1;
        }

        sync();
    }

    ~terminal_line() {
        if (!m_popped) {
            m_fd.write("\r\x1b[2K");
        }
    }

    void cursor_back() {
        if (m_position == 0) {
            return;
        }

        m_position--;
        sync();
    }

    void cursor_forward() {
        if (m_position == m_buf.size()) {
            return;
        }

        m_position++;
        sync();
    }

    void cursor_home() {
        m_position = 0;
        sync();
    }

    void cursor_end() {
        m_position = m_buf.size();
        sync();
    }

    void insert_character(char32_t to_insert) {
        m_buf.insert(m_position, to_insert);
        m_position++;
        modified_sync();
    }

    void erase_previous_character() {
        if (m_position == 0) {
            return;
        }

        m_buf.erase(m_position - 1, m_position);
        m_position--;
        modified_sync();
    }

    void erase_current_character() {
        if (m_position == m_buf.size()) {
            return;
        }

        m_buf.erase(m_position, m_position + 1);
        modified_sync();
    }

    void erase_line_backward() {
        m_buf.erase(0, m_position);
        m_position = 0;
        modified_sync();
    }

    void erase_line_forward() {
        m_buf.erase(m_position, m_buf.size());
        modified_sync();
    }

    void swap_characters() {
        if (m_position == 0 || m_buf.size() < 2) {
            return;
        }

        if (m_position == m_buf.size()) {
            m_position--;
        }

        m_buf.swap(m_position, m_position - 1);
        m_position++;
        modified_sync();
    }

    void erase_previous_word() {
        if (m_position == 0) {
            return;
        }

        int i = m_position - 1;
        while (i > 0 && m_buf[i] == ' ') {
            i--;
        }
        while (i > 0 && m_buf[i] != ' ') {
            i--;
        }
        int erase_start = i == 0 ? 0 : i + 1;

        m_buf.erase(erase_start, m_position);
        m_position = erase_start;
        modified_sync();
    }

    void clear_screen() {
        m_fd.write("\x1b[2J\x1b[1;1H");
        redraw();
    }

    std::string pop_line() {
        m_popped = true;
        m_hint.clear();
        sync();
        m_fd.write("\r\n");
        return m_buf.to_string();
    }

    void new_line() {
        m_fd.write("\r\n");
        redraw();
    }

    void set_line(std::u32string_view str) {
        m_position = str.length();
        m_buf = terminal_string(str);
        modified_sync();
    }

    void erase_line_visual() { m_fd.write("\r\x1b[2K"); }

    void redraw() {
        m_prev = {};
        sync();
    }

    std::u32string_view current_line() const { return m_buf.buf(); }

    bool empty() const { return m_buf.empty(); }

private:
    void modified_sync() {
        if (!m_masked) {
            if (m_hint_callback) {
                auto hint = m_hint_callback(m_buf.to_string());
                m_hint = terminal_string(hint, m_hint_style);
            }

            if (m_color_callback) {
                auto str = m_buf.to_string();
                std::vector<style_impl> style_vec(m_buf.size());
                style_iterator iter(str.data(), style_vec);
                m_color_callback(str, iter);
                m_buf.style() = std::move(style_vec);
            }
        }

        sync();
    }

    void sync() {
        auto state = current_state();

        std::size_t i = 0;
        std::size_t i_col = 0;
        std::size_t j = 0;
        std::size_t j_col = 0;
        std::size_t start_col = 0;
        std::size_t end_col = 0;
        std::size_t start_update = i;
        std::size_t end_update = -1UL;
        bool first = true;
        while (i < state.buf.size() && j < m_prev.buf.size()) {
            if (i_col == j_col) {
                if (state.buf[i] != m_prev.buf[j] || state.buf.style()[i] != m_prev.buf.style()[j]) {
                    if (first) {
                        first = false;
                        start_update = i;
                        start_col = i_col;
                    }
                    end_update = i;
                    end_col = i_col + state.buf.width()[i];
                }
                i_col += state.buf.width()[i];
                j_col += m_prev.buf.width()[j];
                i++;
                j++;
            } else {
                if (i_col > j_col) {
                    j_col += m_prev.buf.width()[j];
                    j++;
                } else {
                    end_update = i;
                    end_col = i_col + state.buf.width()[i];
                    i_col += state.buf.width()[i];
                    i++;
                }
            }
        }

        if (i < state.buf.size()) {
            end_update = state.buf.size() - 1;
            end_col = state.buf.total_width();
        }

        int current_column = m_prev.column;

        if (end_update != -1UL) {
            move_cursor_to(start_col, current_column);
            auto to_write = std::u32string_view(state.buf.buf()).substr(start_update, end_update - start_update + 1);
            auto style_it = state.buf.style().begin() + start_update;
            auto style_end = state.buf.style().begin() + end_update + 1;

            while (!to_write.empty()) {
                auto new_style = std::find_if(style_it, style_end, [this](auto s) { return s != m_current_style; });
                auto n = std::distance(style_it, new_style);
                m_fd.write(to_write.substr(0, n));
                to_write = to_write.substr(n);

                if (new_style != style_end) {
                    m_fd.write(style_impl::switch_to(m_current_style, *new_style));
                    style_it = new_style;
                }
            }

            current_column = end_col;
            m_fd.write(style_impl::switch_to(m_current_style, style{}));
        }

        if (j < m_prev.buf.size()) {
            move_cursor_to(state.buf.total_width(), current_column);
            m_fd.write("\x1b[K");
        }

        move_cursor_to(state.column, current_column);

        m_prev = std::move(state);
    }

    void move_cursor_to(int column, int& prev) {
        int n = column - prev;
        prev = column;
        if (n > 0) {
            m_fd.write("\x1b[" + std::to_string(n) + "C");
        } else if (n < 0) {
            m_fd.write("\x1b[" + std::to_string(-n) + "D");
        }
    }

    term_state current_state() {
        std::vector<width_t> mask_width;
        if (m_masked) {
            mask_width = std::vector<width_t>(m_buf.size(), 1);
        }
        const auto& width = m_masked ? mask_width : m_buf.width();

        if (m_position < m_view_start) {
            m_view_start = m_position;
        }

        auto [fwd_end, fwd_width] = iterate_view_forward(m_view_start, m_columns, width);
        std::size_t end;
        int total_width;
        if (m_position > fwd_end) {
            auto [start, bkwd_width] = iterate_view_backward(m_position, m_columns, width);
            m_view_start = start;
            end = m_position;
            total_width = bkwd_width;
        } else {
            auto [start, bkwd_width] = iterate_view_backward(m_view_start, m_columns - fwd_width, width);
            m_view_start = start;
            end = fwd_end;
            total_width = fwd_width + bkwd_width;
        }

        int column = std::accumulate(width.begin() + m_view_start, width.begin() + m_position, m_prompt.total_width());
        auto view = m_prompt;
        view += m_masked ? terminal_string(std::string(m_buf.size(), '*')) : m_buf.substr(m_view_start, end);

        if (end == m_buf.size()) {
            auto [hint_view_end, hint_view_width] = iterate_view_forward(0, m_columns - total_width, m_hint.width());
            if (hint_view_end > 0) {
                view += m_hint.substr(0, hint_view_end);
                total_width += hint_view_width;
            }
        }

        return {std::move(view), column};
    }

    std::pair<std::size_t, int> iterate_view_forward(int start, int max_width,
                                                     const std::vector<width_t>& width) const {
        std::size_t i = start;
        int w = 0;
        while (i < width.size() && w + width[i] <= max_width) {
            w += width[i];
            i++;
        }

        return {i, w};
    }

    std::pair<std::size_t, int> iterate_view_backward(int start, int max_width,
                                                      const std::vector<width_t>& width) const {
        int i = start;
        int w = 0;
        while (i >= 1 && w + width[i - 1] <= max_width) {
            w += width[i - 1];
            i--;
        }

        return {i, w};
    }

    output_fd m_fd;
    int m_columns;
    terminal_string m_prompt;
    terminal_string m_buf;
    std::function<hint_callback_t> m_hint_callback;
    terminal_string m_hint;
    std::function<color_callback_t> m_color_callback;
    std::size_t m_position = 0;
    std::size_t m_view_start = 0;
    term_state m_prev = {};
    style_impl m_current_style = style{};
    bool m_popped = false;
    bool m_masked;
    style m_hint_style;
};

} // namespace detail

} // namespace lined