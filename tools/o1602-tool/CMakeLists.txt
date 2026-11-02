# SPDX-License-Identifier: GPL-3.0-or-later
# o1602-tool: command line utility to inspect and convert original game data.

add_executable(o1602-tool main.cpp bench.cpp formats.cpp mods.cpp render.cpp savegames.cpp scenario.cpp templates.cpp
               video.cpp)
target_link_libraries(o1602-tool PRIVATE o1602::assets o1602::sim o1602::render o1602::video o1602::editor o1602::menus
                                         o1602::script)
o1602_set_warnings(o1602-tool)
o1602_utf8_manifest(o1602-tool)
o1602_set_output_dir(o1602-tool)
