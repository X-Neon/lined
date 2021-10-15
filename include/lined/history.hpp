#pragma once

#include <deque>
#include <fstream>
#include <optional>
#include <string>

namespace lined::detail {

struct history_entry
{
    std::u32string original;
    std::optional<std::u32string> edited;

    std::u32string_view value() const {
        if (edited) {
            return *edited;
        }

        return original;
    }
};

class history
{
public:
    history(std::size_t max_size) : m_history(1), m_index(0), m_max_size(max_size) {}

    void load(const std::string& path) {
        std::ifstream file(path);
        std::string line;
        m_history.clear();

        while (std::getline(file, line)) {
            m_history.push_front({decode_utf8(line), {}});
        }
        m_history.push_front({{}, {}});

        m_index = 0;
        if (m_max_size < m_history.size() - 1) {
            m_max_size = m_history.size() - 1;
        }
    }

    void add(std::u32string_view str) {
        if (m_history.size() >= 2 && str == m_history[1].value()) {
            m_history[0] = {};
        } else {
            m_history[0] = history_entry{std::u32string(str), {}};
            m_history.push_front({});
            if (m_history.size() > m_max_size + 1) {
                m_history.pop_back();
            }
        }

        m_index = 0;
    }

    std::optional<std::u32string_view> record_and_go_back(std::u32string_view str) {
        if (m_index == m_history.size() - 1) {
            return {};
        }

        record_entry(str);
        m_index++;
        return current_entry();
    }

    std::optional<std::u32string_view> record_and_go_forward(std::u32string_view str) {
        if (m_index == 0) {
            return {};
        }

        record_entry(str);
        m_index--;
        return current_entry();
    }

    void save(const std::string& path) const {
        std::ofstream file(path);
        for (auto it = m_history.rbegin(); it != m_history.rend() - 1; ++it) {
            file << encode_utf8(it->original) << "\n";
        }
    }

private:
    void record_entry(std::u32string_view str) {
        if (m_history[m_index].original != str) {
            m_history[m_index].edited = str;
        } else {
            m_history[m_index].edited.reset();
        }
    }

    std::u32string_view current_entry() const {
        auto& h = m_history[m_index];
        if (h.edited) {
            return *h.edited;
        }

        return h.original;
    }

    std::deque<history_entry> m_history;
    std::size_t m_index;
    std::size_t m_max_size;
};

} // namespace lined::detail