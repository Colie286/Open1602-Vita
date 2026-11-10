// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/game_content.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <set>
#include <system_error>
#include <utility>

#include "o1602/core/data_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/hash.hpp"

namespace o1602::assets {

namespace {

/// Mixed in for a file that is missing or cannot be read: not the same as an empty one.
constexpr std::uint64_t kUnreadable = 0x9E3779B97F4A7C15ULL;

std::uint64_t hash_text(std::string_view text, std::uint64_t seed) {
    return core::xxh64(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), seed);
}

std::uint64_t hash_number(std::uint64_t value, std::uint64_t seed) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
    return core::xxh64(bytes, seed);
}

/// The numbers of a valid version.
std::array<std::uint32_t, 4> version_numbers(std::string_view version) noexcept {
    std::array<std::uint32_t, 4> numbers{};
    std::size_t n = 0;
    std::size_t start = 0;
    while (n < numbers.size() && start <= version.size()) {
        const std::size_t dot = std::min(version.find('.', start), version.size());
        std::from_chars(version.data() + start, version.data() + dot, numbers[n]);
        ++n;
        start = dot + 1;
    }
    return numbers;
}

std::string where(const ModPackage& package, std::size_t line) {
    return package.id + "/" + std::string(kManifestFile) + " line " + std::to_string(line) + ": ";
}

/// The id after a prefix such as "depends.", or nothing.
std::optional<std::string> key_after(std::string_view key, std::string_view prefix) {
    if (key.size() <= prefix.size() || key.substr(0, prefix.size()) != prefix) {
        return std::nullopt;
    }
    return std::string(key.substr(prefix.size()));
}

/// The regular files of a package's folder and its subfolders: their paths relative to it (UTF-8,
/// '/' between folders) and on disk, ordered by the bytes of the relative paths.
std::vector<std::pair<std::string, std::filesystem::path>> files_of(const std::filesystem::path& folder) {
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            const std::u8string relative = it->path().lexically_relative(folder).generic_u8string();
            files.emplace_back(std::string(relative.begin(), relative.end()), it->path());
        }
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return files;
}

/// Whether `id` can be a package's id and so the start of a folder's name.
bool valid_package_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > kMaxPackageIdLength || id == "." || id == "..") {
        return false;
    }
    return std::none_of(id.begin(), id.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7F || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
               c == '<' || c == '>' || c == '|';
    });
}

std::string hex16(std::uint64_t value) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (std::size_t i = 0; i < 16; ++i) {
        out[15 - i] = kDigits[(value >> (4 * i)) & 0xFU];
    }
    return out;
}

}  // namespace

std::uint64_t content_fingerprint(const GameDirectory& game) {
    std::uint64_t h = 0;
    for (const char* file : {"haeuser.cod", "figuren.cod"}) {
        if (const auto path = game.resolve(file)) {
            if (const auto bytes = core::read_file(*path)) {
                h = core::xxh64(*bytes, h);
                continue;
            }
        }
        h = core::xxh64({}, h ^ kUnreadable);  // a missing file is not an empty one
    }
    return h;
}

std::vector<ModPackage> mod_packages(const std::filesystem::path& mods) {
    std::vector<ModPackage> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(mods, ec)) {
        if (entry.is_directory(ec)) {
            ModPackage package;
            package.id = core::path_to_utf8(entry.path().filename());
            package.folder = entry.path();
            package.name = package.id;
            out.push_back(std::move(package));
        }
    }
    std::sort(out.begin(), out.end(), [](const ModPackage& a, const ModPackage& b) { return a.id < b.id; });
    return out;
}

bool valid_version(std::string_view version) noexcept {
    if (version.empty() || version.size() > kMaxPackageVersionLength) {
        return false;
    }
    std::size_t numbers = 0;
    std::size_t digits = 0;
    for (const char c : version) {
        if (c == '.') {
            if (digits == 0) {
                return false;
            }
            ++numbers;
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            if (++digits > 9) {
                return false;
            }
        } else {
            return false;
        }
    }
    return digits != 0 && numbers < 4;
}

int compare_versions(std::string_view a, std::string_view b) noexcept {
    const auto x = version_numbers(a);
    const auto y = version_numbers(b);
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (x[i] != y[i]) {
            return x[i] < y[i] ? -1 : 1;
        }
    }
    return 0;
}

