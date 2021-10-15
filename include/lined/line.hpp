#pragma once

#include <string>
#include <string_view>
#include <variant>

namespace lined {

enum class line_error
{
    ctrl_c,
    ctrl_d,
    cancelled,
    syscall
};

class bad_line_access : public std::exception
{};

class line
{
public:
    line(std::string str) : m_line(std::move(str)) {}
    line(line_error e) : m_line(e) {}

    std::string& operator*() { return *std::get_if<std::string>(&m_line); }
    const std::string& operator*() const { return operator*(); }

    std::string* operator->() { return std::get_if<std::string>(&m_line); }
    const std::string* operator->() const { return operator->(); }

    bool has_value() const { return std::holds_alternative<std::string>(m_line); }
    operator bool() const { return has_value(); }

    std::string& value() {
        if (has_value()) {
            return std::get<0>(m_line);
        }

        throw bad_line_access{};
    }
    const std::string& value() const { return value(); }

    std::string value_or(std::string_view default_value) const {
        if (has_value()) {
            return std::get<0>(m_line);
        }

        return std::string(default_value);
    }

    line_error& error() { return *std::get_if<line_error>(&m_line); }
    const line_error& error() const { return error(); }

private:
    std::variant<std::string, line_error> m_line;
};

} // namespace lined