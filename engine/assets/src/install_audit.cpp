// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/install_audit.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/game_content.hpp"
#include "o1602/core/text.hpp"
#include "o1602/core/file.hpp"
#include <array>
#include <fstream>
#include <map>
#include <sstream>
#include <algorithm>
namespace o1602::assets {
namespace {
std::string key(std::string s) {
    for (char& c : s) { if (c >= 'A' && c <= 'Z') c += 'a'-'A'; }
    return s;
}
std::string category(const std::string& path) {
    const auto p = key(path);
    if (p.ends_with(".smk") || p.ends_with(".bk2")) return "video";
    if (p.ends_with(".wav")) return p.find("music") != p.npos ? "music" : "sound_or_speech";
    if (p.ends_with(".bsh") || p.ends_with(".col")) return "graphics";
    if (p.ends_with(".gad")) return "interface";
    if (p.find("text.cod") != p.npos || p.ends_with(".rda") || p.starts_with("loca/")) return "localization";
    if (p == "haeuser.cod" || p == "figuren.cod") return "simulation";
    return "other_optional_or_scenario";
}
struct File { std::string path; std::uint64_t size = 0, hash = 0; bool readable = false; };
}
core::Status audit_installation(const std::filesystem::path& root, std::string_view language,
                               const std::filesystem::path& output, const std::filesystem::path& reference,
                               bool hashes) {
    // Reject output beneath the input, including symlink aliases. Never create a file there.
    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) return core::make_error(core::ErrorCode::IoError, "cannot resolve original directory");
    const auto canonical_out = std::filesystem::weakly_canonical(output, ec);
    if (ec) return core::make_error(core::ErrorCode::IoError, "cannot resolve audit output");
    auto rel = canonical_out.lexically_relative(canonical_root);
    if (!rel.empty() && *rel.begin() != "..")
        return core::make_error(core::ErrorCode::IoError, "audit output must be outside original directory");
    std::ofstream out(output);
    if (!out) return core::make_error(core::ErrorCode::IoError, "cannot write installation report");
    out << "# Open1602 installation audit v1; FNV1a64 copy checksums; hashes=" << hashes << '\n';
    auto game = GameDirectory::open(root);
    if (game) {
        if (language != "auto") game->choose_language(language);
        out << "# edition=" << (game->resolve("ahe/a1he0.rda") ? "History" : "Classic/Gold")
            << " localized=" << (game->localized_root().empty() ? "(base)" : core::path_to_utf8(game->localized_root().lexically_relative(root)))
            << " requested_language=" << language << " text_language=" << game->text_language()
            << " fingerprint=" << content_fingerprint(*game) << '\n';
        // Paths taken from GameDirectory, Palette, SpriteSheet, load_gad, TextTable, video and cursor loaders.
        for (auto path : {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col", "gfx/stadtfld.bsh",
                          "mgfx/stadtfld.bsh", "sgfx/stadtfld.bsh", "Text.cod", "Gaddata/CTRL.GAD",
                          "Gaddata/LEIST.GAD", "ahe/a1he0.rda", "VideoSmk/58.bk2", "Language.dll"}) {
            const auto resolved = game->resolve(path);
            out << "resolved\t" << path << '\t' << (resolved ? core::path_to_utf8(resolved->lexically_relative(root)) : "MISSING") << '\n';
        }
        out << "# Language.dll is optional desktop cursor art; ahe and bk2 are edition-dependent.\n";
    } else out << "# loader_error=" << game.error().message << '\n';
    std::map<std::string, File> files;
    std::array<char, 65536> buffer{};
    std::filesystem::recursive_directory_iterator it(root, ec), last;
    unsigned unreadable = 0, differences = 0;
    for (; !ec && it != last; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        File f;
        f.path = core::path_to_utf8(it->path().lexically_relative(root));
        if (f.path.find_first_of("\t\r\n") != f.path.npos) { ++unreadable; continue; }
        f.size = it->file_size(ec);
        std::ifstream input(it->path(), std::ios::binary);
        f.hash = 14695981039346656037ULL;
        std::uint64_t read = 0;
        while (input) {
            input.read(buffer.data(), buffer.size());
            const auto n = input.gcount(); read += static_cast<std::uint64_t>(n);
            if (hashes) for (std::streamsize i=0; i<n; ++i) {
                f.hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]); f.hash *= 1099511628211ULL;
            }
        }
        f.readable = input.eof() && !input.bad() && read == f.size;
        if (!f.readable) ++unreadable;
        out << "file\t" << f.path << '\t' << f.size << '\t' << (hashes ? f.hash : 0) << '\t'
            << (f.readable ? "OK" : "UNREADABLE") << '\t' << category(f.path) << '\n';
        files.emplace(key(f.path), std::move(f));
    }
    if (ec) { ++unreadable; out << "# traversal_error=" << ec.message() << '\n'; }
    if (!reference.empty()) {
        std::ifstream ref(reference); if (!ref) return core::make_error(core::ErrorCode::IoError, "cannot read audit reference");
        std::string line;
        while (std::getline(ref, line)) {
            std::istringstream row(line); std::string kind, path, size, hash;
            if (!std::getline(row, kind, '\t') || kind != "file") continue;
            if (!std::getline(row,path,'\t') || !std::getline(row,size,'\t') || !std::getline(row,hash,'\t')) continue;
            const auto found=files.find(key(path));
            const char* status = found==files.end() ? "MISSING" : !found->second.readable ? "UNREADABLE" :
                std::to_string(found->second.size)!=size ? "SIZE_MISMATCH" :
                hashes && hash!="0" && std::to_string(found->second.hash)!=hash ? "HASH_MISMATCH" : "OK";
            if (std::string_view(status)!="OK") { ++differences; out << "compare\t" << path << '\t' << status << '\t' << category(path) << '\n'; }
        }
        if (!ref.eof()) return core::make_error(core::ErrorCode::IoError, "audit reference read failed");
    }
    out << "# files=" << files.size() << " unreadable=" << unreadable << " differences=" << differences
        << " reference=" << !reference.empty() << "; no reference means completeness is unproven\n";
    out.flush();
    if (!out) return core::make_error(core::ErrorCode::IoError, "audit report write failed");
    return core::Ok{};
}
}
