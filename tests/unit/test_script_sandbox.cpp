// SPDX-License-Identifier: GPL-3.0-or-later
// The sandboxed Lua state of mod scripts (engine/script/include/o1602/script/sandbox.hpp,
// docs/modding/scripts.md).

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/script/sandbox.hpp"
#include "o1602/sim/random.hpp"

using namespace o1602;

namespace {

/// Runs `source` and returns the result of its global function `result`.
script::Value evaluate(script::Sandbox& sandbox, const std::string& source) {
    const auto ran = sandbox.run("test", source);
    if (!ran) {
        std::fprintf(stderr, "%s\n", ran.error().message.c_str());
    }
    REQUIRE(ran.ok());
    auto value = sandbox.call("result");
    if (!value) {
        std::fprintf(stderr, "%s\n", value.error().message.c_str());
    }
    REQUIRE(value.ok());
    return std::move(value).value();
}

std::string text(script::Sandbox& sandbox, const std::string& source) {
    const script::Value value = evaluate(sandbox, source);
    const auto* string = std::get_if<std::string>(&value);
    REQUIRE(string != nullptr);
    return *string;
}

std::string error_of(script::Sandbox& sandbox, const std::string& source) {
    const auto ran = sandbox.run("test", source);
    REQUIRE(!ran.ok());
    return ran.error().message;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::vector<script::Value> args(std::int64_t a, std::string b) {
    return {script::Value{a}, script::Value{std::move(b)}};
}

}  // namespace

TEST_CASE("script: runs a script and calls its functions with values") {
    script::Sandbox sandbox;
    REQUIRE(sandbox.run("greeting", "function greet(n, name) return name .. ' x' .. n end").ok());
    CHECK(sandbox.has_function("greet"));
    CHECK(!sandbox.has_function("missing"));

    const auto values = args(3, "Ahoy");
    const auto result = sandbox.call("greet", values);
    REQUIRE(result.ok());
    CHECK(std::get<std::string>(*result) == "Ahoy x3");

    CHECK(std::get<std::int64_t>(evaluate(sandbox, "function result() return 7 // 2 end")) == 3);
    CHECK(std::get<double>(evaluate(sandbox, "function result() return 7 / 2 end")) == 3.5);
    CHECK(std::get<bool>(evaluate(sandbox, "function result() return 1 < 2 end")));
    CHECK(std::holds_alternative<std::monostate>(evaluate(sandbox, "function result() end")));

    // Globals stay between runs and calls.
    REQUIRE(sandbox.run("a", "counter = 40").ok());
    REQUIRE(sandbox.run("b", "function bump() counter = counter + 1 return counter end").ok());
    REQUIRE(sandbox.call("bump").ok());
    CHECK(std::get<std::int64_t>(*sandbox.call("bump")) == 42);
}

TEST_CASE("script: errors name the script and line and leave the state usable") {
    script::Sandbox sandbox;
    const std::string syntax = error_of(sandbox, "x = = 1");
    CHECK(contains(syntax, "test:1:"));

    REQUIRE(sandbox.run("quests", "function fail()\n  error('no ships')\nend").ok());
    const auto failed = sandbox.call("fail");
    REQUIRE(!failed.ok());
    CHECK(failed.error().code == core::ErrorCode::ScriptError);
    CHECK(contains(failed.error().message, "quests:2: no ships"));

    const auto missing = sandbox.call("nothing");
    REQUIRE(!missing.ok());
    CHECK(missing.error().code == core::ErrorCode::NotFound);

    REQUIRE(sandbox.run("t", "function table_back() return {} end").ok());
    const auto table = sandbox.call("table_back");
    REQUIRE(!table.ok());
    CHECK(contains(table.error().message, "returned a table"));

    CHECK(contains(error_of(sandbox, "error({})"), "no text but a table"));
    CHECK(std::get<std::int64_t>(evaluate(sandbox, "function result() return 5 end")) == 5);
}

TEST_CASE("script: a text is read without running it, a syntax error with its line") {
    script::Sandbox sandbox;
    CHECK(sandbox.compile("scenario", "x = 1\nerror('not run')\n").ok());
    CHECK(!sandbox.has_function("later"));
    REQUIRE(sandbox.compile("scenario", "function later() end").ok());
    CHECK(!sandbox.has_function("later"));  // read, not run
    const auto bad = sandbox.compile("scenario", "x = 1\nif then\n");
    REQUIRE(!bad.ok());
    CHECK(contains(bad.error().message, "scenario:2:"));
}

TEST_CASE("script: no files, no clock, no loading of code") {
    script::Sandbox sandbox;
    CHECK(text(sandbox,
               "function result() return type(io) .. type(os) .. type(require) .. type(package) .. type(debug) end") ==
          "nilnilnilnilnil");
    CHECK(text(sandbox,
               "function result() return type(dofile) .. type(loadfile) .. type(load) .. type(collectgarbage) .. "
               "type(string.dump) .. type(math.randomseed) end") == "nilnilnilnilnilnil");
    CHECK(
        text(sandbox, "function result() return type(string.format) .. type(coroutine.wrap) .. type(utf8.char) end") ==
        "functionfunctionfunction");

    // Binary chunks are refused.
    const std::string binary("\x1bLua\x54", 5);
    CHECK(contains(error_of(sandbox, binary), "binary"));
}

TEST_CASE("script: no addresses in texts") {
    script::Sandbox sandbox;
    CHECK(text(sandbox, "function result() return tostring({}) .. ' ' .. tostring(print) end") == "table function");
    CHECK(text(sandbox, "function result() return string.format('%s|%5s', {}, true) end") == "table| true");
    CHECK(text(sandbox,
               "local t = setmetatable({}, {__tostring = function() return 'fleet' end})\n"
               "function result() return tostring(t) .. string.format(' %s', t) end") == "fleet fleet");
    CHECK(contains(error_of(sandbox, "local s = string.format('%p', {})"), "'%p' is not available"));
    CHECK(contains(error_of(sandbox, "local s = ('%-8p'):format({})"), "'%p' is not available"));
    CHECK(text(sandbox, "function result() return string.format('100%% %d', 5) end") == "100% 5");

    std::vector<std::string> lines;
    sandbox.set_print([&lines](std::string_view line) { lines.emplace_back(line); });
    REQUIRE(sandbox.run("p", "print('ships', 3, {}, nil)").ok());
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "ships\t3\ttable\tnil");
}

