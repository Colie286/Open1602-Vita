// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/script/sandbox.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <limits>
#include <new>
#include <utility>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace o1602::script {

// Lua reports errors with longjmp, which skips C++ destructors. So the functions below that Lua calls
// hold no C++ objects that own anything while they call into Lua, and the callbacks of the game run
// inside a try block that ends before an error is raised.

struct Sandbox::State {
    Limits limits;
    lua_State* lua = nullptr;
    std::size_t used = 0;        ///< bytes allocated for the state
    std::uint64_t executed = 0;  ///< instructions of the current run() or call()
    RandomSource random;
    PrintSink print;
    std::vector<HostFunction> functions;
    // Scratch of host function calls and of save_table(). They live here, not in the functions Lua
    // calls, so that a Lua error never skips their destructors.
    std::vector<Value> host_args;
    Value host_result;
    std::string host_error;
    std::string saved;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        if (lua != nullptr) {
            lua_close(lua);
        }
    }
};

namespace {

using State = Sandbox::State;

constexpr int kMaxArguments = 200;
/// Where open_libraries() keeps table.sort, which a script cannot replace there (save_table).
constexpr const char* kSortKey = "o1602.sort";

State& state_of(lua_State* lua) {
    void* data = nullptr;
    lua_getallocf(lua, &data);
    return *static_cast<State*>(data);
}

// The allocator of the state, which keeps it below the memory limit.
void* allocate(void* data, void* block, std::size_t old_size, std::size_t new_size) {
    State& state = *static_cast<State*>(data);
    if (block == nullptr) {
        old_size = 0;  // Lua passes the kind of object here when it asks for a new block
    }
    if (new_size == 0) {
        std::free(block);  // NOLINT(cppcoreguidelines-no-malloc): Lua's allocator interface
        state.used -= old_size;
        return nullptr;
    }
    if (new_size > old_size && new_size - old_size > state.limits.memory_bytes - state.used) {
        return nullptr;  // Lua turns this into a "not enough memory" error
    }
    void* moved = std::realloc(block, new_size);  // NOLINT(cppcoreguidelines-no-malloc)
    if (moved == nullptr) {
        return nullptr;
    }
    state.used = state.used - old_size + new_size;
    return moved;
}

// Count hook: adds up the instructions and stops a script that runs past its limit. After the limit
// every further instruction fails too, so a pcall in the script cannot swallow the error and go on.
void count_instructions(lua_State* lua, lua_Debug* /*debug*/) {
    State& state = state_of(lua);
    state.executed += static_cast<std::uint64_t>(lua_gethookcount(lua));
    if (state.executed > state.limits.instructions) {
        lua_sethook(lua, count_instructions, LUA_MASKCOUNT, 1);
        luaL_error(lua, "the script ran too long (more than %I instructions)",
                   static_cast<lua_Integer>(state.limits.instructions));
    }
}

/// Pushes the text of a value as tostring() would, but without addresses: a table, function,
/// coroutine or userdata without __tostring becomes its type name. An address differs from
/// computer to computer and from run to run.
void push_text(lua_State* lua, int index) {
    index = lua_absindex(lua, index);
    switch (lua_type(lua, index)) {
        case LUA_TNIL:
        case LUA_TBOOLEAN:
        case LUA_TNUMBER:
        case LUA_TSTRING:
            luaL_tolstring(lua, index, nullptr);
            return;
        default:
            break;
    }
    if (luaL_getmetafield(lua, index, "__tostring") != LUA_TNIL) {
        lua_pop(lua, 1);
        luaL_tolstring(lua, index, nullptr);
        return;
    }
    lua_pushstring(lua, luaL_typename(lua, index));
}

int safe_tostring(lua_State* lua) {
    luaL_checkany(lua, 1);
    push_text(lua, 1);
    return 1;
}

int safe_print(lua_State* lua) {
    const int count = lua_gettop(lua);
    luaL_Buffer line;
    luaL_buffinit(lua, &line);
    for (int i = 1; i <= count; ++i) {
        if (i > 1) {
            luaL_addchar(&line, '\t');
        }
        push_text(lua, i);
        luaL_addvalue(&line);
    }
    luaL_pushresult(&line);
    std::size_t length = 0;
    const char* text = lua_tolstring(lua, -1, &length);
    const State& state = state_of(lua);
    if (state.print) {
        try {
            state.print(std::string_view(text, length));
        } catch (...) {  // NOLINT(bugprone-empty-catch): a failing log must not stop the script
        }
    }
    return 0;
}

std::uint32_t draw(lua_State* lua) {
    const State& state = state_of(lua);
    std::uint32_t number = 0;
    bool drawn = false;
    if (state.random) {
        try {
            number = state.random();
            drawn = true;
        } catch (...) {  // NOLINT(bugprone-empty-catch): reported below, outside the catch block
        }
    }
    if (!drawn) {
        luaL_error(lua, "math.random has no random numbers here");
    }
    return number;
}

/// Uniform in [0, span], without modulo bias.
lua_Unsigned uniform(lua_State* lua, lua_Unsigned span) {
    if (span <= 0xFFFF'FFFFU) {
        const std::uint64_t count = span + 1;
        const std::uint64_t limit = (std::uint64_t{1} << 32U) % count;  // draws below it favour small results
        for (;;) {
            const std::uint64_t r = draw(lua);
            if (r >= limit) {
                return r % count;
            }
        }
    }
    for (;;) {
        const lua_Unsigned r = (static_cast<lua_Unsigned>(draw(lua)) << 32U) | draw(lua);
        if (span == ~lua_Unsigned{0}) {
            return r;
        }
        const lua_Unsigned count = span + 1;
        if (r >= (lua_Unsigned{0} - count) % count) {
            return r % count;
        }
    }
}

// math.random as in Lua 5.4, but with the game's numbers: random() a float in [0, 1), random(m)
// an integer in [1, m], random(m, n) one in [m, n], random(0) an integer with all bits random.
int game_random(lua_State* lua) {
    lua_Integer low = 1;
    lua_Integer up = 0;
    switch (lua_gettop(lua)) {
        case 0:
            lua_pushnumber(lua, static_cast<lua_Number>(draw(lua)) * 0x1p-32);
            return 1;
        case 1:
            up = luaL_checkinteger(lua, 1);
            if (up == 0) {
                lua_pushinteger(lua, static_cast<lua_Integer>(uniform(lua, ~lua_Unsigned{0})));
                return 1;
            }
            break;
        case 2:
            low = luaL_checkinteger(lua, 1);
            up = luaL_checkinteger(lua, 2);
            break;
        default:
            return luaL_error(lua, "wrong number of arguments");
    }
    luaL_argcheck(lua, low <= up, 1, "interval is empty");
    const lua_Unsigned span = static_cast<lua_Unsigned>(up) - static_cast<lua_Unsigned>(low);
    lua_pushinteger(lua, static_cast<lua_Integer>(static_cast<lua_Unsigned>(low) + uniform(lua, span)));
    return 1;
}

/// Whether a format string holds a %p conversion, which prints an address.
bool formats_pointer(const char* format, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
        if (format[i] != '%') {
            continue;
        }
        ++i;
        while (i < length && (format[i] == '-' || format[i] == '+' || format[i] == ' ' || format[i] == '#' ||
                              format[i] == '.' || (format[i] >= '0' && format[i] <= '9'))) {
            ++i;
        }
        if (i < length && format[i] == 'p') {
            return true;
        }
    }
    return false;
}

