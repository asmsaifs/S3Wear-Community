// Sample host test: proves the GoogleTest harness builds and runs.
// Real tests for pure-logic components follow the same pattern.
#include <gtest/gtest.h>

#include <array>
#include <numeric>

TEST(Sample, ArithmeticWorks)
{
    EXPECT_EQ(2 + 2, 4);
}

TEST(Sample, StdLibraryAvailable)
{
    constexpr std::array<int, 4> values{1, 2, 3, 4};
    EXPECT_EQ(std::accumulate(values.begin(), values.end(), 0), 10);
}
