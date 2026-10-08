// watchfaces: FACE_CONFIG string codec (wf_cfg.h): parse, malformed input, writer
// limits and round trip.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "wf_cfg.h"

namespace {

struct Item {
    std::string face, key, value;
    bool operator==(const Item &o) const { return face == o.face && key == o.key && value == o.value; }
};

struct Parsed {
    std::vector<Item> items;
    int bad = 0;
};

Parsed parse(const char *s, const char *reject_key = nullptr)
{
    Parsed p;
    struct Ctx {
        Parsed *p;
        const char *reject;
    } ctx{&p, reject_key};
    p.bad = wf_cfg_parse(
        s,
        [](const char *f, const char *k, const char *v, void *c) {
            auto *x = static_cast<Ctx *>(c);
            if (x->reject && std::string(k) == x->reject) {
                return false;
            }
            x->p->items.push_back({f, k, v});
            return true;
        },
        &ctx);
    return p;
}

TEST(WfCfg, ParsesEntriesAndItems)
{
    const Parsed p = parse("analog:left=moon,color=ff9f0a;s3w.neon:ring=battery");
    EXPECT_EQ(p.bad, 0);
    ASSERT_EQ(p.items.size(), 3u);
    EXPECT_EQ(p.items[0], (Item{"analog", "left", "moon"}));
    EXPECT_EQ(p.items[1], (Item{"analog", "color", "ff9f0a"}));
    EXPECT_EQ(p.items[2], (Item{"s3w.neon", "ring", "battery"}));
}

TEST(WfCfg, EmptyAndNull)
{
    EXPECT_EQ(parse("").items.size(), 0u);
    EXPECT_EQ(parse(nullptr).items.size(), 0u);
    const Parsed p = parse(";;a:b=c,,;");
    EXPECT_EQ(p.bad, 0);
    ASSERT_EQ(p.items.size(), 1u);
    EXPECT_EQ(p.items[0], (Item{"a", "b", "c"}));
}

TEST(WfCfg, MalformedPartsAreSkipped)
{
    struct Case {
        const char *in;
        size_t items;
        int bad;
    } cases[] = {
        {"analog", 0, 1},                      // no ':'
        {"Analog:left=moon", 0, 1},            // upper case
        {":left=moon;minimal:bottom=moon", 1, 1},
        {"analog:left", 0, 1},                 // no '='
        {"analog:left=", 0, 1},                // empty value
        {"analog:=moon,right=steps", 1, 1},
        {"analog:left=moon=x,right=steps", 1, 1},
        {"analog:left=mo on,right=steps", 1, 1},
        {"analog:left=moon;bad face:x=y;minimal:bottom=moon", 2, 1},
        {"a:b=c\n", 0, 1},
        {"a:b=c,d=\xC3\xA9;e:f=g", 2, 1},
    };
    for (const Case &c : cases) {
        const Parsed p = parse(c.in);
        EXPECT_EQ(p.items.size(), c.items) << c.in;
        EXPECT_EQ(p.bad, c.bad) << c.in;
    }
}

TEST(WfCfg, TokenLength)
{
    const std::string ok(47, 'a');
    const std::string too_long(48, 'a');
    EXPECT_EQ(parse((ok + ":k=v").c_str()).items.size(), 1u);
    const Parsed p = parse((too_long + ":k=v;x:y=z").c_str());
    EXPECT_EQ(p.bad, 1);
    ASSERT_EQ(p.items.size(), 1u);
    EXPECT_EQ(p.items[0].face, "x");
}

TEST(WfCfg, RejectedItemsCount)
{
    const Parsed p = parse("a:x=1,color=000000,y=2", "color");
    EXPECT_EQ(p.bad, 1);
    EXPECT_EQ(p.items.size(), 2u);
}

TEST(WfCfg, WriterGroupsByFace)
{
    char buf[128];
    wf_cfg_writer_t w;
    wf_cfg_writer_init(&w, buf, sizeof buf);
    EXPECT_STREQ(buf, "");
    EXPECT_TRUE(wf_cfg_put(&w, "analog", "left", "moon"));
    EXPECT_TRUE(wf_cfg_put(&w, "analog", "color", "ff9f0a"));
    EXPECT_TRUE(wf_cfg_put(&w, "minimal", "bottom", "weather"));
    EXPECT_STREQ(buf, "analog:left=moon,color=ff9f0a;minimal:bottom=weather");
    EXPECT_EQ(w.len, strlen(buf));
    EXPECT_EQ(w.need, w.len);
}

TEST(WfCfg, WriterRejectsBadTokens)
{
    char buf[64];
    wf_cfg_writer_t w;
    wf_cfg_writer_init(&w, buf, sizeof buf);
    EXPECT_FALSE(wf_cfg_put(&w, "a;b", "k", "v"));
    EXPECT_FALSE(wf_cfg_put(&w, "a", "", "v"));
    EXPECT_FALSE(wf_cfg_put(&w, "a", "k", nullptr));
    EXPECT_FALSE(wf_cfg_put(&w, std::string(48, 'a').c_str(), "k", "v"));
    EXPECT_STREQ(buf, "");
    EXPECT_EQ(w.need, 0u);
}

TEST(WfCfg, WriterOverflowKeepsCompleteItems)
{
    char buf[20];
    wf_cfg_writer_t w;
    wf_cfg_writer_init(&w, buf, sizeof buf);
    EXPECT_TRUE(wf_cfg_put(&w, "analog", "left", "moon")); // 16 chars
    EXPECT_FALSE(wf_cfg_put(&w, "analog", "right", "steps"));
    EXPECT_FALSE(wf_cfg_put(&w, "a", "b", "c")); // would fit, but an item was dropped before
    EXPECT_STREQ(buf, "analog:left=moon");
    EXPECT_EQ(w.need, strlen("analog:left=moon,right=steps;a:b=c"));

    // Exactly full: the NUL needs the last byte.
    char exact[17];
    wf_cfg_writer_init(&w, exact, sizeof exact);
    EXPECT_TRUE(wf_cfg_put(&w, "analog", "left", "moon"));
    char tight[16];
    wf_cfg_writer_init(&w, tight, sizeof tight);
    EXPECT_FALSE(wf_cfg_put(&w, "analog", "left", "moon"));
    EXPECT_STREQ(tight, "");
}

TEST(WfCfg, RoundTrip)
{
    char buf[256];
    wf_cfg_writer_t w;
    wf_cfg_writer_init(&w, buf, sizeof buf);
    const std::vector<Item> in = {
        {"digital", "bottom", "moon"},
        {"modular", "left", "heart_rate"},
        {"modular", "color", "40c8e0"},
        {"s3w.dial", "sub-1", "none"},
    };
    for (const Item &i : in) {
        ASSERT_TRUE(wf_cfg_put(&w, i.face.c_str(), i.key.c_str(), i.value.c_str()));
    }
    const Parsed p = parse(buf);
    EXPECT_EQ(p.bad, 0);
    EXPECT_EQ(p.items, in);
}

TEST(WfCfg, Colors)
{
    char hex[7];
    wf_cfg_color_str(0xFF9F0A, hex);
    EXPECT_STREQ(hex, "ff9f0a");
    wf_cfg_color_str(0x00000F, hex);
    EXPECT_STREQ(hex, "00000f");
    uint32_t rgb = 0;
    EXPECT_TRUE(wf_cfg_color_parse("40c8e0", &rgb));
    EXPECT_EQ(rgb, 0x40C8E0u);
    EXPECT_TRUE(wf_cfg_color_parse("000000", &rgb));
    EXPECT_EQ(rgb, 0u);
    for (const char *bad : {"", "40c8e", "40c8e00", "40C8E0", "#40c8e", "zzzzzz", " 40c8e"}) {
        EXPECT_FALSE(wf_cfg_color_parse(bad, &rgb)) << bad;
    }
    EXPECT_FALSE(wf_cfg_color_parse(nullptr, &rgb));
}

} // namespace
