// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/rda_archive.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <system_error>

#include "o1602/core/file.hpp"

namespace o1602::assets {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::string_view kMagic = "Resource File V2.2";
constexpr std::uint64_t kFirstBlockAt = 0x310;  ///< u64: where the first block header is
constexpr std::uint64_t kBlockHeaderSize = 32;
constexpr std::uint64_t kEntrySize = 560;  ///< 260 UTF-16 characters, five u64
constexpr std::size_t kNameChars = 260;
/// Block flags: compressed (zlib), encrypted, memory resident, deleted. Only blocks without any of
/// them hold files Open1602 reads; the History Edition's have none.
constexpr std::uint32_t kFlagsUnreadable = 1U | 2U | 4U;
constexpr std::uint32_t kFlagDeleted = 8U;
/// A directory larger than this is refused (the History Edition's largest has some 2 000 entries).
constexpr std::uint64_t kMaxDirectory = std::uint64_t{64} << 20;
constexpr std::size_t kMaxBlocks = 4096;

std::uint32_t u32_at(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
}

std::uint64_t u64_at(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(u32_at(p)) | (static_cast<std::uint64_t>(u32_at(p + 4)) << 32U);
}

/// UTF-16LE up to its first 0 (at most kNameChars) as UTF-8.
std::string name_at(const std::uint8_t* p) {
    std::string out;
    for (std::size_t i = 0; i < kNameChars; ++i) {
        std::uint32_t c = static_cast<std::uint32_t>(p[i * 2]) | (static_cast<std::uint32_t>(p[i * 2 + 1]) << 8U);
        if (c == 0) {
            break;
        }
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < kNameChars) {
            const std::uint32_t low =
                static_cast<std::uint32_t>(p[i * 2 + 2]) | (static_cast<std::uint32_t>(p[i * 2 + 3]) << 8U);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10U) + (low - 0xDC00);
                ++i;
            }
        }
        if (c < 0x80) {
            out.push_back(static_cast<char>(c == '\\' ? '/' : c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6U)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3FU)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12U)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18U)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3FU)));
        }
    }
    return out;
}

/// Reads `count` bytes at `offset` of the archive, from the file or from memory.
using ReadAt = std::function<bool(std::uint64_t offset, std::size_t count, std::uint8_t* out)>;

core::Result<std::vector<RdaArchive::Entry>> read_directory(std::uint64_t size, const ReadAt& read_at) {
    std::array<std::uint8_t, 8> word{};
    std::array<std::uint8_t, kMagic.size()> magic{};
    if (size < kFirstBlockAt + 8 || !read_at(0, magic.size(), magic.data()) ||
        std::memcmp(magic.data(), kMagic.data(), kMagic.size()) != 0) {
        return make_error(ErrorCode::InvalidFormat, "not an RDA archive of version 2.2");
    }
    if (!read_at(kFirstBlockAt, 8, word.data())) {
        return make_error(ErrorCode::InvalidFormat, "RDA header cut off");
    }
    std::vector<RdaArchive::Entry> entries;
    std::uint64_t block = u64_at(word.data());
    if (block >= size) {
        return make_error(ErrorCode::InvalidFormat, "RDA archive has no block");
    }
    for (std::size_t n = 0; block < size; ++n) {
        if (n == kMaxBlocks || block > size - kBlockHeaderSize) {
            return make_error(ErrorCode::InvalidFormat, "RDA block chain is broken");
        }
        std::array<std::uint8_t, kBlockHeaderSize> header{};
        if (!read_at(block, header.size(), header.data())) {
            return make_error(ErrorCode::InvalidFormat, "RDA block header cut off");
        }
        const std::uint32_t flags = u32_at(header.data());
        const std::uint32_t files = u32_at(header.data() + 4);
        const std::uint64_t stored = u64_at(header.data() + 8);
        const std::uint64_t next = u64_at(header.data() + 24);
        if (next <= block) {
            return make_error(ErrorCode::InvalidFormat, "RDA block chain runs backwards");
        }
        // The directory lies right before its block's header; one stored as it is has an entry
        // per file. Others (compressed, encrypted) are left out.
        if ((flags & (kFlagsUnreadable | kFlagDeleted)) == 0 && files > 0) {
            if (stored != std::uint64_t{files} * kEntrySize || stored > kMaxDirectory || stored > block) {
                return make_error(ErrorCode::InvalidFormat, "RDA directory has an invalid size");
            }
            std::vector<std::uint8_t> directory(static_cast<std::size_t>(stored));
            if (!read_at(block - stored, directory.size(), directory.data())) {
                return make_error(ErrorCode::InvalidFormat, "RDA directory cut off");
            }
            for (std::uint32_t i = 0; i < files; ++i) {
                const std::uint8_t* e = directory.data() + std::size_t{i} * kEntrySize;
                RdaArchive::Entry entry;
                entry.name = name_at(e);
                entry.offset = u64_at(e + kNameChars * 2);
                entry.stored = u64_at(e + kNameChars * 2 + 8);
                entry.size = u64_at(e + kNameChars * 2 + 16);
                entry.readable =
                    entry.stored == entry.size && entry.offset <= size && entry.stored <= size - entry.offset;
                entries.push_back(std::move(entry));
            }
        }
        block = next;
    }
    return entries;
}