core::Status read_manifest(ModPackage& package, std::string_view utf8, std::vector<std::string>& warnings) {
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        return core::make_error(tables.error().code,
                                package.id + "/" + std::string(kManifestFile) + " " + tables.error().message);
    }
    ModPackage read = package;
    for (const core::DataTable& table : *tables) {
        if (!table.name.empty()) {
            warnings.push_back(where(package, table.line) + "a manifest has no tables; [" + table.name +
                               "] is passed over");
            continue;
        }
        for (const core::DataEntry& e : table.entries) {
            const std::string* text = e.value.string();
            const std::string at = where(package, e.value.line);
            if (e.key == "name" || e.key == "description") {
                if (text == nullptr) {
                    warnings.push_back(at + e.key + " must be a string");
                } else {
                    (e.key == "name" ? read.name : read.description) = *text;
                }
            } else if (e.key == "version") {
                if (text == nullptr || !valid_version(*text)) {
                    warnings.push_back(at +
                                       "version must be a string of one to four numbers with dots, such as \"1.2\"");
                } else {
                    read.version = *text;
                }
            } else if (auto id = key_after(e.key, "depends.")) {
                if (text == nullptr || (!text->empty() && !valid_version(*text))) {
                    warnings.push_back(at + e.key + " must be the lowest version needed, such as \"1.0\", or \"\"");
                } else if (*id == package.id) {
                    warnings.push_back(at + "a package does not depend on itself");
                } else {
                    read.depends.push_back(PackageDependency{std::move(*id), *text});
                }
            } else if (auto after = key_after(e.key, "after.")) {
                if (e.value.boolean() == nullptr) {
                    warnings.push_back(at + e.key + " must be true or false");
                } else if (*e.value.boolean() && *after != package.id) {
                    read.after.push_back(std::move(*after));
                }
            } else {
                warnings.push_back(at + "unknown field " + e.key + " is passed over");
            }
        }
    }
    package = std::move(read);
    return core::Ok{};
}

std::uint64_t package_hash(const std::filesystem::path& folder) {
    const auto files = files_of(folder);
    std::uint64_t h = hash_number(files.size(), 0);
    for (const auto& [relative, path] : files) {
        h = hash_text(relative, h);
        if (const auto bytes = core::read_file(path)) {
            h = hash_number(bytes->size(), h);
            h = core::xxh64(*bytes, h);
        } else {
            h = hash_number(kUnreadable, h);
        }
    }
    return h;
}

std::vector<ModPackage> load_mod_packages(const std::filesystem::path& mods, std::vector<std::string>& warnings,
                                          std::span<const std::string> disabled) {
    std::vector<ModPackage> packages;
    for (ModPackage& package : mod_packages(mods)) {
        if (std::find(disabled.begin(), disabled.end(), package.id) != disabled.end()) {
            warnings.push_back(package.id + ": turned off; the package is not loaded");
            continue;
        }
        if (package.id.size() > kMaxPackageIdLength) {
            warnings.push_back(package.id + ": a package's name has at most " + std::to_string(kMaxPackageIdLength) +
                               " bytes; the package is not loaded");
            continue;
        }
        const std::filesystem::path manifest = package.folder / kManifestFile;
        std::error_code ec;
        if (std::filesystem::exists(manifest, ec)) {
            const auto bytes = core::read_file(manifest);
            core::Status read = bytes ? core::Status(core::Ok{}) : core::Status(bytes.error());
            if (bytes) {
                read = read_manifest(
                    package, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), warnings);
            }
            if (!read) {
                warnings.push_back(read.error().message + "; the package is not loaded");
                continue;
            }
        }
        package.hash = package_hash(package.folder);
        packages.push_back(std::move(package));
    }
    return order_packages(std::move(packages), warnings);
}

