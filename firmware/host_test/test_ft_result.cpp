// Factory test JSON (components/factory_test/ft_result.c).
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "ft_result.h"

static ft_result_t make(const char *name, ft_status_t st, const char *detail)
{
    ft_result_t r{};
    r.name = name;
    r.status = st;
    snprintf(r.detail, sizeof r.detail, "%s", detail);
    return r;
}

TEST(FtResult, OverallPassAllowsSkipOnly)
{
    std::vector<ft_result_t> r = {make("a", FT_PASS, ""), make("b", FT_SKIP, "")};
    EXPECT_TRUE(ft_overall_pass(r.data(), r.size()));
    r.push_back(make("c", FT_FAIL, ""));
    EXPECT_FALSE(ft_overall_pass(r.data(), r.size()));
    r.back().status = FT_PENDING;
    EXPECT_FALSE(ft_overall_pass(r.data(), r.size()));
}

TEST(FtResult, JsonParsesAndEscapes)
{
    const std::vector<ft_result_t> r = {
        make("i2c", FT_PASS, "6/6"),
        make("sd", FT_SKIP, "no \"card\"\n\\"),
    };
    char buf[512];
    const size_t n = ft_json_write(buf, sizeof buf, "v0.1-\"x\"", r.data(), r.size());
    ASSERT_LT(n, sizeof buf);
    const auto j = nlohmann::json::parse(buf);
    const auto &ft = j.at("factory_test");
    EXPECT_EQ(ft.at("fw"), "v0.1-\"x\"");
    EXPECT_EQ(ft.at("pass"), true);
    EXPECT_EQ(ft.at("skipped"), 1);
    ASSERT_EQ(ft.at("results").size(), 2u);
    EXPECT_EQ(ft.at("results")[0].at("status"), "PASS");
    EXPECT_EQ(ft.at("results")[1].at("detail"), "no \"card\"\n\\");
}

TEST(FtResult, TruncatesLikeSnprintf)
{
    const std::vector<ft_result_t> r = {make("imu", FT_FAIL, "self-test")};
    char full[256];
    const size_t need = ft_json_write(full, sizeof full, "v1", r.data(), r.size());
    char small[16];
    EXPECT_EQ(ft_json_write(small, sizeof small, "v1", r.data(), r.size()), need);
    EXPECT_EQ(std::string(small), std::string(full).substr(0, sizeof small - 1));
    EXPECT_EQ(ft_json_write(nullptr, 0, "v1", r.data(), r.size()), need);
}
