// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/text_format.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace o1602::ui {

namespace {

std::string as_integer(const TextArg& a) {
    if (const auto* i = std::get_if<std::int64_t>(&a)) {
        return std::to_string(*i);
    }
    if (const auto* d = std::get_if<double>(&a)) {
        return std::to_string(static_cast<std::int64_t>(std::trunc(*d)));
    }
    return std::string(std::get<std::string_view>(a));
}

std::string as_real(const TextArg& a, int precision) {
    double v = 0.0;
    if (const auto* i = std::get_if<std::int64_t>(&a)) {
        v = static_cast<double>(*i);
    } else if (const auto* d = std::get_if<double>(&a)) {
        v = *d;
    } else {
        return std::string(std::get<std::string_view>(a));
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", precision, v);
    return buf;
}

std::string as_text(const TextArg& a) {
    if (const auto* s = std::get_if<std::string_view>(&a)) {
        return std::string(*s);
    }
    return as_integer(a);
}

}  // namespace

std::string format_text(std::string_view pattern, std::initializer_list<TextArg> args) {
    const std::vector<TextArg> list(args);
    std::size_t next = 0;
    std::string out;
    out.reserve(pattern.size() + 16);
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c != '%' || i + 1 >= pattern.size()) {
            out += c;
            continue;
        }
        if (pattern[i + 1] == '%') {
            out += '%';
            ++i;
            continue;
        }
        // Flags, width and precision: digits, '-', '+', ' ', '.'.
        std::size_t j = i + 1;
        int precision = 6;
        bool dot = false;
        int number = 0;
        while (j < pattern.size() && (pattern[j] == '-' || pattern[j] == '+' || pattern[j] == ' ' ||
                                      pattern[j] == '.' || (pattern[j] >= '0' && pattern[j] <= '9'))) {
            if (pattern[j] == '.') {
                dot = true;
                number = 0;
            } else if (pattern[j] >= '0' && pattern[j] <= '9' && number < 100) {
                number = number * 10 + (pattern[j] - '0');
            }
            ++j;
        }
        if (dot) {
            precision = number;
        }
        if (j >= pattern.size()) {
            out.append(pattern.substr(i));
            break;
        }
        const char conv = pattern[j];
        const TextArg* arg = next < list.size() ? &list[next] : nullptr;
        switch (conv) {
            case 'd':
            case 'i':
            case 'u':
                out += arg != nullptr ? as_integer(*arg) : std::string();
                ++next;
                break;
            case 'f':
                out += arg != nullptr ? as_real(*arg, precision) : std::string();
                ++next;
                break;
            case 's':
                out += arg != nullptr ? as_text(*arg) : std::string();
                ++next;
                break;
            default:
                // Not a conversion we know: keep it as written.
                out.append(pattern.substr(i, j - i + 1));
                break;
        }
        i = j;
    }
    return out;
}

}  // namespace o1602::ui
