#pragma once

#include "completion.hpp"
#include "fd.hpp"
#include "history.hpp"
#include "line.hpp"
#include "terminal_line.hpp"
#include "utf8.hpp"
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/eventfd.h>
#include <termios.h>
#include <unistd.h>

namespace lined {

namespace detail::key {

constexpr char32_t null = 0;
constexpr char32_t ctrl_a = 1;
constexpr char32_t ctrl_b = 2;
constexpr char32_t ctrl_c = 3;
constexpr char32_t ctrl_d = 4;
constexpr char32_t ctrl_e = 5;
constexpr char32_t ctrl_f = 6;
constexpr char32_t ctrl_h = 8;
constexpr char32_t tab = 9;
constexpr char32_t ctrl_k = 11;
constexpr char32_t ctrl_l = 12;
constexpr char32_t enter = 13;
constexpr char32_t ctrl_n = 14;
constexpr char32_t ctrl_p = 16;
constexpr char32_t ctrl_t = 20;
constexpr char32_t ctrl_u = 21;
constexpr char32_t ctrl_w = 23;
constexpr char32_t esc = 27;
constexpr char32_t backspace = 127;

} // namespace detail::key

struct options
{
    int in_fd;
    int out_fd;
    int history_size;
    bool auto_history;
    style hint_style;
};

constexpr options default_options{STDIN_FILENO, STDOUT_FILENO, 100, true, {.fg = color::gray()}};

class line_reader;

class styled_string
{
    friend line_reader;

public:
    styled_string() {}
    styled_string(std::string_view str) : m_str(str) {}
    styled_string& operator<<(std::string_view str) {
        m_str += detail::terminal_string(str, m_cur_style);
        return *this;
    }
    styled_string& operator<<(style s) {
        m_cur_style = s;
        return *this;
    }

private:
    detail::terminal_string m_str;
    style m_cur_style{};
};

class line_reader
{
public:
    line_reader(options opt = default_options) :
        m_in(opt.in_fd), m_out(opt.out_fd), m_cancel_fd(eventfd(0, O_NONBLOCK)), m_history(opt.history_size),
        m_auto_history(opt.auto_history), m_hint_style(opt.hint_style) {}

    line getline(const styled_string& prompt) {
        if (!m_line) {
            activate(prompt);
        }

        while (true) {
            pollfd poll_items[] = {{m_in.get(), POLLIN}, {m_cancel_fd.get(), POLLIN}};
            poll(poll_items, std::size(poll_items), -1);

            if (poll_items[1].revents) {
                uint64_t dummy_read;
                [[maybe_unused]] auto unused = read(m_cancel_fd.get(), &dummy_read, sizeof(dummy_read));
                deactivate();
                return line_error::cancelled;
            }

            char char_read{};
            if (read(m_in.get(), &char_read, 1) == -1) {
                return line_error::syscall;
            }

            auto l = process_single_char(char_read);
            if (l) {
                deactivate();
                return *l;
            }
        }
    }

    line getline(std::string_view prompt) { return getline(prompt); }

    std::optional<line> getline_nonblocking(const styled_string& prompt) {
        if (!m_line) {
            activate(prompt);
        }

        uint64_t dummy_read;
        if (read(m_cancel_fd.get(), &dummy_read, sizeof(dummy_read)) == sizeof(dummy_read)) {
            deactivate();
            return line_error::cancelled;
        }

        char char_read;
        int n_read = read(m_in.get(), &char_read, 1);
        if (n_read == 0) {
            return {};
        } else if (n_read == -1) {
            return line_error::syscall;
        }

        auto l = process_single_char(char_read);
        if (l) {
            deactivate();
            return *l;
        }

        return {};
    }

    line getline_nonblocking(std::string_view prompt) { return getline_nonblocking(prompt); }

    void cancel() {
        uint64_t to_write = 1;
        write(m_cancel_fd.get(), &to_write, sizeof(to_write));
    }

    void clear_screen() {
        if (m_line) {
            m_line->clear_screen();
        } else {
            [[maybe_unused]] auto unused = write(m_out, "\x1b[2J\x1b[1;1H", 10);
        }
    }

    void mask() { m_masked = true; }
    void unmask() { m_masked = false; }

    void add_history(std::string_view str) { m_history.add(detail::decode_utf8(str)); }
    void save_history(const std::string& path) { m_history.save(path); }
    void load_history(const std::string& path) { m_history.load(path); }

    void set_completion(std::function<completion_callback_t> callback) { m_completion.set_callback(callback); }
    void set_hint(std::function<hint_callback_t> callback) { m_hint_callback = std::move(callback); }
    void set_colorization(std::function<color_callback_t> callback) { m_color_callback = std::move(callback); }

    void disable_output() {
        m_mutex.lock();
        if (m_line) {
            m_in.disable_raw_mode();
            m_line->erase_line_visual();
        }
    }

    void enable_output() {
        if (m_line) {
            m_in.enable_raw_mode();
            m_line->redraw();
        }
        m_mutex.unlock();
    }

private:
    void activate(const styled_string& prompt) {
        m_in.enable_raw_mode();
        m_line.emplace(m_out, prompt.m_str, m_hint_callback, m_color_callback, m_masked, m_hint_style);
    }

