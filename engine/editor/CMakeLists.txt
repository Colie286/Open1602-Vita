# SPDX-License-Identifier: GPL-3.0-or-later
# The editor's logic (ADR 0013): the document it edits, its operations with undo and redo, and the
# checks of a scenario. No platform dependency, so tests and o1602-tool use it headless.

add_library(o1602_editor
    src/document.cpp
    src/history.cpp
    src/operations.cpp
    src/original_check.cpp
)
add_library(o1602::editor ALIAS o1602_editor)

target_include_directories(o1602_editor PUBLIC include)
target_link_libraries(o1602_editor PUBLIC o1602::sim o1602::assets o1602::core)
o1602_set_warnings(o1602_editor)