std::vector<ModPackage> order_packages(std::vector<ModPackage> packages, std::vector<std::string>& warnings) {
    std::sort(packages.begin(), packages.end(), [](const ModPackage& a, const ModPackage& b) { return a.id < b.id; });
    const auto find = [&](std::string_view id) {
        return std::find_if(packages.begin(), packages.end(), [&](const ModPackage& p) { return p.id == id; });
    };
    // Out go the packages whose dependencies are not there, until none is left out any more: one
    // that goes may take others with it.
    for (bool changed = true; changed;) {
        changed = false;
        for (auto p = packages.begin(); p != packages.end(); ++p) {
            for (const PackageDependency& d : p->depends) {
                const auto other = find(d.id);
                std::string problem;
                if (other == packages.end()) {
                    problem = "needs the package " + d.id + (d.min_version.empty() ? "" : " " + d.min_version) +
                              ", which is not there";
                } else if (!d.min_version.empty() &&
                           (other->version.empty() || compare_versions(other->version, d.min_version) < 0)) {
                    problem = "needs " + d.id + " " + d.min_version + " or later, but " +
                              (other->version.empty() ? "it has no version" : "it is " + other->version);
                }
                if (!problem.empty()) {
                    warnings.push_back(p->id + ": " + problem + "; the package is not loaded");
                    packages.erase(p);
                    changed = true;
                    break;
                }
            }
            if (changed) {
                break;
            }
        }
    }
    // Every package after those it depends on and wants to come after (Kahn's algorithm), the
    // first by the bytes of its id whenever several could come next.
    const std::size_t n = packages.size();
    std::vector<std::vector<std::size_t>> later(n);
    std::vector<std::size_t> waiting(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        std::set<std::size_t> before;
        for (const PackageDependency& d : packages[i].depends) {
            before.insert(static_cast<std::size_t>(find(d.id) - packages.begin()));
        }
        for (const std::string& a : packages[i].after) {
            if (const auto other = find(a); other != packages.end()) {
                before.insert(static_cast<std::size_t>(other - packages.begin()));
            }
        }
        for (const std::size_t b : before) {
            later[b].push_back(i);
            ++waiting[i];
        }
    }
    std::set<std::size_t> ready;  // indexes, so in the order of the ids
    for (std::size_t i = 0; i < n; ++i) {
        if (waiting[i] == 0) {
            ready.insert(i);
        }
    }
    std::vector<std::size_t> order;
    while (!ready.empty()) {
        const std::size_t i = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(i);
        for (const std::size_t j : later[i]) {
            if (--waiting[j] == 0) {
                ready.insert(j);
            }
        }
    }
    std::vector<bool> placed(n, false);
    for (const std::size_t i : order) {
        placed[i] = true;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!placed[i]) {
            warnings.push_back(packages[i].id +
                               ": its dependencies or load order go round in a circle; the package is not loaded");
        }
    }
    std::vector<ModPackage> out;
    out.reserve(order.size());
    for (const std::size_t i : order) {
        out.push_back(std::move(packages[i]));
    }
    return out;
}

std::vector<ContentPackage> content_of(std::span<const ModPackage> packages) {
    std::vector<ContentPackage> out;
    out.reserve(packages.size());
    for (const ModPackage& p : packages) {
        out.push_back(p.content());
    }
    return out;
}

std::vector<ModPackage> packages_for(std::span<const ModPackage> installed, std::span<const ContentPackage> wanted,
                                     std::vector<std::string>& warnings) {
    std::vector<ModPackage> out;
    for (const ContentPackage& w : wanted) {
        auto p = std::find_if(installed.begin(), installed.end(),
                              [&](const ModPackage& m) { return m.id == w.id && m.hash == w.hash; });
        if (p == installed.end()) {
            p = std::find_if(installed.begin(), installed.end(), [&](const ModPackage& m) { return m.id == w.id; });
        }
        if (p == installed.end()) {
            warnings.push_back("the game uses the package " + package_label(w) + ", which is not installed");
            continue;
        }
        if (p->hash != w.hash) {
            warnings.push_back("the game uses the package " + package_label(w) + "; the installed one (" +
                               package_label(p->content()) + ") has other files and is used instead");
        }
        out.push_back(*p);
    }
    return out;
}

std::vector<ContentPackage> missing_packages(std::span<const ContentPackage> needed,
                                             std::span<const ContentPackage> present) {
    std::vector<ContentPackage> out;
    for (const ContentPackage& n : needed) {
        if (std::none_of(present.begin(), present.end(),
                         [&](const ContentPackage& p) { return p.id == n.id && p.hash == n.hash; })) {
            out.push_back(n);
        }
    }
    return out;
}

std::string package_label(const ContentPackage& package) {
    return package.version.empty() ? package.id : package.id + " " + package.version;
}

bool safe_package_path(std::string_view path) noexcept {
    if (path.empty() || path.size() > kMaxPackagePathLength) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = end + 1;
    }
    return std::none_of(path.begin(), path.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7F || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
               c == '>' || c == '|';
    });
}

core::Result<std::vector<PackageFile>> read_package_files(const std::filesystem::path& folder) {
    std::vector<PackageFile> out;
    for (const auto& [relative, path] : files_of(folder)) {
        auto bytes = core::read_file(path);
        if (!bytes) {
            return bytes.error();
        }
        out.push_back(PackageFile{relative, std::move(bytes).value()});
    }
    return out;
}

