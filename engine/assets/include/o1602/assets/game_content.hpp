// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the game, its tools and a server share to start the same game (LC-17): the fingerprint of
// the data the simulation computes with, and the packages of the mods folder with their manifests,
// in their load order (ADR 0007 section 2, docs/modding/packages.md).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/game_directory.hpp"
#include "o1602/core/result.hpp"

namespace o1602::assets {

/// The data the simulation computes with, as one number the computers of a multiplayer game
/// compare before they play: the bytes of haeuser.cod and figuren.cod. The names of new cities and
/// ships travel with the game (sim::Simulation::offer_names), so installations in other languages
/// play together; the corrections of assets::fixes change nothing the simulation reads and may
/// differ (data_fixes.hpp). The packages a game uses are compared one by one (ContentPackage).
[[nodiscard]] std::uint64_t content_fingerprint(const GameDirectory& game);

/// The file in a package's folder that describes it.
inline constexpr std::string_view kManifestFile = "mod.toml";
/// Upper limits of a package's id and version, in bytes (savegames and the network carry them).
inline constexpr std::size_t kMaxPackageIdLength = 64;
inline constexpr std::size_t kMaxPackageVersionLength = 32;

/// A package a game uses, as the game names it: in its savegame, its lobby and the network's Hello.
struct ContentPackage {
    std::string id;          ///< UTF-8, the folder's name
    std::string version;     ///< as the manifest gives it, empty without one
    std::uint64_t hash = 0;  ///< of every file of the package (package_hash)
    friend bool operator==(const ContentPackage&, const ContentPackage&) = default;
};

/// "Needs that package, in one of these versions" (`depends.<id> = ">=1.2, <2"` in the manifest).
struct PackageDependency {
    std::string id;
    std::string versions;  ///< a version requirement (valid_version_requirement); empty: any version
};

/// A folder of <data folder>/mods, or a zip archive there (<id>.zip): its id is the folder's name,
/// or the archive's without ".zip". The rest comes from its manifest (read_manifest); a package
/// without one has no version, no name of its own and no dependencies.
struct ModPackage {
    std::string id;  ///< UTF-8
    /// Where its files are: the folder, or where its archive is unpacked (unpack_package); empty
    /// for an archive not unpacked yet.
    std::filesystem::path folder;
    std::filesystem::path archive;  ///< the zip archive it comes from; empty for a folder
    std::string name;               ///< for lists and messages; the id if the manifest names none
    std::string version;            ///< empty without a manifest
    std::string description;
    std::vector<PackageDependency> depends;
    std::vector<std::string> after;  ///< loads after these when they are there, without needing them
    std::uint64_t hash = 0;          ///< package_hash(folder), set by load_mod_packages

