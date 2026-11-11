// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/game_content.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <new>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include "o1602/core/data_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/hash.hpp"
#include "o1602/core/zip_archive.hpp"

namespace o1602::assets {

namespace {

using fs_iterator = std::filesystem::directory_iterator;

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

/// Whether `path` names a package that is a zip archive.
bool is_archive(const std::filesystem::path& path) {
    std::string extension = core::path_to_utf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return extension == kPackageArchiveExtension && !path.stem().empty();
}

/// What package_hash gives for these files, ordered by their paths.
std::uint64_t files_hash(std::span<const PackageFile> files) {
    std::uint64_t h = hash_number(files.size(), 0);
    for (const PackageFile& f : files) {
        h = hash_text(f.path, h);
        h = hash_number(f.bytes.size(), h);
        h = core::xxh64(f.bytes, h);
    }
    return h;
}

/// Whether `name` is that of an unpacked archive or a received package: <id>-<16 hex digits>.
bool is_hashed_name(std::string_view name) noexcept {
    const std::size_t dash = name.rfind('-');
    return dash != std::string_view::npos && dash > 0 && name.size() - dash == 17 &&
           std::all_of(name.begin() + static_cast<std::ptrdiff_t>(dash) + 1, name.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
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
    std::vector<ModPackage> archives;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(mods, ec)) {
        ModPackage package;
        if (entry.is_directory(ec)) {
            package.id = core::path_to_utf8(entry.path().filename());
            package.folder = entry.path();
        } else if (entry.is_regular_file(ec) && is_archive(entry.path())) {
            package.id = core::path_to_utf8(entry.path().stem());
            package.archive = entry.path();
        } else {
            continue;
        }
        package.name = package.id;
        (package.archive.empty() ? out : archives).push_back(std::move(package));
    }
    for (ModPackage& archive : archives) {
        if (std::none_of(out.begin(), out.end(), [&](const ModPackage& p) { return p.id == archive.id; })) {
            out.push_back(std::move(archive));
        }
    }
    std::sort(out.begin(), out.end(), [](const ModPackage& a, const ModPackage& b) { return a.id < b.id; });
    return out;
}

std::filesystem::path unpacked_packages_folder(const std::filesystem::path& mods) {
    return mods.parent_path() / "mods-unpacked";
}

core::Status unpack_package(ModPackage& package, const std::filesystem::path& unpacked) {
    const std::string name = package.id + std::string(kPackageArchiveExtension);
    const auto fail = [&](const std::string& why) {
        return core::make_error(core::ErrorCode::InvalidFormat, name + ": " + why);
    };
    if (!valid_package_id(package.id)) {
        return fail("a name that cannot be a package's");
    }
    auto bytes = core::read_file(package.archive);
    if (!bytes) {
        return fail(bytes.error().message);
    }
    auto entries = core::read_zip(*bytes);
    if (!entries) {
        return fail(entries.error().message);
    }
    bytes = core::Bytes{};  // the archive's bytes are not needed any more
    std::erase_if(*entries, [](const core::ZipEntry& e) { return e.path.starts_with("__MACOSX/"); });
    // Packed with its folder ("lighthouse/mod.toml"): the folder's files are the package's.
    if (!entries->empty()) {
        const std::size_t slash = entries->front().path.find('/');
        const std::string top = slash == std::string::npos ? std::string() : entries->front().path.substr(0, slash + 1);
        if (!top.empty() && std::all_of(entries->begin(), entries->end(),
                                        [&](const core::ZipEntry& e) { return e.path.starts_with(top); })) {
            for (core::ZipEntry& e : *entries) {
                e.path.erase(0, top.size());
            }
        }
    }
    std::vector<PackageFile> files;
    files.reserve(entries->size());
    for (core::ZipEntry& e : *entries) {
        if (!safe_package_path(e.path)) {
            return fail("\"" + e.path + "\" is no path inside the package");
        }
        files.push_back(PackageFile{std::move(e.path), std::move(e.bytes)});
    }
    std::sort(files.begin(), files.end(), [](const PackageFile& a, const PackageFile& b) { return a.path < b.path; });
    const auto twice = std::adjacent_find(files.begin(), files.end(),
                                          [](const PackageFile& a, const PackageFile& b) { return a.path == b.path; });
    if (twice != files.end()) {
        return fail(twice->path + " is in the archive twice");
    }
    const ContentPackage content{package.id, {}, files_hash(files)};
    const std::filesystem::path folder = cached_package_folder(unpacked, content);
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec) || package_hash(folder) != content.hash) {
        auto stored = store_package(unpacked, content, files);
        if (!stored) {
            return fail(stored.error().message);
        }
    }
    package.folder = folder;
    return core::Ok{};
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

namespace {

/// One comparison of a version requirement ("<2", ">=1.2", "1.2"), without the blanks around it.
struct VersionBound {
    std::string_view op;  ///< ">=", ">", "<=", "<" or "="
    std::string_view version;
};

std::string_view trimmed(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

/// The comparisons of `requirement`, separated by commas; nothing if one is not valid.
std::optional<std::vector<VersionBound>> version_bounds(std::string_view requirement) {
    std::vector<VersionBound> bounds;
    for (;;) {
        const std::size_t comma = requirement.find(',');
        std::string_view item = trimmed(requirement.substr(0, comma));
        VersionBound bound{">=", {}};
        for (const std::string_view op : {">=", "<=", ">", "<", "="}) {
            if (item.starts_with(op)) {
                bound.op = op;
                item = trimmed(item.substr(op.size()));
                break;
            }
        }
        if (!valid_version(item)) {
            return std::nullopt;
        }
        bound.version = item;
        bounds.push_back(bound);
        if (comma == std::string_view::npos) {
            return bounds;
        }
        requirement.remove_prefix(comma + 1);
    }
}

}  // namespace

bool valid_version_requirement(std::string_view requirement) noexcept {
    if (requirement.empty()) {
        return true;
    }
    try {
        return version_bounds(requirement).has_value();
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool version_meets(std::string_view version, std::string_view requirement) noexcept {
    if (requirement.empty()) {
        return true;
    }
    if (version.empty() || !valid_version(version)) {
        return false;
    }
    std::optional<std::vector<VersionBound>> bounds;
    try {
        bounds = version_bounds(requirement);
    } catch (const std::bad_alloc&) {
        return false;
    }
    if (!bounds) {
        return false;
    }
    return std::all_of(bounds->begin(), bounds->end(), [&](const VersionBound& b) {
        const int c = compare_versions(version, b.version);
        return b.op == ">=" ? c >= 0 : b.op == ">" ? c > 0 : b.op == "<=" ? c <= 0 : b.op == "<" ? c < 0 : c == 0;
    });
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
                if (text == nullptr || !valid_version_requirement(*text)) {
                    warnings.push_back(at + e.key +
                                       " must be the versions needed, such as \"1.0\" (or later), \">=1.2, <2\" or "
                                       "\"\" (any)");
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

std::vector<ModPackage> read_mod_packages(const std::filesystem::path& mods, std::vector<std::string>& warnings,
                                          std::vector<PackageProblem>* problems) {
    std::vector<ModPackage> packages;
    const auto unreadable = [&](const ModPackage& package, std::string why) {
        warnings.push_back(std::move(why) + "; the package is not loaded");
        if (problems != nullptr) {
            problems->push_back(PackageProblem{package.id, PackageProblem::Kind::Unreadable, {}, {}, {}});
        }
    };
    const std::filesystem::path unpacked = unpacked_packages_folder(mods);
    std::set<std::filesystem::path> unpacked_in_use;
    for (ModPackage& package : mod_packages(mods)) {
        if (package.id.size() > kMaxPackageIdLength) {
            unreadable(package,
                       package.id + ": a package's name has at most " + std::to_string(kMaxPackageIdLength) + " bytes");
            continue;
        }
        if (!package.archive.empty()) {
            if (const auto status = unpack_package(package, unpacked); !status) {
                unreadable(package, status.error().message);
                continue;
            }
            unpacked_in_use.insert(package.folder.filename());
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
                unreadable(package, read.error().message);
                continue;
            }
        }
        packages.push_back(std::move(package));
    }
    // What archives that changed or are gone left behind.
    std::error_code ec;
    std::vector<std::filesystem::path> unused;
    for (fs_iterator it(unpacked, ec), last; !ec && it != last; it.increment(ec)) {
        const std::filesystem::path name = it->path().filename();
        std::string text = core::path_to_utf8(name);
        if (text.ends_with(".incoming")) {
            text.resize(text.size() - 9);
        }
        if (is_hashed_name(text) && unpacked_in_use.count(name) == 0) {
            unused.push_back(it->path());
        }
    }
    for (const std::filesystem::path& path : unused) {
        std::filesystem::remove_all(path, ec);
    }
    return packages;
}

std::vector<ModPackage> load_mod_packages(const std::filesystem::path& mods, std::vector<std::string>& warnings,
                                          std::span<const std::string> disabled,
                                          std::vector<PackageProblem>* problems) {
    std::vector<PackageProblem> found;
    std::vector<ModPackage> packages =
        order_packages(read_mod_packages(mods, warnings, &found), warnings, disabled, &found);
    for (ModPackage& package : packages) {
        package.hash = package_hash(package.folder);
    }
    if (problems != nullptr) {
        problems->insert(problems->end(), found.begin(), found.end());
    }
    return packages;
}

namespace {

std::string problem_text(const PackageProblem& p) {
    using Kind = PackageProblem::Kind;
    const std::string needs = p.dependency + (p.versions.empty() ? "" : " " + p.versions);
    switch (p.kind) {
        case Kind::TurnedOff:
            return "turned off";
        case Kind::Unreadable:
            return "its manifest cannot be read";
        case Kind::Missing:
            return "needs the package " + needs + ", which is not there";
        case Kind::WrongVersion:
            return "needs " + needs + ", but " + (p.found.empty() ? "it has no version" : "it is " + p.found);
        case Kind::DependencyOff:
            return "needs " + p.dependency + ", which is turned off";
        case Kind::DependencyBroken:
            return "needs " + p.dependency + ", which is not loaded itself";
        case Kind::Circle:
            return "its dependencies or load order go round in a circle";
    }
    return {};
}

}  // namespace

std::vector<ModPackage> order_packages(std::vector<ModPackage> packages, std::vector<std::string>& warnings,
                                       std::span<const std::string> disabled, std::vector<PackageProblem>* problems) {
    std::vector<PackageProblem> found = problems != nullptr ? *problems : std::vector<PackageProblem>{};
    const std::size_t known = found.size();
    const auto leave_out = [&](PackageProblem problem) {
        warnings.push_back(problem.package + ": " + problem_text(problem) + "; the package is not loaded");
        found.push_back(std::move(problem));
    };
    std::sort(packages.begin(), packages.end(), [](const ModPackage& a, const ModPackage& b) { return a.id < b.id; });
    // Every installed package, for what a dependency is when it is left out.
    const std::vector<ModPackage> installed = packages;
    const auto installed_as = [&](std::string_view id) -> const ModPackage* {
        const auto it =
            std::find_if(installed.begin(), installed.end(), [&](const ModPackage& p) { return p.id == id; });
        return it != installed.end() ? &*it : nullptr;
    };
    const auto problem_of = [&](std::string_view id) -> const PackageProblem* {
        const auto it =
            std::find_if(found.begin(), found.end(), [&](const PackageProblem& p) { return p.package == id; });
        return it != found.end() ? &*it : nullptr;
    };
    std::erase_if(packages, [&](const ModPackage& p) {
        if (std::find(disabled.begin(), disabled.end(), p.id) == disabled.end()) {
            return false;
        }
        leave_out(PackageProblem{p.id, PackageProblem::Kind::TurnedOff, {}, {}, {}});
        return true;
    });
    const auto find = [&](std::string_view id) {
        return std::find_if(packages.begin(), packages.end(), [&](const ModPackage& p) { return p.id == id; });
    };
    // What keeps a package from loading: the first of its dependencies that is not there as it
    // needs it.
    const auto unmet = [&](const ModPackage& p) -> std::optional<PackageProblem> {
        using Kind = PackageProblem::Kind;
        for (const PackageDependency& d : p.depends) {
            PackageProblem problem{p.id, Kind::Missing, d.id, d.versions, {}};
            const ModPackage* other = installed_as(d.id);
            const PackageProblem* gone = problem_of(d.id);
            if (other == nullptr) {
                problem.kind = gone != nullptr ? Kind::DependencyBroken : Kind::Missing;  // unreadable
            } else if (!version_meets(other->version, d.versions)) {
                problem.kind = Kind::WrongVersion;
                problem.found = other->version;
            } else if (find(d.id) != packages.end()) {
                continue;
            } else {
                problem.kind =
                    gone != nullptr && gone->kind == Kind::TurnedOff ? Kind::DependencyOff : Kind::DependencyBroken;
            }
            if (problem.kind != Kind::Missing && problem.kind != Kind::WrongVersion) {
                problem.versions.clear();
            }
            return problem;
        }
        return std::nullopt;
    };
    // Out go the packages whose dependencies are not there as they need them, until none is left
    // out any more: one that goes may take others with it.
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t i = 0; i < packages.size(); ++i) {
            if (std::optional<PackageProblem> problem = unmet(packages[i])) {
                leave_out(std::move(*problem));
                packages.erase(packages.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
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
            leave_out(PackageProblem{packages[i].id, PackageProblem::Kind::Circle, {}, {}, {}});
        }
    }
    if (problems != nullptr) {
        problems->insert(problems->end(), found.begin() + static_cast<std::ptrdiff_t>(known), found.end());
    }
    std::vector<ModPackage> out;
    out.reserve(order.size());
    for (const std::size_t i : order) {
        out.push_back(std::move(packages[i]));
    }
    return out;
}

std::vector<std::string> dependencies_to_turn_on(std::span<const ModPackage> installed, std::string_view id,
                                                 std::span<const std::string> disabled) {
    std::set<std::string, std::less<>> needed;
    std::vector<std::string_view> open{id};
    while (!open.empty()) {
        const std::string_view current = open.back();
        open.pop_back();
        const auto package =
            std::find_if(installed.begin(), installed.end(), [&](const ModPackage& p) { return p.id == current; });
        if (package == installed.end()) {
            continue;
        }
        for (const PackageDependency& d : package->depends) {
            const auto other =
                std::find_if(installed.begin(), installed.end(), [&](const ModPackage& p) { return p.id == d.id; });
            if (other != installed.end() && d.id != id && version_meets(other->version, d.versions) &&
                needed.insert(d.id).second) {
                open.emplace_back(other->id);
            }
        }
    }
    std::vector<std::string> out;
    for (const ModPackage& p : installed) {
        if (needed.contains(p.id) && std::find(disabled.begin(), disabled.end(), p.id) != disabled.end()) {
            out.push_back(p.id);
        }
    }
    return out;
}

std::vector<std::string> packages_needing(std::span<const ModPackage> installed, std::string_view id) {
    std::set<std::string, std::less<>> needing;
    for (bool changed = true; changed;) {
        changed = false;
        for (const ModPackage& p : installed) {
            if (p.id == id || needing.contains(p.id)) {
                continue;
            }
            if (std::any_of(p.depends.begin(), p.depends.end(),
                            [&](const PackageDependency& d) { return d.id == id || needing.contains(d.id); })) {
                needing.insert(p.id);
                changed = true;
            }
        }
    }
    std::vector<std::string> out;
    for (const ModPackage& p : installed) {
        if (needing.contains(p.id)) {
            out.push_back(p.id);
        }
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
    if (std::filesystem::exists(target, ec) ||
        std::filesystem::exists(mods / core::path_from_utf8(cached.id + std::string(kPackageArchiveExtension)), ec)) {
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
