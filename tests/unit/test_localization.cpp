// SPDX-License-Identifier: GPL-3.0-or-later
// Open1602's own texts in the language of the installation (ui/localization.hpp,
// docs/modding/languages.md).

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/ui/localization.hpp"

using namespace o1602;

namespace {

/// A folder of its own under the system's temporary folder, removed again at the end.
struct TempFolder {
    std::filesystem::path path;
    explicit TempFolder(std::string_view name) : path(test::temp_path(std::string(name))) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    void write(std::string_view name, std::string_view text) const {
        std::filesystem::create_directories((path / name).parent_path());
        std::ofstream out(path / name, std::ios::binary);
        out << text;
    }
};

/// Puts English back for the tests that follow, whatever a test chose.
struct LanguageGuard {
    LanguageGuard() = default;
    ~LanguageGuard() { ui::set_language(ui::kFallbackLanguage); }
    LanguageGuard(const LanguageGuard&) = delete;
    LanguageGuard& operator=(const LanguageGuard&) = delete;
};

}  // namespace

TEST_CASE("localization: a catalogue keeps its texts in Windows-1252 with line breaks") {
    const ui::Catalogue c = ui::Catalogue::parse(
        "\xEF\xBB\xBF# a comment\n"
        "\n"
        "dialog.yes = Ja\r\n"
        "dialog.quit = Spiel beenden?\\nDer Stand wird als \xE2\x80\x9Elastgame\xE2\x80\x9C gesichert.\n"
        "no equals sign\n"
        "  spaced.key   =   Gr\xC3\xB6\xC3\x9F"
        "e  \n");
    CHECK(c.size() == 3);
    REQUIRE(c.find("dialog.yes") != nullptr);
    CHECK(*c.find("dialog.yes") == "Ja");
    CHECK(*c.find("dialog.quit") == "Spiel beenden?\nDer Stand wird als \x84lastgame\x93 gesichert.");
    CHECK(*c.find("spaced.key") ==
          "Gr\xF6\xDF"
          "e");
    CHECK(c.find("missing") == nullptr);
}

TEST_CASE("localization: English and German are built in, and German has no key English lacks") {
    const auto codes = ui::builtin_languages();
    CHECK(std::find(codes.begin(), codes.end(), "en") != codes.end());
    CHECK(std::find(codes.begin(), codes.end(), "de") != codes.end());
    const ui::Catalogue en = ui::builtin_catalogue("en");
    CHECK(en.size() > 50);
    for (const std::string& code : codes) {
        const ui::Catalogue c = ui::builtin_catalogue(code);
        CHECK(c.find("language.name") != nullptr);
        CHECK(c.find("language.detect") != nullptr);
        for (const std::string& key : c.keys()) {
            CHECK(en.find(key) != nullptr);  // English is the fallback for every key
        }
    }
    CHECK(ui::builtin_catalogue("xx").size() == 0);
}

TEST_CASE("localization: German translates every English key but the language-neutral ones") {
    // Key names and digits read the same in both languages, so German leaves them to English.
    constexpr std::string_view kNeutral[] = {
        "keys.group_digits",     "keys.modifier.alt", "keys.name.escape", "keys.name.keypad_minus",
        "keys.name.keypad_plus", "keys.name.pause",   "keys.name.tab",
    };
    const ui::Catalogue en = ui::builtin_catalogue("en");
    const ui::Catalogue de = ui::builtin_catalogue("de");
    for (const std::string& key : en.keys()) {
        const bool neutral = std::find(std::begin(kNeutral), std::end(kNeutral), key) != std::end(kNeutral);
        if (de.find(key) == nullptr && !neutral) {
            CHECK(key == "a key de.lang translates");
        }
    }
    for (const std::string_view key : kNeutral) {
        CHECK(en.find(key) != nullptr);  // the list follows en.lang
    }
}

TEST_CASE("localization: the language of an installation comes from Text.cod's player colours") {
    const auto german = assets::TextTable::parse(
        "[PLAYCOL]\nRoter Spieler\nBlauer Spieler\nGelber Spieler\nWei\xDF"
        "er Spieler\n[END]\n");
    CHECK(ui::detect_language(german) == "de");
    const auto english = assets::TextTable::parse("[PLAYCOL]\nRed Player\nBlue Player\n[END]\n");
    CHECK(ui::detect_language(english) == "en");
    const auto unknown = assets::TextTable::parse("[PLAYCOL]\nJoueur rouge\n[END]\n");
    CHECK(ui::detect_language(unknown).empty());
    CHECK(ui::detect_language(assets::TextTable{}).empty());
}

