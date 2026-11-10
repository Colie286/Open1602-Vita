// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/localized_names.hpp"

#include <algorithm>

namespace o1602::assets {

std::string name_in(const LocalizedNames& names, std::string_view language, std::string_view fallback) {
    for (const std::string_view wanted : {language, std::string_view("en")}) {
        const auto it = std::find_if(names.begin(), names.end(), [&](const auto& n) { return n.first == wanted; });
        if (it != names.end()) {
            return it->second;
        }
    }
    return names.empty() ? std::string(fallback) : names.front().second;
}

std::string_view name_key_language(std::string_view key) noexcept {
    constexpr std::string_view kPrefix = "name.";
    if (!key.starts_with(kPrefix)) {
        return {};
    }
    const std::string_view code = key.substr(kPrefix.size());
    const bool ok = !code.empty() && code.size() <= 16 && std::all_of(code.begin(), code.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || c == '-';
    });
    return ok ? code : std::string_view{};
}

}  // namespace o1602::assets
