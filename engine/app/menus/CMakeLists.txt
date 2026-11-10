# SPDX-License-Identifier: GPL-3.0-or-later
# The game's parts that need no platform: the main menu's lists, the start dialog of the endless
# game, the multiplayer menu and its texts, the end screen, the settings files, the build tool, the
# fertility view, the command sink, the questions before orders, the input of ship and city names, the status line over
# the map, which video plays when, what the player is told of the game's events, the diplomacy
# window's strip and open offers, the window "Warenbilanz", the gestures on the map, and the
# windows of what the player selects (buildings, ships, units, lists, diplomacy, options), the build
# menu, the player status of the side panel and the texts over the production buildings. Built
# without the game too, so that the tests have them. Also the command line of the program.

add_library(o1602_menus
    ../src/app_settings.cpp
    ../src/build_tool.cpp
    ../src/catchment_view.cpp
    ../src/chat_window.cpp
    ../src/command_line.cpp
    ../src/command_sink.cpp
    ../src/diplomacy_panel.cpp
    ../src/diplomacy_strip.cpp
    ../src/display_menu.cpp
    ../src/display_settings.cpp
    ../src/end_screen.cpp
    ../src/fertility_view.cpp
    ../src/endless_setup.cpp
    ../src/event_presenter.cpp
    ../src/game_folder.cpp
    ../src/game_settings_menu.cpp
    ../src/goods_tabs.cpp
    ../src/goods_window.cpp
    ../src/high_score_file.cpp
    ../src/key_help.cpp
    ../src/list_panels.cpp
    ../src/main_menu.cpp
    ../src/map_gestures.cpp
    ../src/message_table.cpp
    ../src/mods_menu.cpp
    ../src/multiplayer_menu.cpp
    ../src/multiplayer_texts.cpp
    ../src/name_edit.cpp
    ../src/names.cpp
    ../src/options_panel.cpp
    ../src/order_questions.cpp
    ../src/panel_common.cpp
    ../src/parchment.cpp
    ../src/production_utilisation.cpp
    ../src/selection_panels.cpp
    ../src/ship_panel.cpp
    ../src/side_panels.cpp
    ../src/stock_notes.cpp
    ../src/tile_status.cpp
    ../src/tutorial.cpp
    ../src/unit_panels.cpp
    ../src/video_queue.cpp
)
add_library(o1602::menus ALIAS o1602_menus)

target_include_directories(o1602_menus PUBLIC ../src)
target_link_libraries(o1602_menus PUBLIC o1602::assets o1602::audio o1602::net o1602::render o1602::sim o1602::ui)
o1602_set_warnings(o1602_menus)
