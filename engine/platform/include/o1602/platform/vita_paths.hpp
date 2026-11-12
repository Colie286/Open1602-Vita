// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace o1602::platform::vita {

// Vita mount names are relative to std::filesystem, and Vita stat has no unique
// inode identity. Validate mounted paths without host absolute()/equivalent().
inline std::filesystem::path checked_save_path(const std::filesystem::path& path, std::error_code& error,
                                              bool scenario = false) {
    std::string text = path.generic_string();
    std::replace(text.begin(), text.end(), '\\', '/');
    if (text.starts_with("ux0:data/")) {
        text.insert(4, "/");
    }
    const auto normal = std::filesystem::path(text).lexically_normal();
    const std::string value = normal.generic_string();
    const std::string_view allowed = scenario ? "ux0:/data/open1602/scenarios/" : "ux0:/data/open1602/saves/";
    if (!value.starts_with(allowed) || value.size() <= allowed.size() || normal.filename().empty() ||
        value.find(':', 4) != std::string::npos) {
        error = std::make_error_code(std::errc::permission_denied);
        return {};
    }
    error.clear();
    return normal;
}

}  // namespace o1602::platform::vita
