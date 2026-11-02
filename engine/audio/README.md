# engine/audio

Sound without platform dependencies (replaces `Maxsound.dll`): reading WAVE files, including the 4-bit music
(`wav.hpp`), software mixer (`mixer.hpp`), files of the game installation (`sound_bank.hpp`) and the
game's sounds (`game_audio.hpp`: sounds on screen, voice messages, music). The device is
`platform::AudioOutput`; the viewer connects the two in `engine/app/src/game_sound.*`.

Description: [docs/formats/wav.md](../../docs/formats/wav.md).
Videos (Smacker) are decoded by `engine/video`.