// string.format without addresses: no %p, and tables and functions print as their type name.
int safe_format(lua_State* lua) {
    std::size_t length = 0;
    const char* format = luaL_checklstring(lua, 1, &length);
    if (formats_pointer(format, length)) {
        return luaL_error(lua, "string.format: '%%p' is not available in scripts");
    }
    const int count = lua_gettop(lua);
    for (int i = 2; i <= count; ++i) {
        switch (lua_type(lua, i)) {
            case LUA_TTABLE:
            case LUA_TFUNCTION:
            case LUA_TUSERDATA:
            case LUA_TLIGHTUSERDATA:
            case LUA_TTHREAD:
                push_text(lua, i);
                lua_replace(lua, i);
                break;
            default:
                break;
        }
    }
    lua_pushvalue(lua, lua_upvalueindex(1));
    lua_insert(lua, 1);
    lua_call(lua, count, 1);
    return 1;
}

// setmetatable without finalizers and weak tables: when the garbage collector runs differs between
// systems, so what those do would differ too.
int safe_setmetatable(lua_State* lua) {
    if (lua_type(lua, 2) == LUA_TTABLE) {
        for (const char* field : {"__gc", "__mode"}) {
            lua_pushstring(lua, field);
            if (lua_rawget(lua, 2) != LUA_TNIL) {
                return luaL_error(lua, "setmetatable: '%s' is not available in scripts", field);
            }
            lua_pop(lua, 1);
        }
    }
    const int count = lua_gettop(lua);
    lua_pushvalue(lua, lua_upvalueindex(1));
    lua_insert(lua, 1);
    lua_call(lua, count, 1);
    return 1;
}

