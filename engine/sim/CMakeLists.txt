# SPDX-License-Identifier: GPL-3.0-or-later
# Deterministic simulation. Must not depend on platform, render, audio or ui (ADR 0004, 0005).

add_library(o1602_sim
    src/ai/ai_build.cpp
    src/ai/ai_core.cpp
    src/ai/ai_logistics.cpp
    src/ai/ai_military.cpp
    src/ai/ai_planner.cpp
    src/ai/ai_settle.cpp
    src/ai/ai_sites.cpp
    src/ai/ai_world.cpp
    src/construction.cpp
    src/carts.cpp
    src/castles.cpp
    src/combat.cpp
    src/command_io.cpp
    src/diplomacy.cpp
    src/consumption.cpp
    src/disasters.cpp
    src/drag_path.cpp
    src/goods_ledger.cpp
    src/goods_statistics.cpp
    src/growth.cpp
    src/houses.cpp
    src/island_events.cpp
    src/island_map.cpp
    src/land_paths.cpp
    src/logistics.cpp
    src/military.cpp
    src/missions.cpp
    src/naval.cpp
    src/pathfinding.cpp
    src/pirates.cpp
    src/explore.cpp
    src/save/apply.cpp
    src/save/army.cpp
    src/save/cities.cpp
    src/save/islands.cpp
    src/save/map.cpp
    src/save/nature.cpp
    src/save/orders.cpp
    src/save/players.cpp
    src/save/sections.cpp
    src/save/ships.cpp
    src/save/world.cpp
    src/savegame_extension.cpp
    src/score.cpp
    src/victory.cpp
    src/save_game.cpp
    src/savegame_writer.cpp
    src/island_templates.cpp
    src/world_generator.cpp
    src/world_plan.cpp
    src/new_game.cpp
    src/sea_paths.cpp
    src/ship_orders.cpp
    src/ships.cpp
    src/trader.cpp
    src/validate.cpp
    src/shipyards.cpp
    src/player.cpp
    src/faction.cpp
    src/balance.cpp
    src/production_cycle.cpp
    src/simulation.cpp
    src/soldiers.cpp
    src/volcanoes.cpp
    src/world.cpp
)
add_library(o1602::sim ALIAS o1602_sim)

target_include_directories(o1602_sim PUBLIC include)
target_link_libraries(o1602_sim PUBLIC o1602::assets)
o1602_set_warnings(o1602_sim)

# The building values of rules such as the revised bread chain (data/balance, compiled in).
file(GLOB o1602_balance_presets CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/data/balance/*.toml")
o1602_embed_files(o1602_sim o1602::sim::presets ${o1602_balance_presets})
# The same results on every machine (ADR 0004): no fused multiply-add where the source has two
# operations, which compilers for arm64 contract by default (DS-2). MSVC contracts only with
# /fp:fast, which is not set.
if(NOT MSVC)
    target_compile_options(o1602_sim PRIVATE -ffp-contract=off)
endif()
