#pragma once

#include "utf8.hpp"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace lined::detail {

class unique_fd
{
public:
    unique_fd() : m_fd(-1) {}
    unique_fd(int fd) : m_fd(fd) {}

    unique_fd(unique_fd&& other) noexcept {
        m_fd = other.m_fd;
        other.m_fd = -1;
    }
    unique_fd& operator=(unique_fd&& other) noexcept {
        if (m_fd != other.m_fd) {
            if (m_fd != -1) {
                close(m_fd);
            }
            m_fd = other.m_fd;
            other.m_fd = -1;
        }
        return *this;
    }

    ~unique_fd() {
        if (m_fd != -1) {
            close(m_fd);
        }
    }

    int get() const noexcept { return m_fd; }

private:
    int m_fd;
};

class input_fd
{
public:
    input_fd() : m_active(false), m_fd(-1), m_initially_blocking(false) {}
    input_fd(int fd) : m_active(false), m_fd(fd) {
        m_initially_blocking = fcntl(m_fd, F_GETFL, O_NONBLOCK) == 0;
        fcntl(m_fd, F_SETFL, O_NONBLOCK, 1);
    }

    input_fd(input_fd&& other) noexcept {
        m_fd = other.m_fd;
        other.m_fd = -1;
    }
    input_fd& operator=(input_fd&& other) noexcept {
        if (m_fd != other.m_fd) {
            cleanup();
            m_fd = other.m_fd;
            other.m_fd = -1;
        }
        return *this;
    }

    ~input_fd() { cleanup(); }

    void enable_raw_mode() {
        tcgetattr(m_fd, &m_initial_termios);

        termios raw = m_initial_termios;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;

        tcsetattr(m_fd, TCSAFLUSH, &raw);
        m_active = true;
    }

    void disable_raw_mode() {
        tcsetattr(m_fd, TCSAFLUSH, &m_initial_termios);
        m_active = false;
    }

    int get() const noexcept { return m_fd; }

private:
    void cleanup() {
        if (m_fd == -1) {
            return;
        }

        if (m_initially_blocking) {
            fcntl(m_fd, F_SETFL, O_NONBLOCK, 0);
        }

        if (m_active) {
            disable_raw_mode();
        }
    }

    bool m_active;
    int m_fd;
    bool m_initially_blocking;
    termios m_initial_termios;
};

class output_fd
{
public:
    output_fd(int fd) : m_fd(fd) {}

    void write(std::string_view str) {
        if (str.size()) {
            [[maybe_unused]] auto unused = ::write(m_fd, str.data(), str.size());
        }
    }
    void write(char c) { [[maybe_unused]] auto unused = ::write(m_fd, &c, 1); }
    void write(std::u32string_view str) { write(encode_utf8(str)); }
    void write(char32_t c) { write(encode_utf8({&c, 1})); }

    int fd() const { return m_fd; }

private:
    int m_fd;
};

} // namespace lined::detail