void remove_fields(lua_State* lua, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        lua_pushnil(lua);
        lua_setfield(lua, -2, name);
    }
}

/// Replaces field `name` of the table on top with `function`, which gets the old value as upvalue.
void wrap_field(lua_State* lua, const char* name, lua_CFunction function) {
    lua_getfield(lua, -1, name);
    lua_pushcclosure(lua, function, 1);
    lua_setfield(lua, -2, name);
}

// Opens the libraries a script may use: base, coroutine, table, string, math and utf8. No io, os,
// package (require) and debug; base without the functions that read files, load binary chunks or
// look at the garbage collector.
int open_libraries(lua_State* lua) {
    static const luaL_Reg kLibraries[] = {
        {LUA_GNAME, luaopen_base},        {LUA_COLIBNAME, luaopen_coroutine}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},    {LUA_UTF8LIBNAME, luaopen_utf8},
    };
    for (const luaL_Reg& library : kLibraries) {
        luaL_requiref(lua, library.name, library.func, 1);
        lua_pop(lua, 1);
    }

    lua_pushglobaltable(lua);
    remove_fields(lua, {"dofile", "loadfile", "load", "collectgarbage", "warn"});
    lua_pushcfunction(lua, safe_print);
    lua_setfield(lua, -2, "print");
    lua_pushcfunction(lua, safe_tostring);
    lua_setfield(lua, -2, "tostring");
    wrap_field(lua, "setmetatable", safe_setmetatable);

    lua_getfield(lua, -1, LUA_TABLIBNAME);
    lua_getfield(lua, -1, "sort");
    lua_setfield(lua, LUA_REGISTRYINDEX, kSortKey);
    lua_pop(lua, 1);

    lua_getfield(lua, -1, LUA_STRLIBNAME);
    remove_fields(lua, {"dump"});
    wrap_field(lua, "format", safe_format);
    lua_pop(lua, 1);

    lua_getfield(lua, -1, LUA_MATHLIBNAME);
    remove_fields(lua, {"randomseed"});
    lua_pushcfunction(lua, game_random);
    lua_setfield(lua, -2, "random");
    lua_pop(lua, 2);
    return 0;
}

struct RunRequest {
    std::string_view name;
    std::string_view source;
    bool compile_only = false;
};

int run_chunk(lua_State* lua) {
    const auto& request = *static_cast<const RunRequest*>(lua_touserdata(lua, 1));
    lua_pushliteral(lua, "=");
    lua_pushlstring(lua, request.name.data(), request.name.size());
    lua_concat(lua, 2);
    if (luaL_loadbufferx(lua, request.source.data(), request.source.size(), lua_tostring(lua, -1), "t") != LUA_OK) {
        return lua_error(lua);
    }
    if (!request.compile_only) {
        lua_call(lua, 0, 0);
    }
    return 0;
}

struct CallRequest {
    std::string_view name;
    std::span<const Value> args;
    bool missing = false;
};

/// Pushes the global `name` and reports whether it is a function, without metamethods of _G.
bool push_global_function(lua_State* lua, std::string_view name) {
    lua_pushglobaltable(lua);
    lua_pushlstring(lua, name.data(), name.size());
    const bool function = lua_rawget(lua, -2) == LUA_TFUNCTION;
    lua_remove(lua, -2);
    return function;
}

int find_function(lua_State* lua) {
    const auto& name = *static_cast<const std::string_view*>(lua_touserdata(lua, 1));
    lua_pushboolean(lua, static_cast<int>(push_global_function(lua, name)));
    return 1;
}

void push_value(lua_State* lua, const Value& value, int depth = 0) {
    if (const auto* table = std::get_if<std::shared_ptr<const Table>>(&value)) {
        if (*table == nullptr || depth >= Sandbox::kMaxSavedDepth) {
            lua_pushnil(lua);
            return;
        }
        constexpr std::size_t kMost = std::numeric_limits<int>::max();
        luaL_checkstack(lua, 3, "tables nested too deep");
        lua_createtable(lua, static_cast<int>(std::min((*table)->items.size(), kMost)),
                        static_cast<int>(std::min((*table)->fields.size(), kMost)));
        lua_Integer index = 0;
        for (const Value& item : (*table)->items) {
            push_value(lua, item, depth + 1);
            lua_rawseti(lua, -2, ++index);
        }
        for (const auto& [name, field] : (*table)->fields) {
            lua_pushlstring(lua, name.data(), name.size());
            push_value(lua, field, depth + 1);
            lua_rawset(lua, -3);
        }
        return;
    }
    if (const auto* flag = std::get_if<bool>(&value)) {
        lua_pushboolean(lua, static_cast<int>(*flag));
    } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        lua_pushinteger(lua, *integer);
    } else if (const auto* number = std::get_if<double>(&value)) {
        lua_pushnumber(lua, *number);
    } else if (const auto* text = std::get_if<std::string>(&value)) {
        lua_pushlstring(lua, text->data(), text->size());
    } else {
        lua_pushnil(lua);
    }
}

