// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Lua 5.4 state for the scripts of content packages (ADR 0007 section 6, docs/modding/scripts.md).
// Scripts run on every computer of a game and in every replay, so the sandbox keeps them
// deterministic and closed off: no files, no clock, no addresses, random numbers only from the
// game's generator, and limits on memory and on the instructions of each entry.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::script {

struct Limits {
    std::size_t memory_bytes = std::size_t{32} * 1024 * 1024;  ///< everything the state holds
    std::uint64_t instructions = 10'000'000;                   ///< per run() or call()
};

struct Table;

/// What passes between the game and a script: nil, a boolean, an integer, a number, a string or a
/// table of them. Functions and coroutines stay inside the sandbox.
using Value = std::variant<std::monostate, bool, std::int64_t, double, std::string, std::shared_ptr<const Table>>;

/// A table the game hands to a script: fields by name and a list (indices 1 to n).
struct Table {
    std::vector<std::pair<std::string, Value>> fields;
    std::vector<Value> items;
};

/// A table value with these fields and items.
[[nodiscard]] Value make_table(std::vector<std::pair<std::string, Value>> fields, std::vector<Value> items = {});

class Sandbox {
public:
    /// A function of the game: gets the script's arguments (nil, booleans, numbers, strings and
    /// tables of them, with names or indices from 1 as keys; fields sorted by name) and returns one
    /// value, or an error, which the script sees as a Lua error with its message.
    using HostFunction = std::function<core::Result<Value>(std::span<const Value>)>;
    /// Uniformly distributed 32-bit numbers from the game's generator.
    using RandomSource = std::function<std::uint32_t()>;
    /// One line a script printed.
    using PrintSink = std::function<void(std::string_view)>;

    /// Throws std::bad_alloc when not even an empty state fits into the memory limit.
    explicit Sandbox(Limits limits = {});
    ~Sandbox();
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;
    Sandbox(Sandbox&&) noexcept;
    Sandbox& operator=(Sandbox&&) noexcept;

    /// math.random draws from `source`; without one it raises an error in the script.
    void set_random(RandomSource source);
    /// print() goes to `sink`; without one it is dropped.
    void set_print(PrintSink sink);

    /// Makes `function` the script's `library.name` (the global table `library` is made if needed).
    /// A host function must not call back into this sandbox. Fails only for lack of memory.
    core::Status add_function(std::string_view library, std::string_view name, HostFunction function);
    /// Sets the script's `library.name` to `value`.
    core::Status add_value(std::string_view library, std::string_view name, const Value& value);

    /// Runs a script's text (binary chunks are refused). `name` appears in its error messages.
    core::Status run(std::string_view name, std::string_view source);

    /// Whether the script defined a global function of this name.
    [[nodiscard]] bool has_function(std::string_view name) const;

    /// Calls a global function with `args` and returns its first result. NotFound when there is no
    /// such function, ScriptError when it fails or returns a table or function.
    core::Result<Value> call(std::string_view name, std::span<const Value> args = {});

    /// The global table `name` as bytes, for a savegame: its booleans, numbers, strings and tables
    /// of them, keys in a fixed order. Other values (functions, coroutines, tables nested deeper than
    /// kMaxSavedDepth) are left out. No bytes for a global that is no table.
    core::Result<std::vector<std::uint8_t>> save_table(std::string_view name);
    /// Sets the global `name` to the table save_table() gave; no bytes leave it as it is.
    core::Status load_table(std::string_view name, std::span<const std::uint8_t> bytes);
    static constexpr int kMaxSavedDepth = 32;

    /// Bytes the state holds now.
    [[nodiscard]] std::size_t memory_used() const noexcept;
    /// Instructions the last run() or call() executed, counted in steps of kInstructionStep.
    [[nodiscard]] std::uint64_t instructions_used() const noexcept;

    /// How often the instruction limit is checked.
    static constexpr int kInstructionStep = 1000;

    struct State;

private:
    std::unique_ptr<State> m_state;
};

}  // namespace o1602::script
