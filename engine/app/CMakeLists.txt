# SPDX-License-Identifier: GPL-3.0-or-later
# The game executable: open1602 (open1602.exe on Windows, Open1602.app on macOS).

add_executable(open1602
    src/main.cpp
    src/diagnostics.cpp
    src/editor_mode.cpp
    src/editor_goals.cpp
    src/editor_markers.cpp
    src/editor_package.cpp
    src/editor_objects.cpp
    src/editor_players.cpp
    src/editor_rules.cpp
    src/game_folder_dialog.cpp
    src/game_sound.cpp
    src/hud.cpp
    src/multiplayer.cpp
    src/map_viewer.cpp
    src/map_input.cpp
    src/video_controller.cpp
    src/video_player.cpp
)
target_link_libraries(open1602 PRIVATE o1602::sim o1602::render_sdl o1602::platform o1602::audio o1602::ui o1602::video o1602::net o1602::menus o1602::script o1602::editor)
o1602_set_warnings(open1602)
target_compile_definitions(open1602 PRIVATE O1602_VERSION="${PROJECT_VERSION}")  # the log's first line
o1602_set_output_dir(open1602)
o1602_utf8_manifest(open1602)