// Calls the function and leaves its first result on the stack.
int call_function(lua_State* lua) {
    auto& request = *static_cast<CallRequest*>(lua_touserdata(lua, 1));
    if (!push_global_function(lua, request.name)) {
        request.missing = true;
        lua_pushlstring(lua, request.name.data(), request.name.size());
        return luaL_error(lua, "the script has no function '%s'", lua_tostring(lua, -1));
    }
    if (request.args.size() > static_cast<std::size_t>(kMaxArguments)) {
        return luaL_error(lua, "more than %d arguments", kMaxArguments);
    }
    const auto count = static_cast<int>(request.args.size());
    luaL_checkstack(lua, count, "too many arguments");
    for (const Value& value : request.args) {
        push_value(lua, value);
    }
    lua_call(lua, count, 1);
    return 1;
}

/// Arguments of host functions: tables at most this deep and with at most this many entries.
constexpr int kMaxArgumentDepth = 8;
constexpr std::size_t kMaxArgumentEntries = 4096;

/// A script's argument as a Value; false with `error` set for what the game does not take. Raises
/// no Lua error: strings are read without conversion and tables only walked with lua_next.
bool read_argument(lua_State* lua, int index, Value& out, std::string& error, int depth) {
    index = lua_absindex(lua, index);
    switch (lua_type(lua, index)) {
        case LUA_TNIL:
            out = std::monostate{};
            return true;
        case LUA_TBOOLEAN:
            out = lua_toboolean(lua, index) != 0;
            return true;
        case LUA_TNUMBER:
            if (lua_isinteger(lua, index) != 0) {
                out = static_cast<std::int64_t>(lua_tointeger(lua, index));
            } else {
                out = static_cast<double>(lua_tonumber(lua, index));
            }
            return true;
        case LUA_TSTRING: {
            std::size_t length = 0;
            const char* text = lua_tolstring(lua, index, &length);
            out = std::string(text, length);
            return true;
        }
        case LUA_TTABLE:
            break;
        default:
            error = std::string("a ") + luaL_typename(lua, index) +
                    " was given; the game's functions take nil, booleans, numbers, strings and tables of them";
            return false;
    }
    if (depth >= kMaxArgumentDepth || lua_checkstack(lua, 2) == 0) {
        error = "a table nested too deep was given";
        return false;
    }
    auto table = std::make_shared<Table>();
    lua_pushnil(lua);
    while (lua_next(lua, index) != 0) {
        Value value;
        if (table->fields.size() + table->items.size() >= kMaxArgumentEntries) {
            error = "a table of more than " + std::to_string(kMaxArgumentEntries) + " entries was given";
        } else if (read_argument(lua, -1, value, error, depth + 1)) {
            if (lua_type(lua, -2) == LUA_TSTRING) {
                std::size_t length = 0;
                const char* key = lua_tolstring(lua, -2, &length);
                table->fields.emplace_back(std::string(key, length), std::move(value));
            } else if (lua_isinteger(lua, -2) != 0 && lua_tointeger(lua, -2) >= 1 &&
                       lua_tointeger(lua, -2) <= static_cast<lua_Integer>(kMaxArgumentEntries)) {
                const auto at = static_cast<std::size_t>(lua_tointeger(lua, -2));
                if (table->items.size() < at) {
                    table->items.resize(at);
                }
                table->items[at - 1] = std::move(value);
            } else {
                error = "a table with a key that is no name and no index from 1 was given";
            }
        }
        if (!error.empty()) {
            lua_pop(lua, 2);
            return false;
        }
        lua_pop(lua, 1);
    }
    // Fields by name, whatever order the table kept them in.
    std::sort(table->fields.begin(), table->fields.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    out = std::shared_ptr<const Table>(std::move(table));
    return true;
}

/// Reads the arguments of a host function call, calls it and keeps its result or error in `state`.
/// Holds nothing on its own stack frame that a Lua error could skip.
bool run_host_function(lua_State* lua, State& state, std::size_t index) noexcept {
    try {
        state.host_args.clear();
        state.host_result = {};
        state.host_error.clear();
        const int count = lua_gettop(lua);
        state.host_args.resize(static_cast<std::size_t>(count));
        for (int i = 1; i <= count; ++i) {
            if (!read_argument(lua, i, state.host_args[static_cast<std::size_t>(i - 1)], state.host_error, 0)) {
                state.host_error = "argument " + std::to_string(i) + ": " + state.host_error;
                return false;
            }
        }
        core::Result<Value> result = state.functions[index](state.host_args);
        if (!result) {
            state.host_error = result.error().message;
            return false;
        }
        state.host_result = std::move(result).value();
        return true;
    } catch (const std::exception& e) {
        state.host_error = e.what();
    } catch (...) {
        state.host_error = "the game's function failed";
    }
    return false;
}

int call_host(lua_State* lua) {
    State& state = state_of(lua);
    const auto index = static_cast<std::size_t>(lua_tointeger(lua, lua_upvalueindex(1)));
    if (!run_host_function(lua, state, index)) {
        return luaL_error(lua, "%s", state.host_error.c_str());
    }
    push_value(lua, state.host_result);
    return 1;
}

struct AddRequest {
    std::string_view library;
    std::string_view name;
    const Value* value = nullptr;  ///< or:
    lua_Integer function = -1;     ///< index into State::functions
};

int add_entry(lua_State* lua) {
    const auto& request = *static_cast<const AddRequest*>(lua_touserdata(lua, 1));
    lua_pushglobaltable(lua);
    lua_pushlstring(lua, request.library.data(), request.library.size());
    if (lua_rawget(lua, -2) != LUA_TTABLE) {
        lua_pop(lua, 1);
        lua_newtable(lua);
        lua_pushlstring(lua, request.library.data(), request.library.size());
        lua_pushvalue(lua, -2);
        lua_rawset(lua, -4);
    }
    lua_pushlstring(lua, request.name.data(), request.name.size());
    if (request.value != nullptr) {
        push_value(lua, *request.value);
    } else {
        lua_pushinteger(lua, request.function);
        lua_pushcclosure(lua, call_host, 1);
    }
    lua_rawset(lua, -3);
    return 0;
}

// ---- save_table() and load_table() ----
//
// The bytes: a version byte (1), then one value. A value is a tag and its data, little-endian:
// 1 false, 2 true, 3 integer (8 bytes), 4 float (its 8 bytes), 5 string (u32 length, bytes),
// 6 table (u32 count, then key and value of each entry; keys are booleans, numbers and strings in
// the order of key_less).

constexpr std::uint8_t kSaveVersion = 1;
enum : std::uint8_t { kFalse = 1, kTrue, kInteger, kFloat, kString, kTable };

void append(lua_State* lua, State& state, const void* data, std::size_t size) {
    bool appended = false;
    try {
        if (state.saved.size() + size <= state.limits.memory_bytes) {
            state.saved.append(static_cast<const char*>(data), size);
            appended = true;
        }
    } catch (...) {  // NOLINT(bugprone-empty-catch): reported below, outside the catch block
    }
    if (!appended) {
        luaL_error(lua, "the script's state is too large to save");
    }
}

void append_u8(lua_State* lua, State& state, std::uint8_t value) {
    append(lua, state, &value, 1);
}

void append_u32(lua_State* lua, State& state, std::uint32_t value) {
    const std::uint8_t bytes[] = {static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8U),
                                  static_cast<std::uint8_t>(value >> 16U), static_cast<std::uint8_t>(value >> 24U)};
    append(lua, state, bytes, sizeof(bytes));
}