/// The same name, ignoring the case of ASCII letters and with '\\' as '/'.
bool same_name(std::string_view a, std::string_view b) {
    const auto plain = [](char c) {
        return c == '\\' ? '/' : c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [&](char x, char y) { return plain(x) == plain(y); });
}

}  // namespace

core::Result<RdaArchive> RdaArchive::open(const std::filesystem::path& path) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return make_error(ErrorCode::NotFound, core::path_to_utf8(path) + ": " + ec.message());
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return make_error(ErrorCode::NotFound, core::path_to_utf8(path) + " cannot be read");
    }
    const ReadAt read_at = [&](std::uint64_t offset, std::size_t count, std::uint8_t* out) {
        if (offset > size || count > size - offset) {
            return false;
        }
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(count));
        return static_cast<bool>(in);
    };
    auto entries = read_directory(size, read_at);
    if (!entries) {
        return make_error(entries.error().code, core::path_to_utf8(path) + ": " + entries.error().message);
    }
    RdaArchive archive;
    archive.m_path = path;
    archive.m_entries = std::move(entries).value();
    return archive;
}

core::Result<RdaArchive> RdaArchive::from_bytes(std::vector<std::uint8_t> bytes) {
    const std::uint64_t size = bytes.size();
    const ReadAt read_at = [&](std::uint64_t offset, std::size_t count, std::uint8_t* out) {
        if (offset > size || count > size - offset) {
            return false;
        }
        std::memcpy(out, bytes.data() + offset, count);
        return true;
    };
    auto entries = read_directory(size, read_at);
    if (!entries) {
        return entries.error();
    }
    RdaArchive archive;
    archive.m_bytes = std::move(bytes);
    archive.m_entries = std::move(entries).value();
    return archive;
}

const RdaArchive::Entry* RdaArchive::find(std::string_view name) const {
    const auto it =
        std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return same_name(e.name, name); });
    return it == m_entries.end() ? nullptr : &*it;
}

core::Result<std::vector<std::uint8_t>> RdaArchive::read(std::string_view name) const {
    const Entry* entry = find(name);
    if (entry == nullptr) {
        return make_error(ErrorCode::NotFound, std::string(name) + " is not in the archive");
    }
    if (!entry->readable) {
        return make_error(ErrorCode::InvalidFormat, std::string(name) + " is compressed or encrypted");
    }
    if (entry->size > core::kMaxReadSize) {
        return make_error(ErrorCode::InvalidFormat, std::string(name) + " is too large");
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(entry->size));
    if (m_path.empty()) {
        std::copy_n(m_bytes.begin() + static_cast<std::ptrdiff_t>(entry->offset), out.size(), out.begin());
        return out;
    }
    std::ifstream in(m_path, std::ios::binary);
    in.seekg(static_cast<std::streamoff>(entry->offset));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!in) {
        return make_error(ErrorCode::InvalidFormat, std::string(name) + ": the archive is cut off");
    }
    return out;
}

}  // namespace o1602::assets
