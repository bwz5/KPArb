#include "TestConstants.h"
#include "common/EnvironmentFile.h"

#include <gtest/gtest.h>

namespace kparb {
namespace {

using namespace test_constants::environment_test;

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
    ASSERT_EQ(entries.size(), parsedEntryCount);
    EXPECT_EQ(entries[plainEntry].first, "PLAIN");
    EXPECT_EQ(entries[plainEntry].second, "value");
    EXPECT_EQ(entries[exportedEntry].first, "EXPORTED");
    EXPECT_EQ(entries[quotedEntry].second, "line one\nline two");
    EXPECT_EQ(entries[singleQuotedEntry].second, "keep \\n literal");
    EXPECT_EQ(entries[emptyEntry].second, "");
}

TEST(EnvironmentFileTest, IgnoresLinesWithoutSeparator)
{
    const auto entries = parseEnvironmentFileContents("NOT A PAIR\nKEY=1\n");
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].first, "KEY");
}

}
}