void append_u64(lua_State* lua, State& state, std::uint64_t value) {
    append_u32(lua, state, static_cast<std::uint32_t>(value));
    append_u32(lua, state, static_cast<std::uint32_t>(value >> 32U));
}

bool savable_key(lua_State* lua, int index) {
    const int type = lua_type(lua, index);
    return type == LUA_TBOOLEAN || type == LUA_TNUMBER || type == LUA_TSTRING;
}

bool savable_value(lua_State* lua, int index, int depth) {
    return savable_key(lua, index) || (lua_type(lua, index) == LUA_TTABLE && depth < Sandbox::kMaxSavedDepth);
}

// Booleans (false first), then numbers by value, then strings by their bytes: the same order on every
// computer, unlike the order of pairs() or strcoll().
int key_less(lua_State* lua) {
    const auto rank = [lua](int index) {
        const int type = lua_type(lua, index);
        return type == LUA_TBOOLEAN ? 0 : type == LUA_TNUMBER ? 1 : 2;
    };
    const int a = rank(1);
    const int b = rank(2);
    bool less = a < b;
    if (a == b) {
        if (a == 0) {
            less = lua_toboolean(lua, 1) == 0 && lua_toboolean(lua, 2) != 0;
        } else if (a == 1) {
            less = lua_compare(lua, 1, 2, LUA_OPLT) != 0;
        } else {
            std::size_t la = 0;
            std::size_t lb = 0;
            const char* sa = lua_tolstring(lua, 1, &la);
            const char* sb = lua_tolstring(lua, 2, &lb);
            const int order = std::memcmp(sa, sb, std::min(la, lb));
            less = order < 0 || (order == 0 && la < lb);
        }
    }
    lua_pushboolean(lua, static_cast<int>(less));
    return 1;
}