TEST_CASE("script: no finalizers and weak tables") {
    script::Sandbox sandbox;
    CHECK(contains(error_of(sandbox, "setmetatable({}, {__gc = function() end})"), "'__gc' is not available"));
    CHECK(contains(error_of(sandbox, "setmetatable({}, {__mode = 'k'})"), "'__mode' is not available"));
    CHECK(text(sandbox,
               "local t = setmetatable({}, {__index = function() return 'ok' end})\n"
               "function result() return t.anything end") == "ok");
}

TEST_CASE("script: random numbers come from the game") {
    script::Sandbox sandbox;
    REQUIRE(sandbox
                .run("dice",
                     "function roll() return math.random(6) end\n"
                     "function pick() return math.random(-3, 3) end\n"
                     "function any() return math.random() end\n"
                     "function wide() return math.random(0) end")
                .ok());
    const auto without = sandbox.call("roll");
    REQUIRE(!without.ok());
    CHECK(contains(without.error().message, "no random numbers"));

    sim::Random generator(1602, 7);
    sandbox.set_random([&generator] { return generator.next(); });
    for (int i = 0; i < 200; ++i) {
        const auto roll = std::get<std::int64_t>(*sandbox.call("roll"));
        CHECK(roll >= 1 && roll <= 6);
        const auto pick = std::get<std::int64_t>(*sandbox.call("pick"));
        CHECK(pick >= -3 && pick <= 3);
        const double any = std::get<double>(*sandbox.call("any"));
        CHECK(any >= 0.0 && any < 1.0);
    }
    REQUIRE(sandbox.call("wide").ok());

    // The same generator gives the same rolls.
    std::vector<std::int64_t> first;
    std::vector<std::int64_t> second;
    for (auto* rolls : {&first, &second}) {
        generator = sim::Random(99, 1);
        for (int i = 0; i < 20; ++i) {
            rolls->push_back(std::get<std::int64_t>(*sandbox.call("roll")));
        }
    }
    CHECK(first == second);

    CHECK(contains(error_of(sandbox, "local x = math.random(5, 1)"), "interval is empty"));
}

TEST_CASE("script: the instruction limit stops endless loops, also inside pcall") {
    script::Limits limits;
    limits.instructions = 100'000;
    script::Sandbox sandbox(limits);
    CHECK(contains(error_of(sandbox, "while true do end"), "ran too long"));
    CHECK(contains(error_of(sandbox, "while true do pcall(function() while true do end end) end"), "ran too long"));
    CHECK(contains(error_of(sandbox, "local co = coroutine.wrap(function() while true do end end) co()"),
                   "ran too long"));

    // Each entry starts with a fresh budget.
    REQUIRE(sandbox.run("loop", "function work(n) local s = 0 for i = 1, n do s = s + i end return s end").ok());
    const std::vector<script::Value> small{script::Value{std::int64_t{1000}}};
    CHECK(std::get<std::int64_t>(*sandbox.call("work", small)) == 500500);
    CHECK(sandbox.instructions_used() <= 10 * script::Sandbox::kInstructionStep);
    CHECK(std::get<std::int64_t>(*sandbox.call("work", small)) == 500500);
}

TEST_CASE("script: the memory limit stops a script that grows without end") {
    script::Limits limits;
    limits.memory_bytes = std::size_t{2} * 1024 * 1024;
    script::Sandbox sandbox(limits);
    const std::size_t empty = sandbox.memory_used();
    CHECK(empty > 0);
    CHECK(empty < limits.memory_bytes);

    CHECK(contains(error_of(sandbox, "local s = string.rep('x', 10 * 1024 * 1024)"), "bytes of memory"));
    CHECK(contains(error_of(sandbox, "local t = {} for i = 1, 1e9 do t[i] = i end"), "bytes of memory"));
    CHECK(sandbox.memory_used() <= limits.memory_bytes);

    // Still usable afterwards.
    CHECK(std::get<std::int64_t>(evaluate(sandbox, "function result() return 1 end")) == 1);
}

TEST_CASE("script: the order of pairs over string keys is the same in every state") {
    const std::string source =
        "local t = {}\n"
        "for _, name in ipairs({'wood', 'tools', 'bricks', 'cloth', 'wine', 'spices', 'tobacco', 'gold'}) do\n"
        "  t[name] = #name\n"
        "end\n"
        "function result() local s = '' for k in pairs(t) do s = s .. k .. ',' end return s end";
    script::Sandbox first;
    script::Sandbox second;
    const std::string order = text(first, source);
    CHECK(order == text(second, source));
    CHECK(order.size() == std::string("wood,tools,bricks,cloth,wine,spices,tobacco,gold,").size());
}
