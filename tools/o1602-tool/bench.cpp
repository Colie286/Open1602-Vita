// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool bench: runs a game headless as fast as it goes and measures the ticks, for estimating
// what a computer needs (docs/performance.md). Every player is handed to the computer so that the
// world keeps building without anyone at the controls.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "mod_rules.hpp"
#include "tool.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_content.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/sim/balance.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/sim/world_plan.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h first
#include <psapi.h>
#else
#include <cstdlib>
#include <sys/resource.h>
#include <time.h>
#endif

namespace o1602::tool {

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// CPU time of the calling thread in microseconds, which other threads and processes taking the
/// core do not inflate (BT-4); nothing where it is not available or too coarse (Windows counts
/// thread time in scheduler quanta).
std::optional<double> thread_cpu_us() {
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0) {
        return static_cast<double>(t.tv_sec) * 1e6 + static_cast<double>(t.tv_nsec) / 1e3;
    }
#endif
    return std::nullopt;
}

/// The load average of the last minute where the system tells it.
std::optional<double> load_average() {
#if !defined(_WIN32)
    double load[1] = {};
    if (getloadavg(load, 1) == 1) {
        return load[0];
    }
#endif
    return std::nullopt;
}

/// The most memory the process has held so far, in MB; nothing where it cannot be asked.
std::optional<double> peak_memory_mb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters) == 0) {
        return std::nullopt;
    }
    return static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return std::nullopt;
    }
#if defined(__APPLE__)
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);  // bytes
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;  // KB
#endif
#endif
}

/// A tick at speed 8 has 50 ms / 8 of real time.
constexpr double kBudgetUs = 6250.0;

/// Tick times of one report period in microseconds.
struct Period {
    std::vector<double> ticks;

    [[nodiscard]] double mean() const {
        double sum = 0.0;
        for (const double t : ticks) {
            sum += t;
        }
        return ticks.empty() ? 0.0 : sum / static_cast<double>(ticks.size());
    }
    /// The value below which the share `q` of the ticks lies (sorts the ticks).
    [[nodiscard]] double quantile(double q) {
        if (ticks.empty()) {
            return 0.0;
        }
        std::sort(ticks.begin(), ticks.end());
        const auto i = static_cast<std::size_t>(q * static_cast<double>(ticks.size()));
        return ticks[std::min(i, ticks.size() - 1)];
    }
};

void print_world_line(const sim::World& world, const sim::Simulation& simulation) {
    std::uint64_t inhabitants = 0;
    for (const sim::City& city : world.cities()) {
        for (const std::uint32_t n : city.inhabitants) {
            inhabitants += n;
        }
    }
    std::printf(" %7zu %8llu %6zu %5zu %8zu %7zu\n", world.houses().size(),
                static_cast<unsigned long long>(inhabitants), world.productions().size(), world.ships().size(),
                world.soldiers().size(), simulation.logistics().walkers().size());
}

}  // namespace