void write_value(lua_State* lua, State& state, int index, int depth) {
    index = lua_absindex(lua, index);
    switch (lua_type(lua, index)) {
        case LUA_TBOOLEAN:
            append_u8(lua, state, lua_toboolean(lua, index) != 0 ? kTrue : kFalse);
            return;
        case LUA_TNUMBER:
            if (lua_isinteger(lua, index) != 0) {
                append_u8(lua, state, kInteger);
                append_u64(lua, state, static_cast<std::uint64_t>(lua_tointeger(lua, index)));
            } else {
                const double number = lua_tonumber(lua, index);
                std::uint64_t bits = 0;
                std::memcpy(&bits, &number, sizeof(bits));
                append_u8(lua, state, kFloat);
                append_u64(lua, state, bits);
            }
            return;
        case LUA_TSTRING: {
            std::size_t length = 0;
            const char* text = lua_tolstring(lua, index, &length);
            if (length > 0xFFFF'FFFFU) {
                luaL_error(lua, "a string of the script's state is too long to save");
            }
            append_u8(lua, state, kString);
            append_u32(lua, state, static_cast<std::uint32_t>(length));
            append(lua, state, text, length);
            return;
        }
        default:
            break;
    }
    // A table: its keys that can be saved, sorted, then each with its value.
    luaL_checkstack(lua, 8, "the script's state is nested too deep");
    lua_newtable(lua);
    const int keys = lua_gettop(lua);
    lua_Integer count = 0;
    lua_pushnil(lua);
    while (lua_next(lua, index) != 0) {
        if (savable_key(lua, -2) && savable_value(lua, -1, depth + 1)) {
            lua_pushvalue(lua, -2);
            lua_rawseti(lua, keys, ++count);
        }
        lua_pop(lua, 1);
    }
    if (count > 0xFFFF'FFFF) {
        luaL_error(lua, "a table of the script's state is too large to save");
    }
    if (count > 1) {
        lua_getfield(lua, LUA_REGISTRYINDEX, kSortKey);
        lua_pushvalue(lua, keys);
        lua_pushcfunction(lua, key_less);
        lua_call(lua, 2, 0);
    }
    append_u8(lua, state, kTable);
    append_u32(lua, state, static_cast<std::uint32_t>(count));
    for (lua_Integer i = 1; i <= count; ++i) {
        lua_rawgeti(lua, keys, i);
        write_value(lua, state, -1, depth + 1);
        lua_rawget(lua, index);
        write_value(lua, state, -1, depth + 1);
        lua_pop(lua, 1);
    }
    lua_pop(lua, 1);
}

int save_global(lua_State* lua) {
    const auto& name = *static_cast<const std::string_view*>(lua_touserdata(lua, 1));
    State& state = state_of(lua);
    state.saved.clear();
    lua_pushglobaltable(lua);
    lua_pushlstring(lua, name.data(), name.size());
    if (lua_rawget(lua, -2) == LUA_TTABLE) {
        append_u8(lua, state, kSaveVersion);
        write_value(lua, state, -1, 0);
    }
    return 0;
}

struct LoadRequest {
    std::string_view name;
    std::span<const std::uint8_t> bytes;
    std::size_t at = 0;
};

[[noreturn]] void damaged(lua_State* lua) {
    luaL_error(lua, "the saved state of the script is damaged");
    std::abort();  // luaL_error does not return
}

std::uint64_t read_bytes(lua_State* lua, LoadRequest& request, std::size_t count) {
    if (request.bytes.size() - request.at < count) {
        damaged(lua);
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        value |= static_cast<std::uint64_t>(request.bytes[request.at + i]) << (8U * i);
    }
    request.at += count;
    return value;
}