    [[nodiscard]] ContentPackage content() const { return ContentPackage{id, version, hash}; }
};

/// The extension of a package that is a zip archive.
inline constexpr std::string_view kPackageArchiveExtension = ".zip";

/// The folders and zip archives in `mods`, ordered by the bytes of their UTF-8 ids, the same on
/// every system (std::filesystem::path compares wchar_t on Windows); none if the folder does not
/// exist. An archive with the id of a folder is left out: the folder is the package. Their
/// manifests are not read and their archives not unpacked yet.
[[nodiscard]] std::vector<ModPackage> mod_packages(const std::filesystem::path& mods);

/// Where the archives of `mods` are unpacked: the folder "mods-unpacked" next to it.
[[nodiscard]] std::filesystem::path unpacked_packages_folder(const std::filesystem::path& mods);

/// Unpacks the archive of `package` into <unpacked>/<id>-<hash as 16 hex digits> and makes that its
/// folder; an archive unpacked before is not unpacked again. An archive whose files all lie in one
/// folder is unpacked without it ("lighthouse/mod.toml" is "mod.toml"); "__MACOSX/" is left out.
/// The unpacked package has the hash its files have as a folder (package_hash). Refused, and the
/// folder is not set, for a damaged archive, a path outside the package (safe_package_path) or a
/// path given twice.
core::Status unpack_package(ModPackage& package, const std::filesystem::path& unpacked);

/// Whether `version` is one a manifest may give: one to four numbers of at most nine digits,
/// separated by dots ("1", "1.2", "0.10.3").
[[nodiscard]] bool valid_version(std::string_view version) noexcept;
/// Compares two valid versions number by number, a missing number counting as 0 ("1.2" == "1.2.0",
/// "1.10" > "1.9"): negative, 0 or positive.
[[nodiscard]] int compare_versions(std::string_view a, std::string_view b) noexcept;

/// Whether `requirement` is one a dependency may give: empty (any version), or comparisons with
/// versions separated by commas, all of which must hold: `>=1.2`, `>1.2`, `<=1.2`, `<2`, `=1.2.3`.
/// A version without a comparison is the lowest one needed (`1.2` is `>=1.2`).
[[nodiscard]] bool valid_version_requirement(std::string_view requirement) noexcept;
/// Whether `version` meets a valid `requirement`. Any version, also none, meets the empty one; no
/// version meets any other.
[[nodiscard]] bool version_meets(std::string_view version, std::string_view requirement) noexcept;

/// Reads the manifest text of `package` (the contents of its mod.toml) into it. What is wrong is
/// passed over with a warning that names the file and the line; a file that cannot be read at all
/// is an error and changes nothing.
core::Status read_manifest(ModPackage& package, std::string_view utf8, std::vector<std::string>& warnings);

/// One number for every file of a package's folder and its subfolders: their paths relative to the
/// folder (UTF-8, '/' between folders, ordered by their bytes), sizes and bytes. Equal on every
/// system for equal files; a file that cannot be read counts differently from an empty one.
[[nodiscard]] std::uint64_t package_hash(const std::filesystem::path& folder);

/// Why a package is not loaded.
struct PackageProblem {
    enum class Kind : std::uint8_t {
        TurnedOff,         ///< turned off in the menu of mods
        Unreadable,        ///< its manifest cannot be read, or its id is too long
        Missing,           ///< `dependency` is not installed
        WrongVersion,      ///< `dependency` is installed as `found` (empty: no version), not in `versions`
        DependencyOff,     ///< `dependency` is turned off
        DependencyBroken,  ///< `dependency` cannot be loaded itself
        Circle,            ///< its dependencies and load order go round in a circle
    };
    std::string package;
    Kind kind = Kind::Unreadable;
    std::string dependency;
    std::string versions;  ///< Missing, WrongVersion: what the package needs of `dependency`
    std::string found;     ///< WrongVersion
    friend bool operator==(const PackageProblem&, const PackageProblem&) = default;
};

/// The packages of `mods` with their manifests read, in the order of mod_packages(), their archives
/// unpacked (unpack_package, into unpacked_packages_folder(mods), where what no archive needs any
/// more is removed); those whose manifest cannot be read, whose archive cannot be unpacked, or
/// whose id is too long, are left out with a warning and, if `problems` is given, a
/// PackageProblem::Unreadable there. No hashes yet.
[[nodiscard]] std::vector<ModPackage> read_mod_packages(const std::filesystem::path& mods,
                                                        std::vector<std::string>& warnings,
                                                        std::vector<PackageProblem>* problems = nullptr);

/// The packages of `mods` that can be loaded, in their load order: each with its manifest read and
/// its hash. A package comes after the ones it depends on and the ones it wants to come after;
/// otherwise in the order of the bytes of their ids. A package whose manifest cannot be read, whose
/// dependency is missing, in a version it does not take, turned off or not loaded itself, that is
/// part of a circle, or whose id is in `disabled` (turned off in the menu of mods), is left out, and
/// a warning says why; so does a PackageProblem in `problems`, if given.
[[nodiscard]] std::vector<ModPackage> load_mod_packages(const std::filesystem::path& mods,
                                                        std::vector<std::string>& warnings,
                                                        std::span<const std::string> disabled = {},
                                                        std::vector<PackageProblem>* problems = nullptr);
/// The same for packages whose manifests were read already: the order, without the packages that
/// cannot be loaded. A package `problems` names already (Unreadable) counts as installed but not
/// loadable.
[[nodiscard]] std::vector<ModPackage> order_packages(std::vector<ModPackage> packages,
                                                     std::vector<std::string>& warnings,
                                                     std::span<const std::string> disabled = {},
                                                     std::vector<PackageProblem>* problems = nullptr);

/// What turning `id` on in the menu of mods turns on with it: the packages of `installed` (manifests
/// read) among `disabled` that it needs, also through other packages it needs, as long as their
/// version is one it takes; in the order of `installed`.
[[nodiscard]] std::vector<std::string> dependencies_to_turn_on(std::span<const ModPackage> installed,
                                                               std::string_view id,
                                                               std::span<const std::string> disabled);
/// The packages of `installed` that need `id`, also through other packages: what is not loaded
/// without it; in the order of `installed`.
[[nodiscard]] std::vector<std::string> packages_needing(std::span<const ModPackage> installed, std::string_view id);

/// What a game names (ContentPackage) of the packages it was started with.
[[nodiscard]] std::vector<ContentPackage> content_of(std::span<const ModPackage> packages);

/// The packages of `wanted` (a game's) among `installed`, in the game's order: the one with the
/// same id and hash, else one with the same id. A package that is not installed is left out, one
/// installed only with other files is taken (this computer's), each with a warning. The installed
/// packages the game does not name are not taken.
[[nodiscard]] std::vector<ModPackage> packages_for(std::span<const ModPackage> installed,
                                                   std::span<const ContentPackage> wanted,
                                                   std::vector<std::string>& warnings);

/// The packages of `needed` that `present` has not with the same id and hash: what a computer lacks
/// to join a game.
[[nodiscard]] std::vector<ContentPackage> missing_packages(std::span<const ContentPackage> needed,
                                                           std::span<const ContentPackage> present);

/// "lighthouse 1.2" (or "lighthouse" without a version), for messages.
[[nodiscard]] std::string package_label(const ContentPackage& package);

// --- Packages a multiplayer host sends to a computer that lacks them ---

/// A file of a package: its path relative to the package's folder ('/' between folders) and bytes.
struct PackageFile {
    std::string path;  ///< UTF-8
    std::vector<std::uint8_t> bytes;
    friend bool operator==(const PackageFile&, const PackageFile&) = default;
};
/// Upper limit of a path of a package file, in bytes.
inline constexpr std::size_t kMaxPackagePathLength = 255;

/// Whether `path` may name a file of a package written into a folder: relative, folders separated by
/// '/', no empty part, no "." or "..", no '\', ':' or control characters, at most
/// kMaxPackagePathLength bytes. A path from another computer is written only when it is.
[[nodiscard]] bool safe_package_path(std::string_view path) noexcept;

/// The bytes of every file of a package's folder (as package_hash counts them, in its order).
[[nodiscard]] core::Result<std::vector<PackageFile>> read_package_files(const std::filesystem::path& folder);
/// The bytes of every file of a package's folder together.
[[nodiscard]] std::uint64_t package_size(const std::filesystem::path& folder);

/// The folder of a received package in the cache: <cache>/<id>-<hash as 16 hex digits>.
[[nodiscard]] std::filesystem::path cached_package_folder(const std::filesystem::path& cache,
                                                          const ContentPackage& package);
/// Writes the files of `package` into the cache (cached_package_folder) and reads it back as a
/// package: refused, and nothing is left behind, when a path is not safe_package_path, the id is
/// no package id, or the files written do not have the package's hash.
[[nodiscard]] core::Result<ModPackage> store_package(const std::filesystem::path& cache, const ContentPackage& package,
                                                     std::span<const PackageFile> files);
/// The packages in the cache, each with its manifest and hash; a folder whose files no longer have
/// the hash its name gives is passed over with a warning. Ordered by id and hash.
[[nodiscard]] std::vector<ModPackage> cached_packages(const std::filesystem::path& cache,
                                                      std::vector<std::string>& warnings);
/// Copies a received package from the cache into `mods`, as <mods>/<id>, where it is a package of
/// this computer's own from then on: refused when `mods` has a package with that id already, as a
/// folder or an archive.
[[nodiscard]] core::Result<std::filesystem::path> adopt_package(const std::filesystem::path& mods,
                                                                const ModPackage& cached);

}  // namespace o1602::assets