    void deactivate() {
        m_line.reset();
        m_in.disable_raw_mode();
    }

    std::optional<line> process_single_char(char c) {
        std::scoped_lock lk(m_mutex);

        auto optional_wc = m_decoder.write_char(c);
        if (!optional_wc) {
            return {};
        }

        auto wc = *optional_wc;
        if (m_chars_required) {
            m_chars_required--;
            m_escape_str.push_back(wc);
            if (!m_chars_required) {
                if (m_escape_str[0] == '[') {
                    if (m_escape_str.size() == 3 && m_escape_str[1] == '3' && m_escape_str[2] == '~') {
                        m_line->erase_current_character();
                        line_modified();
                    } else {
                        if (m_escape_str[1] == '3') {
                            m_chars_required++;
                            return {};
                        } else if (m_escape_str[1] == 'D') {
                            m_line->cursor_back();
                        } else if (m_escape_str[1] == 'C') {
                            m_line->cursor_forward();
                        } else if (m_escape_str[1] == 'H') {
                            m_line->cursor_home();
                        } else if (m_escape_str[1] == 'F') {
                            m_line->cursor_end();
                        } else if (m_escape_str[1] == 'A' && !m_masked) {
                            auto new_line = m_history.record_and_go_back(m_line->current_line());
                            if (new_line) {
                                m_line->set_line(*new_line);
                                line_modified();
                            }
                        } else if (m_escape_str[1] == 'B' && !m_masked) {
                            auto new_line = m_history.record_and_go_forward(m_line->current_line());
                            if (new_line) {
                                m_line->set_line(*new_line);
                                line_modified();
                            }
                        }
                    }
                } else if (m_escape_str[0] == 'O') {
                    if (m_escape_str[1] == 'H') {
                        m_line->cursor_home();
                    } else if (m_escape_str[1] == 'F') {
                        m_line->cursor_end();
                    }
                }

                m_escape_str.clear();
            }

            return {};
        }

        if (wc == detail::key::enter) {
            line_modified();
            if (m_line->empty()) {
                m_line->new_line();
            } else {
                if (m_auto_history) {
                    m_history.add(m_line->current_line());
                }

                return m_line->pop_line();
            }

        } else if (wc == detail::key::ctrl_d) {
            if (m_line->empty()) {
                line_modified();
                return line_error::ctrl_d;
            } else {
                m_line->erase_current_character();
                line_modified();
            }
        } else if (wc == detail::key::ctrl_c) {
            line_modified();
            return line_error::ctrl_c;
        } else if (wc == detail::key::backspace || wc == detail::key::ctrl_h) {
            m_line->erase_previous_character();
            line_modified();
        } else if (wc == detail::key::ctrl_u) {
            m_line->erase_line_backward();
            line_modified();
        } else if (wc == detail::key::ctrl_k) {
            m_line->erase_line_forward();
            line_modified();
        } else if (wc == detail::key::ctrl_a) {
            m_line->cursor_home();
        } else if (wc == detail::key::ctrl_e) {
            m_line->cursor_end();
        } else if (wc == detail::key::ctrl_t) {
            m_line->swap_characters();
            line_modified();
        } else if (wc == detail::key::ctrl_w) {
            m_line->erase_previous_word();
            line_modified();
        } else if (wc == detail::key::ctrl_l) {
            m_line->clear_screen();
        } else if (wc == detail::key::tab && !m_masked) {
            auto completion = m_completion.get_next_completion(m_line->current_line());
            if (completion) {
                m_line->set_line(*completion);
            }
        } else if (wc == detail::key::esc) {
            m_chars_required = 2;
        } else {
            m_line->insert_character(wc);
            line_modified();
        }

        return {};
    }

    void line_modified() { m_completion.reset(); }

    detail::utf8_decoder m_decoder;
    termios m_initial_termios;
    detail::input_fd m_in;
    int m_out;
    detail::unique_fd m_cancel_fd;
    std::optional<detail::terminal_line> m_line;
    int m_chars_required = 0;
    std::string m_escape_str;
    detail::history m_history;
    bool m_auto_history;
    bool m_masked = false;
    detail::completion m_completion;
    std::function<hint_callback_t> m_hint_callback;
    std::function<color_callback_t> m_color_callback;
    style m_hint_style;
    std::mutex m_mutex;
};

class scoped_disable
{
public:
    scoped_disable(line_reader& reader) : m_reader(&reader) { reader.disable_output(); }
    scoped_disable(scoped_disable&& other) noexcept : m_reader(std::exchange(other.m_reader, nullptr)) {}
    scoped_disable& operator=(scoped_disable&& other) noexcept {
        if (m_reader == other.m_reader) {
            m_reader = std::exchange(other.m_reader, nullptr);
        } else {
            std::swap(m_reader, other.m_reader);
        }

        return *this;
    }
    ~scoped_disable() {
        if (m_reader) {
            m_reader->enable_output();
        }
    }

private:
    line_reader* m_reader;
};

} // namespace lined
