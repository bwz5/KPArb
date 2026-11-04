#include "common/EnvironmentFile.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

TEST(EnvironmentFileTest, ParsesPlainQuotedAndExportedValues)
{
    const auto entries = parseEnvironmentFileContents(
        "# heading\n"
        "PLAIN=value\n"
        "export EXPORTED=yes\n"
        "QUOTED=\"line one\\nline two\"\n"
        "SINGLE='keep \\n literal'\n"
        "\n"
        "EMPTY=\n");
    ASSERT_EQ(entries.size(), 5U);
    EXPECT_EQ(entries[0].first, "PLAIN");
    EXPECT_EQ(entries[0].second, "value");
    EXPECT_EQ(entries[1].first, "EXPORTED");
    EXPECT_EQ(entries[2].second, "line one\nline two");
    EXPECT_EQ(entries[3].second, "keep \\n literal");
    EXPECT_EQ(entries[4].second, "");
}

TEST(EnvironmentFileTest, IgnoresLinesWithoutSeparator)
{
    const auto entries = parseEnvironmentFileContents("NOT A PAIR\nKEY=1\n");
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].first, "KEY");
}

}
}
