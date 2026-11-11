# SPDX-License-Identifier: GPL-3.0-or-later
# Loaders for the original Anno 1602 data formats. No platform or rendering dependencies.

add_library(o1602_assets
    src/building_table.cpp
    src/chunk_file.cpp
    src/chunk_writer.cpp
    src/cod.cpp
    src/cod_parser.cpp
    src/cod_predefined.cpp
    src/cursor_resource.cpp
    src/data_fixes.cpp
    src/game_content.cpp
    src/goods.cpp
    src/original_names.cpp
    src/own_buildings.cpp
    src/package_scenarios.cpp
    src/package_graphics.cpp
    src/package_sounds.cpp
    src/resident_classes.cpp
    src/building_looks.cpp
    src/package_arms.cpp
    src/package_flags.cpp
    src/package_interface.cpp
    src/figure_looks.cpp
    src/own_figures.cpp
    src/figure_table.cpp
    src/gad.cpp
    src/high_scores.cpp
    src/history_texts.cpp
    src/game_directory.cpp
    src/install_audit.cpp
    src/island.cpp
    src/localized_names.cpp
    src/palette.cpp
    src/rda_archive.cpp
    src/savegame.cpp
    src/scenario.cpp
    src/sprite_sheet.cpp
    src/text_table.cpp
    src/unlock_levels.cpp
)
add_library(o1602::assets ALIAS o1602_assets)

target_include_directories(o1602_assets PUBLIC include)
target_link_libraries(o1602_assets PUBLIC o1602::core)
if(WIN32)
    target_link_libraries(o1602_assets PRIVATE advapi32)  # GOG's registry entries (game_directory.cpp)
endif()
o1602_set_warnings(o1602_assets)
