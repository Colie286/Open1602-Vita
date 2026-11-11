// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/core/zip_archive.hpp"

#include <algorithm>

#include "o1602/core/byte_reader.hpp"
#include "o1602/core/compress.hpp"

namespace o1602::core {

namespace {

constexpr std::uint32_t kEndSignature = 0x06054B50;
constexpr std::uint32_t kCentralSignature = 0x02014B50;
constexpr std::uint32_t kLocalSignature = 0x04034B50;
constexpr std::size_t kEndSize = 22;
constexpr std::size_t kCentralSize = 46;
constexpr std::size_t kLocalSize = 30;
constexpr std::size_t kMaxComment = 0xFFFF;
constexpr std::uint16_t kEncrypted = 1U << 0;
constexpr std::uint16_t kStored = 0;
constexpr std::uint16_t kDeflate = 8;

Error damaged(const std::string& why) {
    return Error{ErrorCode::InvalidFormat, "zip archive: " + why};
}

/// Where the end of the central directory starts: the last signature that leaves room for the
/// record and its comment up to the end.
std::optional<std::size_t> find_end(std::span<const std::uint8_t> archive) {
    if (archive.size() < kEndSize) {
        return std::nullopt;
    }
    const std::size_t lowest = archive.size() - std::min(archive.size(), kEndSize + kMaxComment);
    for (std::size_t at = archive.size() - kEndSize + 1; at-- > lowest;) {
        if (ByteReader::load_u32(archive, at) == kEndSignature) {
            const std::size_t comment = archive[at + 20] | (std::size_t{archive[at + 21]} << 8);
            if (at + kEndSize + comment == archive.size()) {
                return at;
            }
        }
    }
    return std::nullopt;
}

}  // namespace

Result<std::vector<ZipEntry>> read_zip(std::span<const std::uint8_t> archive, const ZipLimits& limits) {
    const auto end = find_end(archive);
    if (!end) {
        return damaged("no central directory (not a zip archive, or cut off)");
    }
    ByteReader in(archive);
    in.seek(*end + 4);
    const auto disk = in.u16();
    const auto directory_disk = in.u16();
    const auto here = in.u16();
    const auto count = in.u16();
    const auto directory_size = in.u32();
    const auto directory_offset = in.u32();
    if (!disk || !directory_disk || !here || !count || !directory_size || !directory_offset) {
        return damaged("cut off");
    }
    if (*disk != 0 || *directory_disk != 0 || *here != *count) {
        return Error{ErrorCode::Unsupported, "zip archive: spread over several files"};
    }
    if (*count == 0xFFFF || *directory_offset == 0xFFFFFFFF || *directory_size == 0xFFFFFFFF) {
        return Error{ErrorCode::Unsupported, "zip archive: zip64 is not supported"};
    }
    if (*count > limits.max_entries) {
        return Error{ErrorCode::LimitExceeded,
                     "zip archive: more than " + std::to_string(limits.max_entries) + " files"};
    }
    if (std::uint64_t{*directory_offset} + *directory_size > *end) {
        return damaged("the central directory is outside the archive");
    }
    std::vector<ZipEntry> out;
    std::uint64_t total = 0;
    ByteReader central(archive.subspan(*directory_offset, *directory_size));
    for (std::size_t i = 0; i < *count; ++i) {
        if (!central.has(kCentralSize) || central.u32() != kCentralSignature) {
            return damaged("a damaged central directory");
        }
        (void)central.skip(4);  // made by, needed
        const auto flags = central.u16();
        const auto method = central.u16();
        (void)central.skip(4);  // time, date
        const auto crc = central.u32();
        const auto packed = central.u32();
        const auto size = central.u32();
        const auto name_length = central.u16();
        const auto extra_length = central.u16();
        const auto comment_length = central.u16();
        (void)central.skip(8);  // disk, internal and external attributes
        const auto local = central.u32();
        const auto name = central.bytes(name_length.value_or(0));
        if (!flags || !method || !crc || !packed || !size || !local || !name ||
            !central.skip(std::size_t{extra_length.value_or(0)} + comment_length.value_or(0))) {
            return damaged("a damaged central directory");
        }
        ZipEntry entry;
        entry.path.assign(name->begin(), name->end());
        if (entry.path.ends_with('/')) {
            continue;  // a folder
        }
        if ((*flags & kEncrypted) != 0) {
            return Error{ErrorCode::Unsupported, "zip archive: " + entry.path + " is encrypted"};
        }
        if (*packed == 0xFFFFFFFF || *size == 0xFFFFFFFF || *local == 0xFFFFFFFF) {
            return Error{ErrorCode::Unsupported, "zip archive: zip64 is not supported"};
        }
        total += *size;
        if (total > limits.max_total) {
            return Error{ErrorCode::LimitExceeded, "zip archive: its files are larger than " +
                                                       std::to_string(limits.max_total >> 20) + " MiB together"};
        }
        // The local header: its name and extra field may differ in length from the central one's.
        ByteReader head(archive);
        if (!head.seek(*local) || !head.has(kLocalSize) || head.u32() != kLocalSignature) {
            return damaged(entry.path + ": a damaged local header");
        }
        (void)head.skip(22);
        const auto local_name = head.u16();
        const auto local_extra = head.u16();
        if (!local_name || !local_extra || !head.skip(std::size_t{*local_name} + *local_extra)) {
            return damaged(entry.path + ": a damaged local header");
        }
        const auto data = head.bytes(*packed);
        if (!data) {
            return damaged(entry.path + ": cut off");
        }
        if (*method == kStored) {
            if (*packed != *size) {
                return damaged(entry.path + ": sizes that do not match");
            }
            entry.bytes.assign(data->begin(), data->end());
        } else if (*method == kDeflate) {
            auto unpacked = raw_inflate(*data, *size);
            if (!unpacked) {
                return Error{unpacked.error().code, "zip archive: " + entry.path + ": " + unpacked.error().message};
            }
            entry.bytes = std::move(unpacked).value();
        } else {
            return Error{ErrorCode::Unsupported, "zip archive: " + entry.path + " is packed with method " +
                                                     std::to_string(*method) + " (stored and deflate are supported)"};
        }
        if (crc32(entry.bytes) != *crc) {
            return damaged(entry.path + ": damaged (checksum)");
        }
        out.push_back(std::move(entry));
    }
    return out;
}

}  // namespace o1602::core
