// SPDX-License-Identifier: GPL-3.0-or-later
#include <string>

#include "../test_framework.hpp"

#include "o1602/core/data_file.hpp"

using namespace o1602;

TEST_CASE("data file: a table's keys set in place, comments, order and other keys kept") {
    const std::string file =
        "# Scenarios of the package.\n"
        "[north]\n"
        "file = \"maps/north.szs\"  # the old map\n"
        "# the names\n"
        "name.en = \"North\"\n"
        "goal.castle = true\n"
        "\n"
        "[south]\n"
        "file = \"maps/south.szs\"\n";
    auto out = core::set_data_table(file, "north",
                                    {{"file", core::data_string("maps/north.o1602")},
                                     {"stars", "3"},
                                     {"name.en", core::data_string("The \"North\"")}},
                                    {"name.de"});
    REQUIRE(out.ok());
    CHECK(*out ==
          "# Scenarios of the package.\n"
          "[north]\n"
          "file = \"maps/north.o1602\"\n"
          "stars = 3\n"
          "name.en = \"The \\\"North\\\"\"\n"
          "# the names\n"
          "goal.castle = true\n"
          "\n"
          "[south]\n"
          "file = \"maps/south.szs\"\n");
    auto tables = core::parse_data_file(*out);
    REQUIRE(tables.ok());
    REQUIRE(!tables->empty());
    const core::DataValue* name = tables->front().find("name.en");
    REQUIRE(name != nullptr);
    CHECK(*name->string() == "The \"North\"");

    // A table the file has not goes at its end; a file without a last line ending gets one.
    auto added = core::set_data_table("[a]\nx = 1", "harbour", {{"file", core::data_string("maps/h.o1602")}});
    REQUIRE(added.ok());
    CHECK(*added == "[a]\nx = 1\n\n[harbour]\nfile = \"maps/h.o1602\"\n");
    auto first = core::set_data_table("", "harbour", {{"stars", "0"}});
    REQUIRE(first.ok());
    CHECK(*first == "[harbour]\nstars = 0\n");
    // Line endings as the file has them; a table without the keys gets them after its last entry.
    auto crlf = core::set_data_table("[a]\r\nx = 1\r\n\r\n[b]\r\n", "a", {{"y", "2"}});
    REQUIRE(crlf.ok());
    CHECK(*crlf == "[a]\r\nx = 1\r\ny = 2\r\n\r\n[b]\r\n");
}

TEST_CASE("data file: a broken file is not written over") {
    CHECK(!core::set_data_table("[a\nx = 1\n", "a", {{"y", "1"}}).ok());
    CHECK(core::data_string("a\tb\x01") == "\"a\\tb\\u0001\"");
}
