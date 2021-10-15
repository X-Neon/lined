#pragma once

#include <optional>
#include <stdexcept>
#include <string>

namespace lined::detail {

class utf8_decoder
{
public:
    std::optional<char32_t> write_char(uint8_t c) {
        if (m_chars_required == 0) {
            if (!(c & 0b10000000)) {
                return c;
            } else if (c & 0b11000000 && !(c & 0b00100000)) {
                m_code_point = c & 0b00011111;
                m_chars_required = 1;
            } else if (c & 0b11100000 && !(c & 0b00010000)) {
                m_code_point = c & 0b00001111;
                m_chars_required = 2;
            } else if (c & 0b11110000 && !(c & 0b00001000)) {
                m_code_point = c & 0b00000111;
                m_chars_required = 3;
            } else {
                throw std::exception{};
            }
        } else {
            m_code_point = (m_code_point << 6) + (c & 0b00111111);
            m_chars_required--;
            if (m_chars_required == 0) {
                return m_code_point;
            }
        }

        return {};
    }

private:
    int m_chars_required = 0;
    char32_t m_code_point = 0;
};

inline std::u32string decode_utf8(std::string_view str) {
    utf8_decoder decoder;
    std::u32string out;
    out.reserve(str.size());

    for (auto c : str) {
        if (auto wc = decoder.write_char(c)) {
            out.push_back(*wc);
        }
    }

    return out;
}

inline std::string encode_utf8(std::u32string_view str) {
    std::string out;
    out.reserve(str.size());
    for (auto code_point : str) {
        if (code_point < 0x80) {
            out.push_back(code_point);
        } else if (0x80 <= code_point && code_point < 0x0800) {
            out.reserve(out.size() + 2);
            out.push_back(0b11000000 + ((code_point >> 6) & 0b00011111));
            out.push_back(0b10000000 + (code_point & 0b00111111));
        } else if (0x0800 <= code_point && code_point < 0x010000) {
            out.reserve(out.size() + 3);
            out.push_back(0b11100000 + ((code_point >> 12) & 0b00001111));
            out.push_back(0b10000000 + ((code_point >> 6) & 0b00111111));
            out.push_back(0b10000000 + (code_point & 0b00111111));
        } else if (0x010000 <= code_point && code_point < 0x10FFFF) {
            out.reserve(out.size() + 4);
            out.push_back(0b11110000 + ((code_point >> 18) & 0b00000111));
            out.push_back(0b10000000 + ((code_point >> 12) & 0b00111111));
            out.push_back(0b10000000 + ((code_point >> 6) & 0b00111111));
            out.push_back(0b10000000 + (code_point & 0b00111111));
        } else {
            throw std::runtime_error("Input is not valid UTF-8");
        }
    }

    return out;
}

constexpr bool is_continuation_byte(uint8_t c) {
    return (c & 0b10000000) && !(c & 0b01000000);
}

} // namespace lined::detail