int cmd_bench(const Args& args) {
    if (args.positional.size() > 1) {
        return usage();
    }
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        return fail(game.error());
    }
    auto buildings = assets::BuildingTable::load(*game);
    if (!buildings) {
        return fail(buildings.error());
    }
    auto figures = assets::FigureTable::load(*game);
    if (!figures) {
        return fail(figures.error());
    }
    // The rules of the packages of a mods folder, as a new game of the game takes them.
    std::vector<assets::ModPackage> packages;
    std::optional<app::ModRules> rules;
    if (const auto it = args.options.find("--mods"); it != args.options.end()) {
        const fs::path mods(std::string(it->second));
        std::error_code ec;
        if (!fs::is_directory(mods, ec)) {
            return fail(
                core::make_error(core::ErrorCode::NotFound, std::string(it->second) + ": no folder of packages"));
        }
        std::vector<std::string> problems;
        packages = assets::load_mod_packages(mods, problems);
        rules = app::load_mod_rules(packages, *buildings, *figures, problems);
        for (const std::string& p : problems) {
            std::fprintf(stderr, "mod problem: %s\n", p.c_str());
        }
        std::printf("%zu packages from %s, %zu problems\n", packages.size(), std::string(it->second).c_str(),
                    problems.size());
    }
    // The tables with the packages' buildings and figures, before a game's values change them.
    const assets::BuildingTable original_buildings = *buildings;
    const assets::FigureTable original_figures = *figures;
    std::vector<std::string> warnings;
    const auto use_rules = [&](core::Result<sim::World> world) {
        if (world && rules) {
            world->set_factions(rules->factions);
            world->replace_factions(rules->faction_replacements);
            world->set_balance(rules->balance);
            world->set_packages(assets::content_of(packages));
            world->set_own_buildings(rules->own);
            world->set_own_figures(rules->own_figures);
            world->set_goods(rules->goods);
            world->set_resident_classes(rules->classes);
            world->set_infra_levels(rules->levels);
        }
        return world;
    };
    const auto make_world = [&]() -> core::Result<sim::World> {
        if (!args.positional.empty()) {
            auto bytes = core::read_file(fs::path(args.positional[0]));
            if (!bytes) {
                return bytes.error();
            }
            if (sim::is_open1602_save(*bytes)) {
                return sim::read_game(*bytes, *buildings, &warnings, &*figures);
            }
            auto file = assets::ChunkFile::from_bytes(*bytes);
            if (!file) {
                return file.error();
            }
            return use_rules(sim::load_world(*file, *buildings, *game, &warnings));
        }
        auto texts = assets::TextTable::load(*game);
        if (!texts) {
            return texts.error();
        }
        sim::NewGameOptions options;
        options.name = "Bench";
        options.difficulty = static_cast<std::uint8_t>(std::min<std::size_t>(args.number("--level").value_or(1), 3));
        options.world.seed = args.number("--seed").value_or(1);
        options.world.guarantee_resources = true;
        // Without --world as large as a multiplayer game of that many players makes it.
        sim::WorldSettings settings = sim::settings_for_players(static_cast<std::uint8_t>(
            std::clamp<std::size_t>(args.number("--players").value_or(8), 1, sim::kMaxPlayers)));
        if (const auto size = args.pair("--world", 'x')) {
            settings.width = static_cast<std::uint32_t>(std::max(0, size->first));
            settings.height = static_cast<std::uint32_t>(std::max(0, size->second));
        }
        options.custom = settings;
        if (rules) {
            options.world.climates = rules->balance.climates;
        }
        return use_rules(sim::start_new_game(*game, *buildings, *texts, options, &warnings));
    };
    auto world = make_world();
    if (!world) {
        return fail(world.error());
    }
    for (const auto& text : warnings) {
        std::fprintf(stderr, "warning: %s\n", text.c_str());
    }

    const std::size_t minutes = std::max<std::size_t>(args.number("--minutes").value_or(600), 1);
    const std::size_t period_minutes = std::clamp<std::size_t>(args.number("--period").value_or(60), 1, minutes);
    const std::size_t repeats = std::clamp<std::size_t>(args.number("--repeat").value_or(1), 1, 100);
    const bool wall = args.flag("--wall") || !thread_cpu_us();
    const std::uint64_t ticks_per_minute = 60'000 / sim::Simulation::kTickMs;
    const std::size_t tick_count = minutes * ticks_per_minute;
    // The run is deterministic, so every repeat runs the same ticks: the fastest time of each tick
    // is what the code needs, the rest is the machine (BT-4).
    std::vector<double> best(tick_count, 0.0);
    const std::vector<std::string_view> steps = sim::Simulation::tick_step_names();
    std::vector<std::uint64_t> step_ns(steps.size(), 0);
    std::vector<std::uint64_t> step_total_ns(steps.size(), 0);
    std::vector<std::uint64_t> step_max_ns(steps.size(), 0);
    const auto now_us = [&]() { return wall ? ms_since(Clock::time_point{}) * 1000.0 : *thread_cpu_us(); };

    std::optional<sim::Simulation> simulation;
    for (std::size_t run = 0; run < repeats; ++run) {
        const bool last = run + 1 == repeats;
        simulation.reset();
        if (run > 0) {
            auto again = make_world();
            if (!again) {
                return fail(again.error());
            }
            world = std::move(again);
        }
        // The game's values, classes and levels in the tables, as the game sets them when it starts.
        sim::apply_building_values(*buildings, original_buildings, world->balance());
        sim::apply_resident_classes(*buildings, original_buildings, world->resident_classes());
        sim::apply_infra_levels(*buildings, original_buildings, world->infra_levels());
        sim::apply_figure_values(*figures, original_figures, world->balance());
        simulation.emplace(*world, *buildings, *figures);
        std::size_t handed_over = 0;
        for (sim::PlayerId p = sim::first_human_player(*world); p != sim::kNoPlayer;
             p = sim::first_human_player(*world)) {
            if (!simulation->execute(sim::PlayerControl{p, true}).accepted) {
                return fail(core::make_error(core::ErrorCode::Unsupported,
                                             "player " + std::to_string(p) + " cannot be handed to the computer"));
            }
            ++handed_over;
        }
        if (run == 0) {
            std::printf(
                "world %ux%u, %zu islands, %zu players, all played by the computer (%zu human ones handed "
                "over); %zu game minutes",
                world->width(), world->height(), world->islands().size(), world->players().size(), handed_over,
                minutes);
            if (repeats > 1) {
                std::printf(", fastest of %zu runs per tick", repeats);
            }
            std::printf(
                "\n(%s in microseconds per tick of %u ms game time; 20 ticks per second at speed 1, %u at "
                "speed %u)\n",
                wall ? "wall-clock times" : "CPU times of the game thread", sim::Simulation::kTickMs,
                20 * simulation->rules().max_game_speed, simulation->rules().max_game_speed);
            std::printf("minute    mean    p50     p99     max  houses  inhabit.  prods ships soldiers walkers\n");
        }
        Period period;
        period.ticks.reserve(period_minutes * ticks_per_minute);
        std::size_t tick = 0;
        for (std::size_t minute = 1; minute <= minutes; ++minute) {
            for (std::uint64_t t = 0; t < ticks_per_minute; ++t, ++tick) {
                std::fill(step_ns.begin(), step_ns.end(), 0);
                const double start = now_us();
                simulation->tick_timed(step_ns);
                const double us = now_us() - start;
                // What the interface would take; left lying, the buffers only grow to their bound.
                (void)simulation->take_production_events();
                (void)simulation->take_construction_events();
                (void)simulation->take_military_events();
                (void)simulation->take_city_events();
                (void)simulation->take_ship_events();
                (void)simulation->take_diplomacy_notices();
                (void)simulation->take_command_results();
                best[tick] = run == 0 ? us : std::min(best[tick], us);
                if (last) {
                    period.ticks.push_back(best[tick]);
                    for (std::size_t i = 0; i < steps.size(); ++i) {
                        step_total_ns[i] += step_ns[i];
                        step_max_ns[i] = std::max(step_max_ns[i], step_ns[i]);
                    }
                }
            }
            if (last && (minute % period_minutes == 0 || minute == minutes)) {
                const double mean = period.mean();
                const double max = *std::max_element(period.ticks.begin(), period.ticks.end());
                const double p50 = period.quantile(0.5);
                const double p99 = period.quantile(0.99);
                std::printf("%6zu %7.1f %6.0f %7.0f %7.0f", minute, mean, p50, p99, max);
                print_world_line(*world, *simulation);
                std::fflush(stdout);
                period.ticks.clear();
            }
        }
    }

    double total_us = 0.0;
    double max_us = 0.0;
    std::size_t over_budget = 0;
    for (const double us : best) {
        total_us += us;
        max_us = std::max(max_us, us);
        over_budget += us > kBudgetUs ? 1U : 0U;
    }
    const double ticks = static_cast<double>(tick_count);
    std::printf("\nall ticks: mean %.1f us, max %.0f us; at speed %u one core is %.1f%% busy with the game\n",
                total_us / ticks, max_us, simulation->rules().max_game_speed,
                total_us / ticks * 20.0 * simulation->rules().max_game_speed / 10'000.0);
    std::printf("ticks over %.2f ms (the budget at speed 8): %zu (%.3f%%)\n", kBudgetUs / 1000.0, over_budget,
                100.0 * static_cast<double>(over_budget) / ticks);
    if (const auto load = load_average()) {
        std::printf("load average %.2f%s\n", *load,
                    wall ? " (wall-clock times count the time other programs had the core)" : "");
    }
    std::printf("step (%s)    share    mean      max\n", repeats > 1 ? "last run, wall clock" : "wall clock");
    std::uint64_t all_ns = 0;
    for (const std::uint64_t n : step_total_ns) {
        all_ns += n;
    }
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const double step_us = static_cast<double>(step_total_ns[i]) / 1000.0;
        std::printf("%-13.*s %5.1f%% %7.1f %8.0f\n", static_cast<int>(steps[i].size()), steps[i].data(),
                    all_ns > 0 ? 100.0 * static_cast<double>(step_total_ns[i]) / static_cast<double>(all_ns) : 0.0,
                    step_us / ticks, static_cast<double>(step_max_ns[i]) / 1000.0);
    }

    // The multiplayer fingerprint (net::Session, every hash_interval frames) and a save. The first
    // fingerprint hashes every block of tiles; the next one, 100 ticks (5 s of game time) later as in
    // a session, only the blocks that changed in between (TB-26).
    auto start = Clock::now();
    const auto first_hash = simulation->state_hash();
    const double first_hash_ms = ms_since(start);
    for (int t = 0; t < 100; ++t) {
        simulation->tick();
    }
    start = Clock::now();
    const auto hash = simulation->state_hash();
    const double hash_ms = ms_since(start);
    start = Clock::now();
    auto save = sim::write_game(*world, *buildings, &*simulation);
    const double save_ms = ms_since(start);
    if (!save) {
        return fail(save.error());
    }
    std::printf("state hash %016llx in %.1f ms, the first %.1f ms%s; savegame %zu bytes in %.1f ms\n",
                static_cast<unsigned long long>(hash), hash_ms, first_hash_ms,
                hash != 0 && first_hash != 0 ? "" : " (failed)", save->size(), save_ms);
    if (const auto memory = peak_memory_mb()) {
        std::printf("peak memory %.0f MB\n", *memory);
    }
    if (const auto it = args.options.find("--out"); it != args.options.end()) {
        if (auto status = core::write_file(fs::path(it->second), *save); !status) {
            return fail(status.error());
        }
        std::printf("-> %s\n", std::string(it->second).c_str());
    }
    return 0;
}

}  // namespace o1602::tool
