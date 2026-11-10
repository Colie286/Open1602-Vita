# SPDX-License-Identifier: GPL-3.0-or-later
# The game executable: open1602 (open1602.exe on Windows, Open1602.app on macOS).

add_executable(open1602
    src/main.cpp
    src/diagnostics.cpp
    src/game_folder_dialog.cpp
    src/game_sound.cpp
    src/hud.cpp
    src/multiplayer.cpp
    src/map_viewer.cpp
    src/map_input.cpp
    src/video_controller.cpp
    src/video_player.cpp
)
target_link_libraries(open1602 PRIVATE o1602::sim o1602::render_sdl o1602::platform o1602::audio o1602::ui o1602::video o1602::net o1602::menus o1602::script)
o1602_set_warnings(open1602)
o1602_set_output_dir(open1602)

if(WIN32)
    set_target_properties(open1602 PROPERTIES WIN32_EXECUTABLE ON)
    set(O1602_ICON_FILE "${PROJECT_SOURCE_DIR}/data/icons/open1602.ico")
    configure_file(open1602.rc.in "${CMAKE_CURRENT_BINARY_DIR}/open1602.rc" @ONLY)
    target_sources(open1602 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/open1602.rc")
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/open1602.rc" APPEND PROPERTY OBJECT_DEPENDS "${O1602_ICON_FILE}")
    # Windows finds DLLs next to the program: SDL3.dll goes there after every build and install.
    set(runtime_dlls $<TARGET_RUNTIME_DLLS:open1602>)
    add_custom_command(TARGET open1602 POST_BUILD
        COMMAND "$<IF:$<BOOL:${runtime_dlls}>,${CMAKE_COMMAND};-E;copy_if_different;${runtime_dlls};$<TARGET_FILE_DIR:open1602>,${CMAKE_COMMAND};-E;true>"
        COMMAND_EXPAND_LISTS
        VERBATIM)
    install(FILES ${runtime_dlls} DESTINATION bin)
endif()
o1602_utf8_manifest(open1602)

# On macOS the game is an app bundle, so that Finder and the dock show its icon and it starts by a
# double click. From a terminal it is started as Open1602.app/Contents/MacOS/Open1602.
if(APPLE)
    set(icon_file "${PROJECT_SOURCE_DIR}/data/icons/open1602.icns")
    target_sources(open1602 PRIVATE "${icon_file}")
    set_source_files_properties("${icon_file}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(open1602 PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        OUTPUT_NAME Open1602
        MACOSX_BUNDLE_BUNDLE_NAME Open1602
        MACOSX_BUNDLE_GUI_IDENTIFIER org.open1602.Open1602
        MACOSX_BUNDLE_ICON_FILE open1602.icns
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "GPL-3.0-or-later")
endif()

install(TARGETS open1602 RUNTIME DESTINATION bin BUNDLE DESTINATION .)
# Linux desktops find the game in their menus through the desktop entry and the hicolor icon theme.
if(UNIX AND NOT APPLE)
    set(app_id org.open1602.Open1602)
    install(FILES ${PROJECT_SOURCE_DIR}/data/linux/${app_id}.desktop DESTINATION share/applications)
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES ${PROJECT_SOURCE_DIR}/data/icons/png/open1602-${size}.png
            DESTINATION share/icons/hicolor/${size}x${size}/apps RENAME ${app_id}.png)
    endforeach()
    install(FILES ${PROJECT_SOURCE_DIR}/data/icons/open1602.svg
        DESTINATION share/icons/hicolor/scalable/apps RENAME ${app_id}.svg)
endif()
# The licences of the libraries built into the program (MIT asks for its notice to go along).
install(FILES ${PROJECT_SOURCE_DIR}/third_party/enet/LICENSE DESTINATION share/doc/open1602/licenses RENAME enet.txt)
install(FILES ${PROJECT_SOURCE_DIR}/third_party/lua/LICENSE DESTINATION share/doc/open1602/licenses RENAME lua.txt)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION share/doc/open1602)