void read_value(lua_State* lua, LoadRequest& request, int depth) {
    switch (read_bytes(lua, request, 1)) {
        case kFalse:
            lua_pushboolean(lua, 0);
            return;
        case kTrue:
            lua_pushboolean(lua, 1);
            return;
        case kInteger:
            lua_pushinteger(lua, static_cast<lua_Integer>(read_bytes(lua, request, 8)));
            return;
        case kFloat: {
            const std::uint64_t bits = read_bytes(lua, request, 8);
            double number = 0;
            std::memcpy(&number, &bits, sizeof(number));
            lua_pushnumber(lua, number);
            return;
        }
        case kString: {
            const auto length = static_cast<std::size_t>(read_bytes(lua, request, 4));
            if (request.bytes.size() - request.at < length) {
                damaged(lua);
            }
            lua_pushlstring(lua, reinterpret_cast<const char*>(request.bytes.data() + request.at), length);
            request.at += length;
            return;
        }
        case kTable:
            break;
        default:
            damaged(lua);
    }
    const auto count = static_cast<std::size_t>(read_bytes(lua, request, 4));
    if (depth >= Sandbox::kMaxSavedDepth || count > (request.bytes.size() - request.at) / 4) {
        damaged(lua);  // every entry takes at least four bytes (two tags and a boolean each)
    }
    luaL_checkstack(lua, 4, "the saved state of the script is nested too deep");
    lua_newtable(lua);
    for (std::size_t i = 0; i < count; ++i) {
        read_value(lua, request, depth + 1);
        if (!savable_key(lua, -1) || (lua_type(lua, -1) == LUA_TNUMBER && lua_isinteger(lua, -1) == 0 &&
                                      lua_tonumber(lua, -1) != lua_tonumber(lua, -1))) {
            damaged(lua);  // a key must be a boolean, a number other than NaN or a string
        }
        read_value(lua, request, depth + 1);
        lua_rawset(lua, -3);
    }
}

int load_global(lua_State* lua) {
    auto& request = *static_cast<LoadRequest*>(lua_touserdata(lua, 1));
    if (request.bytes.empty()) {
        return 0;
    }
    if (read_bytes(lua, request, 1) != kSaveVersion) {
        return luaL_error(lua, "the saved state of the script is of a newer Open1602");
    }
    read_value(lua, request, 0);
    if (request.at != request.bytes.size() || lua_type(lua, -1) != LUA_TTABLE) {
        damaged(lua);
    }
    lua_pushglobaltable(lua);
    lua_pushlstring(lua, request.name.data(), request.name.size());
    lua_pushvalue(lua, -3);
    lua_rawset(lua, -3);
    return 0;
}

/// The message of a failed protected call, which is on top of the stack.
std::string error_message(lua_State* lua, int status, const Limits& limits) {
    if (status == LUA_ERRMEM) {
        return "the script needs more than " + std::to_string(limits.memory_bytes) + " bytes of memory";
    }
    if (lua_type(lua, -1) == LUA_TSTRING || lua_type(lua, -1) == LUA_TNUMBER) {
        std::size_t length = 0;
        const char* text = lua_tolstring(lua, -1, &length);
        return {text, length};
    }
    return std::string("the script raised an error that is no text but a ") + luaL_typename(lua, -1);
}

/// Runs `function` with `request` as its argument under the instruction limit. On success its
/// `results` results are on the stack, on failure nothing is.
core::Status protected_call(State& state, lua_CFunction function, void* request, int results) {
    lua_State* lua = state.lua;
    state.executed = 0;
    lua_sethook(lua, count_instructions, LUA_MASKCOUNT, Sandbox::kInstructionStep);
    lua_pushcfunction(lua, function);
    lua_pushlightuserdata(lua, request);
    const int status = lua_pcall(lua, 1, results, 0);
    if (status == LUA_OK) {
        return core::Ok{};
    }
    std::string message = error_message(lua, status, state.limits);
    lua_pop(lua, 1);
    return core::make_error(core::ErrorCode::ScriptError, std::move(message));
}

/// A protected call of the game's own work (no script code runs): leaves the instruction count of the
/// last run() or call() as it is.
core::Status protected_setup(State& state, lua_CFunction function, void* request) {
    lua_State* lua = state.lua;
    lua_pushcfunction(lua, function);
    lua_pushlightuserdata(lua, request);
    const int status = lua_pcall(lua, 1, 0, 0);
    if (status == LUA_OK) {
        return core::Ok{};
    }
    std::string message = error_message(lua, status, state.limits);
    lua_pop(lua, 1);
    return core::make_error(core::ErrorCode::ScriptError, std::move(message));
}

}  // namespace

