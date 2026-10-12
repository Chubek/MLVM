#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mlvm::driver_detail {
// The optimizer plan's JSON vocabulary consists only of strings, arrays and
// objects. Reject other values at the reader boundary rather than coercing them.
struct PlanValue {
    using Array = std::vector<PlanValue>;
    using Object = std::map<std::string, PlanValue>;
    std::variant<std::string, Array, Object> data;
};
class PlanReader {
    std::string_view source;
    std::size_t position = 0;
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("optimizer JSON at byte " + std::to_string(position + 1) + ": " + message);
    }
    void space() {
        while (position < source.size() && std::string_view(" \t\r\n").find(source[position]) != std::string_view::npos) ++position;
    }
    bool take(char c) {
        space();
        if (position == source.size() || source[position] != c) return false;
        ++position; return true;
    }
    void require(char c) { if (!take(c)) fail(std::string("expected '") + c + "'"); }
    std::uint32_t hex() {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (position == source.size()) fail("truncated Unicode escape");
            const char c = source[position++];
            const auto digit = std::string_view("0123456789abcdef").find(c >= 'A' && c <= 'F' ? char(c - 'A' + 'a') : c);
            if (digit == std::string_view::npos) fail("invalid Unicode escape");
            value = value * 16 + static_cast<std::uint32_t>(digit);
        }
        return value;
    }
    std::string string() {
        require('"'); std::string out;
        while (position < source.size()) {
            auto c = static_cast<unsigned char>(source[position++]);
            if (c == '"') return out;
            if (c < 32) fail("control character in string");
            if (c >= 0x80) {
                const unsigned count = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
                if (!count || count - 1 > source.size() - position) fail("invalid UTF-8 string");
                const auto second = static_cast<unsigned char>(source[position]);
                if ((c == 0xe0 && second < 0xa0) || (c == 0xed && second >= 0xa0) ||
                    (c == 0xf0 && second < 0x90) || (c == 0xf4 && second >= 0x90)) fail("invalid UTF-8 string");
                out += static_cast<char>(c);
                for (unsigned i = 1; i < count; ++i) {
                    const auto next = static_cast<unsigned char>(source[position++]);
                    if (next < 0x80 || next > 0xbf) fail("invalid UTF-8 string");
                    out += static_cast<char>(next);
                }
                continue;
            }
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (position == source.size()) fail("truncated escape");
            switch (source[position++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                auto cp = hex();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (source.substr(position, 2) != "\\u") fail("missing low surrogate");
                    position += 2; const auto low = hex();
                    if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) fail("unpaired low surrogate");
                if (cp < 0x80) out += static_cast<char>(cp);
                else {
                    if (cp >= 0x10000) out += static_cast<char>(0xf0 | (cp >> 18));
                    if (cp >= 0x800) out += static_cast<char>((cp < 0x10000 ? 0xe0 : 0x80) | ((cp >> 12) & 63));
                    out += static_cast<char>((cp < 0x800 ? 0xc0 : 0x80) | ((cp >> 6) & 63));
                    out += static_cast<char>(0x80 | (cp & 63));
                }
                break;
            }
            default: fail("invalid escape");
            }
        }
        fail("unterminated string");
    }
    PlanValue value(unsigned depth) {
        if (depth > 32) fail("nesting exceeds 32");
        space();
        if (position < source.size() && source[position] == '"') return {string()};
        if (take('[')) {
            PlanValue::Array values;
            if (!take(']')) for (;;) {
                values.push_back(value(depth + 1));
                if (take(']')) break;
                require(',');
            }
            return {std::move(values)};
        }
        if (take('{')) {
            PlanValue::Object values;
            if (!take('}')) for (;;) {
                auto key = string(); require(':');
                if (!values.emplace(std::move(key), value(depth + 1)).second) fail("duplicate field");
                if (take('}')) break;
                require(',');
            }
            return {std::move(values)};
        }
        fail("expected string, array, or object");
    }
public:
    explicit PlanReader(std::string_view text) : source(text) {}
    PlanValue read() {
        auto result = value(0); space();
        if (position != source.size()) fail("trailing input");
        return result;
    }
};
} // namespace mlvm::driver_detail
