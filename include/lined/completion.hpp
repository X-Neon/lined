#pragma once

#include "utf8.hpp"
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lined {

using completion_callback_t = std::vector<std::string>(std::string_view);

namespace detail {

class completion
{
public:
    void set_callback(std::function<completion_callback_t> callback) { m_callback = callback; }

    std::optional<std::u32string_view> get_next_completion(std::u32string_view input) {
        if (!m_completions) {
            if (!get_completions(input)) {
                return {};
            }
        }

        m_index++;
        if (m_index == m_completions->size()) {
            m_index = 0;
        }

        return (*m_completions)[m_index];
    }

    void reset() { m_completions.reset(); }

private:
    bool get_completions(std::u32string_view input) {
        if (!m_callback) {
            return false;
        }

        auto completions = m_callback(encode_utf8(input));
        if (completions.empty()) {
            return false;
        }

        std::vector<std::u32string> unicode_completions;
        unicode_completions.reserve(completions.size() + 1);
        for (auto& c : completions) {
            unicode_completions.push_back(decode_utf8(c));
        }

        m_completions = std::move(unicode_completions);
        m_completions->emplace_back(input);
        m_index = m_completions->size() - 1;
        return true;
    }

    std::function<completion_callback_t> m_callback;
    std::optional<std::vector<std::u32string>> m_completions;
    std::size_t m_index;
};

} // namespace detail

} // namespace lined