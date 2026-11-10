# SPDX-License-Identifier: GPL-3.0-or-later
# The interface without platform dependency: fonts and text, gadget layouts at work, regions, and
# Open1602's own texts in the language of the installation (data/locale, compiled in).

add_library(o1602_ui
    src/build_menu.cpp
    src/font.cpp
    src/form.cpp
    src/key_bindings.cpp
    src/localization.cpp
    src/package_icons.cpp
    src/panel.cpp
    src/text_format.cpp
    src/unit_selection.cpp
)
add_library(o1602::ui ALIAS o1602_ui)

target_include_directories(o1602_ui PUBLIC include)
target_link_libraries(o1602_ui PUBLIC o1602::assets o1602::core o1602::sim)
o1602_set_warnings(o1602_ui)

file(GLOB o1602_locales CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/data/locale/*.lang")
o1602_embed_files(o1602_ui o1602::ui::locales ${o1602_locales})