std::uint64_t package_size(const std::filesystem::path& folder) {
    std::uint64_t total = 0;
    for (const auto& file : files_of(folder)) {
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(file.second, ec);
        total += ec ? 0 : static_cast<std::uint64_t>(size);
    }
    return total;
}

std::filesystem::path cached_package_folder(const std::filesystem::path& cache, const ContentPackage& package) {
    const std::string name = package.id + "-" + hex16(package.hash);
    return cache / core::path_from_utf8(name);
}

core::Result<ModPackage> store_package(const std::filesystem::path& cache, const ContentPackage& package,
                                       std::span<const PackageFile> files) {
    if (!valid_package_id(package.id)) {
        return core::make_error(core::ErrorCode::InvalidFormat, "a package id that cannot name a folder");
    }
    for (const PackageFile& f : files) {
        if (!safe_package_path(f.path)) {
            return core::make_error(core::ErrorCode::InvalidFormat, package.id + ": a file outside the package");
        }
    }
    const std::filesystem::path folder = cached_package_folder(cache, package);
    std::filesystem::path incoming = folder;
    incoming += ".incoming";
    std::error_code ec;
    std::filesystem::remove_all(incoming, ec);
    const auto fail = [&](std::string message) {
        std::error_code ignored;
        std::filesystem::remove_all(incoming, ignored);
        return core::make_error(core::ErrorCode::InvalidFormat, package.id + ": " + std::move(message));
    };
    for (const PackageFile& f : files) {
        const std::filesystem::path path = incoming / core::path_from_utf8(f.path);
        std::filesystem::create_directories(path.parent_path(), ec);
        if (const auto written = core::write_file(path, f.bytes); !written) {
            return fail(written.error().message);
        }
    }
    std::filesystem::create_directories(incoming, ec);  // a package without files
    if (package_hash(incoming) != package.hash) {
        return fail("the files received do not have the package's hash");
    }
    std::filesystem::remove_all(folder, ec);
    std::filesystem::rename(incoming, folder, ec);
    if (ec) {
        return fail("cannot be kept: " + ec.message());
    }
    ModPackage out;
    out.id = package.id;
    out.folder = folder;
    out.name = package.id;
    if (const auto bytes = core::read_file(folder / kManifestFile)) {
        std::vector<std::string> ignored;
        (void)read_manifest(out, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
                            ignored);
    }
    out.hash = package.hash;
    return out;
}

std::vector<ModPackage> cached_packages(const std::filesystem::path& cache, std::vector<std::string>& warnings) {
    std::vector<ModPackage> out;
    for (ModPackage& folder : mod_packages(cache)) {
        const std::size_t dash = folder.id.rfind('-');
        if (dash == std::string::npos || folder.id.size() - dash != 17 || folder.id.ends_with(".incoming")) {
            continue;  // not a received package, or one that never arrived whole
        }
        ModPackage package;
        package.id = folder.id.substr(0, dash);
        package.folder = folder.folder;
        package.name = package.id;
        package.hash = package_hash(folder.folder);
        if (hex16(package.hash) != folder.id.substr(dash + 1)) {
            warnings.push_back(core::path_to_utf8(folder.folder) + ": its files changed since it was received");
            continue;
        }
        if (const auto bytes = core::read_file(folder.folder / kManifestFile)) {
            const auto read = read_manifest(
                package, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), warnings);
            if (!read) {
                warnings.push_back(read.error().message);
            }
        }
        out.push_back(std::move(package));
    }
    return out;
}

core::Result<std::filesystem::path> adopt_package(const std::filesystem::path& mods, const ModPackage& cached) {
    if (!valid_package_id(cached.id)) {
        return core::make_error(core::ErrorCode::InvalidFormat, "a package id that cannot name a folder");
    }
    const std::filesystem::path target = mods / core::path_from_utf8(cached.id);
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        return core::make_error(core::ErrorCode::Unsupported, cached.id + ": the mods folder has this package already");
    }
    std::filesystem::path incoming = target;
    incoming += ".incoming";
    std::filesystem::remove_all(incoming, ec);
    std::filesystem::create_directories(incoming.parent_path(), ec);
    std::filesystem::copy(cached.folder, incoming, std::filesystem::copy_options::recursive, ec);
    if (!ec) {
        std::filesystem::rename(incoming, target, ec);
    }
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove_all(incoming, ignored);
        return core::make_error(core::ErrorCode::IoError, cached.id + ": cannot be copied: " + ec.message());
    }
    return target;
}

}  // namespace o1602::assets
