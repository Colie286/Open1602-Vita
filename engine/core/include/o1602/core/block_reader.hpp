// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace o1602::core {
/// Immutable file read-ahead. At most 32 blocks of 16 KiB per reader and 2 MiB
/// across all readers. Concurrent requests reuse ready blocks, but never wait
/// for an in-flight read; they use an independent exact read instead. The
/// callback must support independent offsets and report exact reads only.
class BlockReader {
public:
    using Read=std::function<bool(std::uint64_t,std::span<std::uint8_t>)>;
    BlockReader(std::uint64_t size,Read read,bool enabled=true);
    ~BlockReader();
    BlockReader(const BlockReader&)=delete;
    BlockReader& operator=(const BlockReader&)=delete;
    bool read(std::uint64_t offset,std::span<std::uint8_t> bytes);
    static std::size_t allocated_bytes() noexcept;
private:
    struct State;
    std::unique_ptr<State> m;
};
}