TEST_CASE("localization: GOG's info file names the language when Text.cod does not") {
    const TempFolder folder("o1602-test-gog-language");
    for (const char* file : {"haeuser.cod", "figuren.cod"}) {
        folder.write(file, "");
    }
    std::filesystem::create_directories(folder.path / "toolgfx");
    std::filesystem::create_directories(folder.path / "gfx");
    folder.write("toolgfx/stadtfld.col", "");
    folder.write("gfx/stadtfld.bsh", "");
    auto game = assets::GameDirectory::open(folder.path);
    REQUIRE(game);
    CHECK(ui::gog_language(*game).empty());
    CHECK(ui::installation_language(*game, nullptr) == "en");  // nothing tells: English

    folder.write("goggame-1438168222.info",
                 "{\n  \"language\": \"German\",\n  \"languages\": [\n    \"de-DE\"\n  ]\n}\n");
    CHECK(ui::gog_language(*game) == "de");
    CHECK(ui::installation_language(*game, nullptr) == "de");
    const auto english = assets::TextTable::parse("[PLAYCOL]\nRed Player\n[END]\n");
    CHECK(ui::installation_language(*game, &english) == "en");  // Text.cod comes first
}

TEST_CASE("localization: tr answers in the chosen language, else in English, else with the key") {
    const LanguageGuard guard;
    CHECK(ui::tr("dialog.yes") == "Yes");  // English before a language is chosen

    ui::set_language("de");
    CHECK(ui::current_language() == "de");
    CHECK(ui::tr("dialog.yes") == "Ja");
    CHECK(ui::tr("keys.action.white_flag") ==
          "Wei\xDF"
          "e Flagge");
    CHECK(ui::tr("keys.group_digits") == "1 .. 0");  // only in English
    CHECK(ui::tr("no.such.key") == "no.such.key");
    CHECK(ui::tr("dialog.break_peace", {"Rot"}) == "Friedensvertrag mit Rot brechen?\nDas bedeutet Krieg.");

    ui::set_language("FR");  // no catalogue: English
    CHECK(ui::current_language() == "fr");
    CHECK(ui::tr("dialog.yes") == "Yes");
}

TEST_CASE("localization: arguments go where the translation puts them") {
    const LanguageGuard guard;
    const TempFolder folder("o1602-test-locale");
    folder.write("xx.lang", "test.order = {2} before {1}, {3} missing, {x} and {0} stay\n");
    ui::set_language("xx", folder.path);
    CHECK(ui::tr("test.order", {"one", "two"}) == "two before one,  missing, {x} and {0} stay");
}

TEST_CASE("localization: a catalogue in the user's folder extends and overrides the built-in one") {
    const LanguageGuard guard;
    const TempFolder folder("o1602-test-locale-user");
    folder.write("de.lang", "dialog.yes = Jawohl\nmod.text = Neu\n");
    ui::set_language("de", folder.path);
    CHECK(ui::tr("dialog.yes") == "Jawohl");
    CHECK(ui::tr("dialog.no") == "Nein");
    CHECK(ui::tr("mod.text") == "Neu");
}

TEST_CASE("localization: the packages' catalogues come between the built-in one and the user's") {
    const LanguageGuard guard;
    const TempFolder folder("o1602-test-locale-packages");
    folder.write("first/locale/fr.lang",
                 "language.name = Fran\xC3\xA7"
                 "ais\ndialog.yes = Oui\ndialog.no = Non\n");
    folder.write("second/locale/fr.lang", "dialog.no = Non merci\n");
    folder.write("user/fr.lang", "dialog.yes = Oui !\n");
    const std::vector<std::filesystem::path> packages = {folder.path / "first", folder.path / "second"};
    ui::set_language("fr", folder.path / "user", packages);
    CHECK(ui::current_language() == "fr");
    CHECK(ui::tr("language.name") ==
          "Fran\xE7"
          "ais");                                            // Windows-1252
    CHECK(ui::tr("dialog.no") == "Non merci");               // the later package wins
    CHECK(ui::tr("dialog.yes") == "Oui !");                  // and the player's own over both
    CHECK(ui::tr("dialog.quit_anno") == "Quit Anno 1602?");  // English for the rest
}

TEST_CASE("localization: check-mod names keys and arguments of a package's catalogue the game has not") {
    std::vector<std::string> problems;
    ui::check_catalogue(
        "# French\n"
        "dialog.yes = Oui\n"
        "dialog.yess = Oui\n"                                      // line 3: no such key
        "dialog.break_peace = Rompre la paix avec {1} et {2} ?\n"  // line 4: no {2}
        "nothing here\n",                                          // line 5
        "french/locale/fr.lang", problems);
    REQUIRE(problems.size() == 3);
    CHECK(problems[0] == "french/locale/fr.lang line 3: 'dialog.yess' is no text of Open1602; nothing shows it");
    CHECK(problems[1] ==
          "french/locale/fr.lang line 4: 'dialog.break_peace' uses {2}, which the game does not fill in there");
    CHECK(problems[2] == "french/locale/fr.lang line 5: is no \"key = text\" line and is passed over");
}
