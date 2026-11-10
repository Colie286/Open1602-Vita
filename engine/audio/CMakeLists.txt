# SPDX-License-Identifier: GPL-3.0-or-later
# Sound: WAVE, Ogg Vorbis and FLAC decoding, the mixer and the game's sounds. No platform dependency (the device is
# platform::AudioOutput), so tests run it headless.

add_library(o1602_audio
    src/game_audio.cpp
    src/mixer.cpp
    src/sound_bank.cpp
    src/package_music.cpp
    src/sound_file.cpp
    src/wav.cpp
    src/prefetch.cpp
)
add_library(o1602::audio ALIAS o1602_audio)

target_include_directories(o1602_audio PUBLIC include)
find_package(Threads REQUIRED)  # music tracks are decoded beside the game (game_audio.cpp)
target_link_libraries(o1602_audio PUBLIC o1602::sim o1602::assets o1602::core PRIVATE Threads::Threads o1602_stb_vorbis o1602_dr_flac)
o1602_set_warnings(o1602_audio)