Value make_table(std::vector<std::pair<std::string, Value>> fields, std::vector<Value> items) {
    auto table = std::make_shared<Table>();
    table->fields = std::move(fields);
    table->items = std::move(items);
    return std::shared_ptr<const Table>(std::move(table));
}

Sandbox::Sandbox(Limits limits) : m_state(std::make_unique<State>()) {
    m_state->limits = limits;
    m_state->lua = lua_newstate(allocate, m_state.get());
    if (m_state->lua == nullptr) {
        throw std::bad_alloc();
    }
    lua_pushcfunction(m_state->lua, open_libraries);
    if (lua_pcall(m_state->lua, 0, 0, 0) != LUA_OK) {
        throw std::bad_alloc();  // opening the libraries can only fail for lack of memory
    }
}

Sandbox::~Sandbox() = default;
Sandbox::Sandbox(Sandbox&&) noexcept = default;
Sandbox& Sandbox::operator=(Sandbox&&) noexcept = default;

void Sandbox::set_random(RandomSource source) {
    m_state->random = std::move(source);
}

void Sandbox::set_print(PrintSink sink) {
    m_state->print = std::move(sink);
}

core::Status Sandbox::add_function(std::string_view library, std::string_view name, HostFunction function) {
    m_state->functions.push_back(std::move(function));
    AddRequest request{library, name, nullptr, static_cast<lua_Integer>(m_state->functions.size() - 1)};
    return protected_setup(*m_state, add_entry, &request);
}

core::Status Sandbox::add_value(std::string_view library, std::string_view name, const Value& value) {
    AddRequest request{library, name, &value};
    return protected_setup(*m_state, add_entry, &request);
}

core::Result<std::vector<std::uint8_t>> Sandbox::save_table(std::string_view name) {
    const core::Status status = protected_setup(*m_state, save_global, &name);
    std::vector<std::uint8_t> bytes(m_state->saved.begin(), m_state->saved.end());
    m_state->saved = std::string();
    if (!status) {
        return status.error();
    }
    return bytes;
}

core::Status Sandbox::load_table(std::string_view name, std::span<const std::uint8_t> bytes) {
    LoadRequest request{name, bytes};
    return protected_setup(*m_state, load_global, &request);
}

core::Status Sandbox::run(std::string_view name, std::string_view source) {
    RunRequest request{name, source};
    return protected_call(*m_state, run_chunk, &request, 0);
}

core::Status Sandbox::compile(std::string_view name, std::string_view source) {
    RunRequest request{name, source, true};
    return protected_call(*m_state, run_chunk, &request, 0);
}

bool Sandbox::has_function(std::string_view name) const {
    lua_State* lua = m_state->lua;
    lua_pushcfunction(lua, find_function);
    lua_pushlightuserdata(lua, &name);
    if (lua_pcall(lua, 1, 1, 0) != LUA_OK) {
        lua_pop(lua, 1);
        return false;
    }
    const bool found = lua_toboolean(lua, -1) != 0;
    lua_pop(lua, 1);
    return found;
}

core::Result<Value> Sandbox::call(std::string_view name, std::span<const Value> args) {
    CallRequest request{name, args};
    core::Status status = protected_call(*m_state, call_function, &request, 1);
    if (!status) {
        if (request.missing) {
            return core::make_error(core::ErrorCode::NotFound, status.error().message);
        }
        return status.error();
    }
    lua_State* lua = m_state->lua;
    Value result;
    switch (lua_type(lua, -1)) {
        case LUA_TNIL:
            break;
        case LUA_TBOOLEAN:
            result = lua_toboolean(lua, -1) != 0;
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(lua, -1) != 0) {
                result = static_cast<std::int64_t>(lua_tointeger(lua, -1));
            } else {
                result = static_cast<double>(lua_tonumber(lua, -1));
            }
            break;
        case LUA_TSTRING: {
            std::size_t length = 0;
            const char* text = lua_tolstring(lua, -1, &length);
            result = std::string(text, length);
            break;
        }
        default: {
            std::string message = "function '" + std::string(name) + "' returned a " + luaL_typename(lua, -1) +
                                  "; only nil, booleans, numbers and strings come back to the game";
            lua_pop(lua, 1);
            return core::make_error(core::ErrorCode::ScriptError, std::move(message));
        }
    }
    lua_pop(lua, 1);
    return result;
}

std::size_t Sandbox::memory_used() const noexcept {
    return m_state->used;
}

std::uint64_t Sandbox::instructions_used() const noexcept {
    return m_state->executed;
}

}  // namespace o1602::script
