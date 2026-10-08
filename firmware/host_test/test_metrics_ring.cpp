#include <gtest/gtest.h>

#include <cstring>

#include "metrics_ring.h"

TEST(MetricsRing, StartsEmpty)
{
    metrics_t m;
    metrics_init(&m);
    EXPECT_EQ(m.count, 0);
    EXPECT_EQ(m.boot_count, 0u);
    EXPECT_EQ(metrics_at(&m, 0), nullptr);
}

TEST(MetricsRing, BootCountsResetReasonAndSessionHeap)
{
    metrics_t m;
    metrics_init(&m);
    metrics_note_boot(&m, 1); // power-on
    metrics_note_heap(&m, 90000, 40000);
    metrics_note_heap(&m, 80000, 45000);
    metrics_note_boot(&m, 4); // panic
    EXPECT_EQ(m.boot_count, 2u);
    EXPECT_EQ(m.reset_counts[1], 1u);
    EXPECT_EQ(m.reset_counts[4], 1u);
    const metric_entry_t *e = metrics_at(&m, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->kind, METRIC_BOOT);
    EXPECT_EQ(e->a, 4u);
    EXPECT_EQ(e->b, 80000u); // previous session's minimum
    EXPECT_EQ(m.min_free_heap, 80000u);
    EXPECT_EQ(m.min_free_internal, 40000u);
    EXPECT_EQ(m.session_min_free, UINT32_MAX);
}

TEST(MetricsRing, UnknownResetReasonCountsAsUnknown)
{
    metrics_t m;
    metrics_init(&m);
    metrics_note_boot(&m, 99);
    EXPECT_EQ(m.reset_counts[0], 1u);
}

TEST(MetricsRing, WrapsAndKeepsNewestInOrder)
{
    metrics_t m;
    metrics_init(&m);
    for (uint32_t i = 0; i < METRICS_RING_LEN + 5; i++) {
        metrics_push(&m, METRIC_DRAIN, i, i * 10);
    }
    ASSERT_EQ(m.count, METRICS_RING_LEN);
    EXPECT_EQ(metrics_at(&m, 0)->a, 5u); // 5 oldest were overwritten
    EXPECT_EQ(metrics_at(&m, 0)->seq, 6u);
    EXPECT_EQ(metrics_at(&m, METRICS_RING_LEN - 1)->a, METRICS_RING_LEN + 4u);
    EXPECT_EQ(metrics_at(&m, METRICS_RING_LEN), nullptr);
}

TEST(MetricsRing, InvalidKindIgnored)
{
    metrics_t m;
    metrics_init(&m);
    metrics_push(&m, METRIC_KIND_COUNT, 1, 2);
    EXPECT_EQ(m.count, 0);
}

TEST(MetricsRing, SealLoadRoundTrip)
{
    metrics_t m;
    metrics_init(&m);
    metrics_note_boot(&m, 3);
    metrics_push(&m, METRIC_BLE_DISCONNECT, 0x13, 120);
    metrics_seal(&m);
    metrics_t back;
    ASSERT_TRUE(metrics_load(&back, &m, sizeof(m)));
    EXPECT_EQ(back.boot_count, 1u);
    EXPECT_EQ(back.count, 2);
    EXPECT_EQ(metrics_at(&back, 1)->kind, METRIC_BLE_DISCONNECT);
    EXPECT_EQ(metrics_at(&back, 1)->a, 0x13u);
}

TEST(MetricsRing, LoadRejectsCorruptWrongSizeAndBadIndices)
{
    metrics_t m;
    metrics_init(&m);
    metrics_note_boot(&m, 1);
    metrics_seal(&m);

    metrics_t bad = m;
    bad.boot_count = 77; // crc no longer matches
    metrics_t out;
    EXPECT_FALSE(metrics_load(&out, &bad, sizeof(bad)));
    EXPECT_EQ(out.boot_count, 0u); // reset to empty

    EXPECT_FALSE(metrics_load(&out, &m, sizeof(m) - 1));
    EXPECT_FALSE(metrics_load(&out, nullptr, 0));

    bad = m;
    bad.head = METRICS_RING_LEN; // out of range, even with a valid crc
    metrics_seal(&bad);
    EXPECT_FALSE(metrics_load(&out, &bad, sizeof(bad)));

    bad = m;
    bad.version = METRICS_VERSION + 1;
    metrics_seal(&bad);
    EXPECT_FALSE(metrics_load(&out, &bad, sizeof(bad)));
}

TEST(MetricsRing, Names)
{
    EXPECT_STREQ(metrics_reset_name(4), "panic");
    EXPECT_STREQ(metrics_reset_name(9), "brownout");
    EXPECT_STREQ(metrics_reset_name(200), "?");
    EXPECT_STREQ(metrics_kind_name(METRIC_BLE_DISCONNECT), "ble-disc");
}
