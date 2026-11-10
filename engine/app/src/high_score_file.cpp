// SPDX-License-Identifier: GPL-3.0-or-later
#include "high_score_file.hpp"

#include <cstddef>

#include "o1602/core/text.hpp"

namespace o1602::app {

std::filesystem::path file_name_of(std::string_view cp1252) {
    std::string utf8 = core::cp1252_to_utf8(cp1252);
    std::erase_if(utf8, [](char c) {
        return static_cast<unsigned char>(c) < 0x20 ||
               std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos;
    });
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::filesystem::path high_score_file(const sim::GameIdentity& game, const std::vector<std::string>& labels,
                                      bool multiplayer) {
    std::string name = game.name;
    if (game.campaign != sim::GameIdentity::kNone) {
        const std::size_t line = std::size_t{5} * game.campaign + game.ranking;
        if (line < labels.size() && !labels[line].empty()) {
            name = labels[line];
        }
    }
    if (name.empty()) {
        return {};
    }
    std::filesystem::path file = file_name_of(name);
    if (file.empty()) {
        return {};
    }
    file += multiplayer ? ".hsm" : ".hss";
    return game.custom ? std::filesystem::path(u8"Eigene Szenarien") / file : file;
}

}  // namespace o1602::app
