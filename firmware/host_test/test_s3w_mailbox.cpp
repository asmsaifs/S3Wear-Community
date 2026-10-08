// UI mailbox ordering (components/sys_core/s3w_mailbox.c).
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "s3w_mailbox.h"

namespace {

std::vector<std::string> g_log;

void record(void *ctx, int32_t arg, const void *data, size_t len)
{
    const char *tag = static_cast<const char *>(ctx);
    g_log.push_back(std::string(tag) + ":" + std::to_string(arg) + ":" +
                    std::string(static_cast<const char *>(data), len));
}

void drain(s3w_mbox_t *m)
{
    s3w_mbox_msg_t msg;
    while (s3w_mbox_pop(m, &msg)) {
        msg.fn(msg.ctx, msg.arg, msg.data, msg.len);
    }
}

class Mailbox : public ::testing::Test {
protected:
    void SetUp() override
    {
        g_log.clear();
        s3w_mbox_init(&m, slots, 4);
    }
    s3w_mbox_msg_t slots[4];
    s3w_mbox_t m;
    char a[2] = "a";
    char b[2] = "b";
};

} // namespace

TEST_F(Mailbox, DeliversInPostOrder)
{
    ASSERT_TRUE(s3w_mbox_push(&m, record, a, 1, "x", 1));
    ASSERT_TRUE(s3w_mbox_push(&m, record, b, 2, "y", 1));
    ASSERT_TRUE(s3w_mbox_push(&m, record, a, 3, "z", 1));
    drain(&m);
    EXPECT_EQ(g_log, (std::vector<std::string>{"a:1:x", "b:2:y", "a:3:z"}));
}

TEST_F(Mailbox, OrderSurvivesWrapAround)
{
    for (int round = 0; round < 10; round++) {
        ASSERT_TRUE(s3w_mbox_push(&m, record, a, round * 2, "p", 1));
        ASSERT_TRUE(s3w_mbox_push(&m, record, a, round * 2 + 1, "q", 1));
        s3w_mbox_msg_t msg;
        ASSERT_TRUE(s3w_mbox_pop(&m, &msg)); // consume one per round: head walks the ring
        msg.fn(msg.ctx, msg.arg, msg.data, msg.len);
        if (s3w_mbox_count(&m) >= 3) { // keep room for the next two pushes
            drain(&m);
        }
    }
    drain(&m);
    ASSERT_EQ(g_log.size(), 20u);
    for (int i = 0; i < 20; i++) {
        EXPECT_EQ(g_log[i], std::string("a:") + std::to_string(i) + (i % 2 ? ":q" : ":p"));
    }
}

TEST_F(Mailbox, PayloadIsCopiedAtPost)
{
    char buf[8] = "before";
    ASSERT_TRUE(s3w_mbox_push(&m, record, a, 0, buf, std::strlen(buf)));
    std::strcpy(buf, "after!");
    drain(&m);
    EXPECT_EQ(g_log, (std::vector<std::string>{"a:0:before"}));
}

TEST_F(Mailbox, FullRejectsAndCountsWithoutReordering)
{
    for (int i = 0; i < 4; i++) {
        ASSERT_TRUE(s3w_mbox_push(&m, record, a, i, "", 0));
    }
    EXPECT_FALSE(s3w_mbox_push(&m, record, a, 99, "", 0));
    EXPECT_EQ(m.dropped, 1u);
    EXPECT_EQ(m.high_water, 4);
    drain(&m);
    EXPECT_EQ(g_log, (std::vector<std::string>{"a:0:", "a:1:", "a:2:", "a:3:"}));
}

TEST_F(Mailbox, RejectsBadArguments)
{
    std::vector<char> big(S3W_MBOX_PAYLOAD_MAX + 1, 'x');
    EXPECT_FALSE(s3w_mbox_push(&m, record, a, 0, big.data(), big.size()));
    EXPECT_FALSE(s3w_mbox_push(&m, nullptr, a, 0, "x", 1));
    EXPECT_FALSE(s3w_mbox_push(&m, record, a, 0, nullptr, 3));
    EXPECT_EQ(m.dropped, 3u);
    EXPECT_TRUE(s3w_mbox_push(&m, record, a, 0, big.data(), S3W_MBOX_PAYLOAD_MAX));
    s3w_mbox_msg_t msg;
    ASSERT_TRUE(s3w_mbox_pop(&m, &msg));
    EXPECT_EQ(msg.len, S3W_MBOX_PAYLOAD_MAX);
    EXPECT_FALSE(s3w_mbox_pop(&m, &msg));
}
