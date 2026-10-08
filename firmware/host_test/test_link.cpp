// svc_link pure logic (P4-02): framing, reassembly, request/reply retries and dedupe, the
// windowed bulk engine under loss. Golden envelope vectors go through fragmentation at several MTUs.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numeric>
#include <random>
#include <vector>

#include "link_bulk.h"
#include "link_frame.h"
#include "link_rpc.h"

namespace {

using Bytes = std::vector<uint8_t>;

Bytes pattern(size_t n, uint32_t seed = 1)
{
    Bytes b(n);
    std::mt19937 g(seed);
    for (auto &x : b) {
        x = static_cast<uint8_t>(g());
    }
    return b;
}

// ---------------------------------------------------------------- framing

struct Wire {
    std::vector<Bytes> frames;
    static bool tx(void *ctx, link_chan_t, const uint8_t *f, size_t n)
    {
        static_cast<Wire *>(ctx)->frames.emplace_back(f, f + n);
        return true;
    }
};

struct Rx {
    Bytes buf = Bytes(LINK_CTRL_MAX_MSG);
    link_reasm_t r;
    std::vector<Bytes> msgs;
    Rx() { link_reasm_init(&r, buf.data(), buf.size()); }
    link_rx_t push(const Bytes &f)
    {
        link_frame_t fr;
        EXPECT_TRUE(link_frame_parse(f.data(), f.size(), &fr));
        const uint8_t *m = nullptr;
        size_t n = 0;
        const link_rx_t res = link_reasm_push(&r, &fr, &m, &n);
        if (res == LINK_RX_MESSAGE) {
            msgs.emplace_back(m, m + n);
        }
        return res;
    }
};

TEST(LinkFrame, HeaderLayout)
{
    uint8_t f[16];
    const uint8_t p[] = {0xAA, 0xBB};
    const size_t n = link_frame_build(f, sizeof f, LINK_FRAG_LAST, 0x1234, LINK_CHAN_BULK, p, 2);
    ASSERT_EQ(n, 6U);
    EXPECT_EQ(f[0], 0xC1); // frag 11, major 1
    EXPECT_EQ(f[1], 0x34); // seq little-endian
    EXPECT_EQ(f[2], 0x12);
    EXPECT_EQ(f[3], 1);
    link_frame_t fr;
    ASSERT_TRUE(link_frame_parse(f, n, &fr));
    EXPECT_EQ(fr.frag, LINK_FRAG_LAST);
    EXPECT_EQ(fr.version, 1);
    EXPECT_EQ(fr.seq, 0x1234);
    EXPECT_EQ(fr.chan, LINK_CHAN_BULK);
    EXPECT_EQ(fr.len, 2U);
    EXPECT_EQ(link_frame_build(f, 5, LINK_FRAG_SINGLE, 0, LINK_CHAN_CONTROL, p, 2), 0U);
}

TEST(LinkFrame, ParseRejectsShortAndBadChannel)
{
    link_frame_t fr;
    const uint8_t short_frame[] = {0x01, 0, 0};
    EXPECT_FALSE(link_frame_parse(short_frame, sizeof short_frame, &fr));
    const uint8_t bad_chan[] = {0x01, 0, 0, 2};
    EXPECT_FALSE(link_frame_parse(bad_chan, sizeof bad_chan, &fr));
    const uint8_t ok_empty[] = {0x01, 0, 0, 0};
    EXPECT_TRUE(link_frame_parse(ok_empty, sizeof ok_empty, &fr));
    EXPECT_EQ(fr.len, 0U);
}

TEST(LinkFrame, SmallMessageIsOneSingleFrame)
{
    Wire w;
    link_seq_t seq{0};
    const Bytes m = pattern(20);
    ASSERT_TRUE(link_ctrl_send(&seq, m.data(), m.size(), 244, Wire::tx, &w));
    ASSERT_EQ(w.frames.size(), 1U);
    EXPECT_EQ(w.frames[0][0] >> 6, LINK_FRAG_SINGLE);
    EXPECT_EQ(w.frames[0].size(), 24U);
    EXPECT_EQ(seq.next, 1);
}

TEST(LinkFrame, FragmentationRoundTripAcrossSizes)
{
    for (size_t max_frame : {8U, 20U, 23U, 100U, 244U, 514U, 700U}) {
        for (size_t len : {1U, 4U, 19U, 20U, 21U, 243U, 244U, 245U, 1000U, 5000U, 16384U}) {
            Wire w;
            link_seq_t seq{0xFFF0}; // wraps during long messages
            const Bytes m = pattern(len, static_cast<uint32_t>(len));
            ASSERT_TRUE(link_ctrl_send(&seq, m.data(), len, max_frame, Wire::tx, &w)) << max_frame << "/" << len;
            Rx rx;
            for (size_t i = 0; i < w.frames.size(); i++) {
                EXPECT_LE(w.frames[i].size(), std::min<size_t>(max_frame, LINK_MAX_FRAME));
                const link_rx_t res = rx.push(w.frames[i]);
                EXPECT_EQ(res, i + 1 == w.frames.size() ? LINK_RX_MESSAGE : LINK_RX_NONE) << max_frame << "/" << len;
            }
            ASSERT_EQ(rx.msgs.size(), 1U);
            EXPECT_EQ(rx.msgs[0], m) << max_frame << "/" << len;
        }
    }
}

// An attribute value is at most 512 bytes: Android drops a longer notification, so at MTU 517 the
// first fragments of a big watch → phone message (the AppList) never arrived.
TEST(LinkFrame, FramesNeverExceedAnAttributeValue)
{
    EXPECT_EQ(link_max_frame(517), 512U);
    EXPECT_EQ(link_max_frame(247), 244U);
    EXPECT_EQ(link_max_frame(23), 20U);
    Wire w;
    link_seq_t seq{0};
    const Bytes m = pattern(1600);
    ASSERT_TRUE(link_ctrl_send(&seq, m.data(), m.size(), link_max_frame(517), Wire::tx, &w));
    ASSERT_EQ(w.frames.size(), 4U);
    for (const Bytes &f : w.frames) {
        EXPECT_LE(f.size(), 512U);
    }
}

TEST(LinkFrame, SendRejectsBadArguments)
{
    Wire w;
    link_seq_t seq{0};
    const Bytes m = pattern(10);
    EXPECT_FALSE(link_ctrl_send(&seq, m.data(), 0, 244, Wire::tx, &w));
    EXPECT_FALSE(link_ctrl_send(&seq, m.data(), m.size(), LINK_MIN_FRAME - 1, Wire::tx, &w));
    const Bytes big(LINK_CTRL_MAX_MSG + 1);
    EXPECT_FALSE(link_ctrl_send(&seq, big.data(), big.size(), 244, Wire::tx, &w));
    EXPECT_TRUE(w.frames.empty());
}

TEST(LinkFrame, TxFailureStopsAndLeavesAGap)
{
    struct Ctx {
        int sent = 0;
        std::vector<Bytes> frames;
    } c;
    auto tx = [](void *ctx, link_chan_t, const uint8_t *f, size_t n) {
        auto *c = static_cast<Ctx *>(ctx);
        if (c->sent == 2) {
            return false;
        }
        c->sent++;
        c->frames.emplace_back(f, f + n);
        return true;
    };
    link_seq_t seq{0};
    const Bytes m = pattern(500);
    EXPECT_FALSE(link_ctrl_send(&seq, m.data(), m.size(), 100, tx, &c));
    EXPECT_EQ(seq.next, 2); // only the frames that went out consumed numbers
    Rx rx;
    for (const auto &f : c.frames) {
        EXPECT_EQ(rx.push(f), LINK_RX_NONE);
    }
    EXPECT_TRUE(rx.msgs.empty());
}

TEST(LinkReasm, LostMiddleFrameDropsMessageAndNextOneArrives)
{
    Wire w;
    link_seq_t seq{10};
    const Bytes a = pattern(600, 1), b = pattern(50, 2), c = pattern(900, 3);
    link_ctrl_send(&seq, a.data(), a.size(), 100, Wire::tx, &w);
    const size_t a_frames = w.frames.size();
    link_ctrl_send(&seq, b.data(), b.size(), 100, Wire::tx, &w);
    link_ctrl_send(&seq, c.data(), c.size(), 100, Wire::tx, &w);

    Rx rx;
    for (size_t i = 0; i < w.frames.size(); i++) {
        if (i == 2) {
            continue; // lose one middle frame of message a
        }
        rx.push(w.frames[i]);
    }
    ASSERT_GT(a_frames, 3U);
    ASSERT_EQ(rx.msgs.size(), 2U); // a is lost; b and c arrive
    EXPECT_EQ(rx.msgs[0], b);
    EXPECT_EQ(rx.msgs[1], c);
    EXPECT_GE(rx.r.dropped, 1U);
}

TEST(LinkReasm, LostFirstFrameDiscardsRestUntilNextStart)
{
    Wire w;
    link_seq_t seq{0};
    const Bytes a = pattern(400, 1), b = pattern(30, 2);
    link_ctrl_send(&seq, a.data(), a.size(), 100, Wire::tx, &w);
    const size_t n = w.frames.size();
    link_ctrl_send(&seq, b.data(), b.size(), 100, Wire::tx, &w);
    Rx rx;
    for (size_t i = 1; i < w.frames.size(); i++) { // frame 0 (first) lost
        rx.push(w.frames[i]);
    }
    ASSERT_GT(n, 2U);
    ASSERT_EQ(rx.msgs.size(), 1U);
    EXPECT_EQ(rx.msgs[0], b);
}

TEST(LinkReasm, DuplicateFrameIgnoredAndMessageStillCompletes)
{
    Wire w;
    link_seq_t seq{0};
    const Bytes m = pattern(450);
    link_ctrl_send(&seq, m.data(), m.size(), 100, Wire::tx, &w);
    Rx rx;
    for (const auto &f : w.frames) {
        rx.push(f);
        EXPECT_EQ(rx.push(f), LINK_RX_NONE); // immediate repeat
    }
    ASSERT_EQ(rx.msgs.size(), 1U);
    EXPECT_EQ(rx.msgs[0], m);
    EXPECT_EQ(rx.r.dups, w.frames.size());
}

TEST(LinkReasm, RejectsOversizeTotalAndOverrun)
{
    Rx rx;
    // FIRST announcing 20000 bytes.
    Bytes f = {0x41, 0, 0, 0, 0x20, 0x4E, 1, 2, 3};
    EXPECT_EQ(rx.push(f), LINK_RX_DROPPED);
    // FIRST announcing 10, LAST bringing too much.
    Bytes first = {0x41, 1, 0, 0, 10, 0, 1, 2, 3};
    Bytes last = {0xC1, 2, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    EXPECT_EQ(rx.push(first), LINK_RX_NONE);
    EXPECT_EQ(rx.push(last), LINK_RX_DROPPED);
    // A good message afterwards still works (resync on SINGLE).
    Bytes single = {0x01, 9, 0, 0, 0x55};
    EXPECT_EQ(rx.push(single), LINK_RX_MESSAGE);
    EXPECT_EQ(rx.msgs.back(), Bytes{0x55});
}

TEST(LinkReasm, MidMessageJoinWaitsForStart)
{
    Rx rx;
    Bytes mid = {0x81, 5, 0, 0, 1, 2};
    EXPECT_EQ(rx.push(mid), LINK_RX_DROPPED);
    Bytes single = {0x01, 6, 0, 0, 7};
    EXPECT_EQ(rx.push(single), LINK_RX_MESSAGE);
}

TEST(LinkReasm, ResetForgetsSequence)
{
    Rx rx;
    Bytes a = {0x01, 100, 0, 0, 1};
    rx.push(a);
    link_reasm_reset(&rx.r);
    Bytes b = {0x01, 3, 0, 0, 2}; // a new connection restarts numbering
    EXPECT_EQ(rx.push(b), LINK_RX_MESSAGE);
}

TEST(LinkFrame, BulkChunkRoundTrip)
{
    link_seq_t seq{7};
    const Bytes d = pattern(100);
    uint8_t out[200];
    const size_t n = link_bulk_build(out, sizeof out, &seq, 0xA1B2C3D4, 0x00010203, d.data(), d.size());
    ASSERT_EQ(n, 4 + 8 + 100U);
    EXPECT_EQ(seq.next, 8);
    link_frame_t fr;
    ASSERT_TRUE(link_frame_parse(out, n, &fr));
    EXPECT_EQ(fr.chan, LINK_CHAN_BULK);
    link_bulk_chunk_t ch;
    ASSERT_TRUE(link_bulk_parse(&fr, &ch));
    EXPECT_EQ(ch.transfer_id, 0xA1B2C3D4U);
    EXPECT_EQ(ch.offset, 0x00010203U);
    EXPECT_EQ(Bytes(ch.data, ch.data + ch.len), d);
    EXPECT_EQ(out[4], 0xD4); // little-endian id
    EXPECT_EQ(link_bulk_build(out, 20, &seq, 1, 0, d.data(), d.size()), 0U);
}

// Golden vectors survive fragmentation at several MTUs.
TEST(LinkFrame, GoldenVectorsThroughFragmentation)
{
    int count = 0;
    for (const auto &e : std::filesystem::directory_iterator(S3W_TESTVECTORS_DIR)) {
        if (e.path().extension() != ".bin") {
            continue;
        }
        std::ifstream f(e.path(), std::ios::binary);
        const Bytes m{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        for (size_t max_frame : {8U, 12U, 23U, 100U, 244U}) {
            Wire w;
            link_seq_t seq{0};
            ASSERT_TRUE(link_ctrl_send(&seq, m.data(), m.size(), max_frame, Wire::tx, &w)) << e.path();
            Rx rx;
            for (const auto &fr : w.frames) {
                rx.push(fr);
            }
            ASSERT_EQ(rx.msgs.size(), 1U) << e.path() << " mtu " << max_frame;
            EXPECT_EQ(rx.msgs[0], m) << e.path() << " mtu " << max_frame;
        }
        count++;
    }
    EXPECT_GE(count, 7);
}

// -------------------------------------------------------------------- rpc

struct RpcEnv {
    link_rpc_t rpc;
    std::vector<Bytes> sent;
    bool tx_ok = true;
    int mallocs = 0, frees = 0;
    std::vector<std::pair<link_rpc_result_t, int>> done; // result, user

    static RpcEnv *self;
    RpcEnv()
    {
        self = this;
        link_rpc_mem_t mem{
            [](size_t n) -> void * {
                self->mallocs++;
                return std::malloc(n);
            },
            [](void *p) {
                if (p) {
                    self->frees++;
                }
                std::free(p);
            }};
        link_rpc_init(
            &rpc, mem,
            [](void *ctx, const uint8_t *m, size_t n) {
                auto *e = static_cast<RpcEnv *>(ctx);
                if (e->tx_ok) {
                    e->sent.emplace_back(m, m + n);
                }
                return e->tx_ok;
            },
            this);
    }
    ~RpcEnv() { link_rpc_reset(&rpc); }
    static void done_cb(void *user, link_rpc_result_t r, const void *)
    {
        self->done.emplace_back(r, static_cast<int>(reinterpret_cast<intptr_t>(user)));
    }
    bool start(uint32_t id, uint32_t now, int user = 0)
    {
        const Bytes m = {static_cast<uint8_t>(id), 0xEE};
        return link_rpc_start(&rpc, id, m.data(), m.size(), now, done_cb, reinterpret_cast<void *>(static_cast<intptr_t>(user)));
    }
};
RpcEnv *RpcEnv::self = nullptr;

TEST(LinkRpc, IdsAreNonZeroAndWrap)
{
    RpcEnv e;
    e.rpc.next_id = 0xFFFFFFFF;
    EXPECT_EQ(link_rpc_next_id(&e.rpc), 0xFFFFFFFFU);
    EXPECT_EQ(link_rpc_next_id(&e.rpc), 1U);
    EXPECT_EQ(link_rpc_next_id(&e.rpc), 2U);
}

TEST(LinkRpc, ReplyCompletesRequest)
{
    RpcEnv e;
    ASSERT_TRUE(e.start(5, 1000, 42));
    EXPECT_EQ(e.sent.size(), 1U);
    EXPECT_FALSE(link_rpc_on_reply(&e.rpc, 6, nullptr));
    EXPECT_TRUE(link_rpc_on_reply(&e.rpc, 5, nullptr));
    ASSERT_EQ(e.done.size(), 1U);
    EXPECT_EQ(e.done[0], std::make_pair(LINK_RPC_OK, 42));
    EXPECT_FALSE(link_rpc_on_reply(&e.rpc, 5, nullptr)); // late duplicate
    EXPECT_EQ(link_rpc_poll(&e.rpc, 100000), UINT32_MAX);
    EXPECT_EQ(e.mallocs, e.frees);
}

TEST(LinkRpc, RetriesThreeTimesWithSameBytesThenTimesOut)
{
    RpcEnv e;
    ASSERT_TRUE(e.start(9, 0));
    EXPECT_EQ(link_rpc_poll(&e.rpc, 0), 5000U);
    EXPECT_EQ(link_rpc_poll(&e.rpc, 4999), 1U);
    EXPECT_EQ(e.sent.size(), 1U);
    EXPECT_EQ(link_rpc_poll(&e.rpc, 5000), 5000U); // retry 1
    EXPECT_EQ(link_rpc_poll(&e.rpc, 10000), 5000U); // retry 2
    EXPECT_EQ(link_rpc_poll(&e.rpc, 15000), 5000U); // retry 3
    EXPECT_EQ(e.sent.size(), 4U);
    EXPECT_TRUE(e.done.empty());
    EXPECT_EQ(link_rpc_poll(&e.rpc, 20000), UINT32_MAX); // out of retries
    ASSERT_EQ(e.done.size(), 1U);
    EXPECT_EQ(e.done[0].first, LINK_RPC_TIMEOUT);
    for (const auto &m : e.sent) {
        EXPECT_EQ(m, e.sent[0]);
    }
    EXPECT_EQ(e.rpc.retries, 3U);
    EXPECT_EQ(e.rpc.timeouts, 1U);
    EXPECT_EQ(e.mallocs, e.frees);
}

TEST(LinkRpc, ReplyAfterRetryStillCompletes)
{
    RpcEnv e;
    ASSERT_TRUE(e.start(3, 0));
    link_rpc_poll(&e.rpc, 5000);
    EXPECT_TRUE(link_rpc_on_reply(&e.rpc, 3, nullptr));
    EXPECT_EQ(e.done[0].first, LINK_RPC_OK);
}

TEST(LinkRpc, ClockWrapIsHandled)
{
    RpcEnv e;
    const uint32_t t0 = 0xFFFFFFFFU - 1000;
    ASSERT_TRUE(e.start(1, t0));
    EXPECT_EQ(link_rpc_poll(&e.rpc, t0 + 4000), 1000U);
    link_rpc_poll(&e.rpc, t0 + 5000); // wraps past 0
    EXPECT_EQ(e.sent.size(), 2U);
}

TEST(LinkRpc, FirstTxFailureTracksNothing)
{
    RpcEnv e;
    e.tx_ok = false;
    EXPECT_FALSE(e.start(1, 0));
    EXPECT_EQ(link_rpc_poll(&e.rpc, 100000), UINT32_MAX);
    EXPECT_EQ(e.mallocs, e.frees);
}

TEST(LinkRpc, SlotsRunOutAndDuplicateIdRefused)
{
    RpcEnv e;
    for (uint32_t i = 1; i <= LINK_RPC_MAX_PENDING; i++) {
        ASSERT_TRUE(e.start(i, 0));
    }
    EXPECT_FALSE(e.start(99, 0));
    EXPECT_FALSE(e.start(1, 0));
    EXPECT_FALSE(e.start(0, 0));
    link_rpc_on_reply(&e.rpc, 2, nullptr);
    EXPECT_TRUE(e.start(99, 0));
}

TEST(LinkRpc, ResetFailsAllPendingAndClearsDedupe)
{
    RpcEnv e;
    e.start(1, 0, 1);
    e.start(2, 0, 2);
    const uint8_t *r;
    size_t n;
    EXPECT_EQ(link_rpc_on_request(&e.rpc, 77, &r, &n), LINK_SEEN_NEW);
    link_rpc_reset(&e.rpc);
    ASSERT_EQ(e.done.size(), 2U);
    EXPECT_EQ(e.done[0].first, LINK_RPC_DISCONNECTED);
    EXPECT_EQ(e.done[1].first, LINK_RPC_DISCONNECTED);
    EXPECT_EQ(link_rpc_on_request(&e.rpc, 77, &r, &n), LINK_SEEN_NEW); // new session, ids restart
    EXPECT_EQ(e.mallocs, e.frees + 0); // leak check happens in ~RpcEnv via reset
}

TEST(LinkRpc, DedupeReplaysCachedReplyAndIgnoresWithout)
{
    RpcEnv e;
    const uint8_t *r = nullptr;
    size_t n = 0;
    EXPECT_EQ(link_rpc_on_request(&e.rpc, 10, &r, &n), LINK_SEEN_NEW);
    EXPECT_EQ(link_rpc_on_request(&e.rpc, 10, &r, &n), LINK_SEEN_IGNORE); // still handling
    const uint8_t reply[] = {1, 2, 3};
    link_rpc_cache_reply(&e.rpc, 10, reply, 3);
    EXPECT_EQ(link_rpc_on_request(&e.rpc, 10, &r, &n), LINK_SEEN_REPLAY);
    ASSERT_EQ(n, 3U);
    EXPECT_EQ(std::memcmp(r, reply, 3), 0);

    // A reply too big to cache: retries are ignored.
    const Bytes big(LINK_RPC_REPLY_CACHE_MAX + 1, 7);
    EXPECT_EQ(link_rpc_on_request(&e.rpc, 11, &r, &n), LINK_SEEN_NEW);
    link_rpc_cache_reply(&e.rpc, 11, big.data(), big.size());
    EXPECT_EQ(link_rpc_on_request(&e.rpc, 11, &r, &n), LINK_SEEN_IGNORE);
}

TEST(LinkRpc, DedupeEvictsOldestAfterEightIds)
{
    RpcEnv e;
    const uint8_t *r;
    size_t n;
    for (uint32_t id = 1; id <= LINK_RPC_DEDUPE; id++) {
        EXPECT_EQ(link_rpc_on_request(&e.rpc, id, &r, &n), LINK_SEEN_NEW);
    }
    EXPECT_EQ(link_rpc_on_request(&e.rpc, 1, &r, &n), LINK_SEEN_IGNORE);
    EXPECT_EQ(link_rpc_on_request(&e.rpc, 100, &r, &n), LINK_SEEN_NEW); // evicts id 1
    EXPECT_EQ(link_rpc_on_request(&e.rpc, 1, &r, &n), LINK_SEEN_NEW);
}

// ------------------------------------------------------------------- bulk

struct Sink {
    Bytes data;
    uint32_t expect_next = 0;
    bool fail_write = false;
    bool verdict = true;
    int finishes = 0;
    link_bulk_sink_t sink()
    {
        return {[](void *c, uint32_t off, const uint8_t *d, size_t n) {
                    auto *s = static_cast<Sink *>(c);
                    if (s->fail_write) {
                        return false;
                    }
                    EXPECT_EQ(off, s->data.size()) << "writes must be sequential";
                    s->data.insert(s->data.end(), d, d + n);
                    return true;
                },
                [](void *c) {
                    auto *s = static_cast<Sink *>(c);
                    s->finishes++;
                    return s->verdict;
                },
                this};
    }
};

// One in-process transfer over a link that drops frames. Time is virtual; every frame takes
// 10 ms each way. Returns when the sender is DONE/FAILED or the budget runs out.
struct BulkSim {
    Bytes src;
    Sink dst;
    link_bulk_tx_t tx;
    link_bulk_rx_t rx;
    uint32_t chunk, window;
    double loss;
    std::mt19937 rng;

    struct Msg {
        uint32_t at;
        enum Kind { CHUNK, STATUS, END } kind;
        uint32_t offset;  // chunk offset / status next_offset
        Bytes data;
        bool verified;
    };
    std::deque<Msg> to_rx, to_tx;
    uint32_t now = 1000;
    uint32_t ack_msgs = 0, end_msgs = 0;

    BulkSim(size_t size, uint32_t chunk_, uint32_t window_, double loss_, uint32_t seed)
        : src(pattern(size, seed)), chunk(chunk_), window(window_), loss(loss_), rng(seed)
    {
    }

    bool drop() { return std::uniform_real_distribution<double>(0, 1)(rng) < loss; }

    void rx_event(link_bulk_rx_event_t ev)
    {
        if (ev == LINK_BULK_RX_ACK) {
            ack_msgs++;
            if (!drop()) {
                to_tx.push_back({now + 10, Msg::STATUS, link_bulk_rx_next(&rx), {}, false});
            }
        } else if (ev == LINK_BULK_RX_COMPLETE || ev == LINK_BULK_RX_ERROR) {
            end_msgs++;
            if (!drop()) {
                to_tx.push_back({now + 10, Msg::END, 0, {}, ev == LINK_BULK_RX_COMPLETE && rx.verified});
            }
        }
    }

    // Starts both sides the way svc_link does: Begin → Status{resume} → first window.
    void start(uint32_t resume)
    {
        auto s = dst.sink();
        ASSERT_TRUE(link_bulk_rx_begin(&rx, 7, static_cast<uint32_t>(src.size()), window, resume, &s));
        ASSERT_TRUE(link_bulk_tx_begin(&tx, 7, static_cast<uint32_t>(src.size()), window, chunk, link_bulk_rx_next(&rx), now));
        if (rx.state == LINK_BULK_RX_DONE) {
            rx_event(LINK_BULK_RX_COMPLETE);
        }
    }

    bool finished() const { return tx.state == LINK_BULK_TX_DONE || tx.state == LINK_BULK_TX_FAILED; }

    void run(uint32_t budget_ms = 3'600'000)
    {
        const uint32_t end = now + budget_ms;
        while (!finished() && now < end) {
            // Deliver due messages.
            while (!to_rx.empty() && to_rx.front().at <= now) {
                Msg m = std::move(to_rx.front());
                to_rx.pop_front();
                rx_event(link_bulk_rx_chunk(&rx, 7, m.offset, m.data.data(), m.data.size()));
            }
            while (!to_tx.empty() && to_tx.front().at <= now) {
                Msg m = std::move(to_tx.front());
                to_tx.pop_front();
                if (m.kind == Msg::STATUS) {
                    link_bulk_tx_on_status(&tx, m.offset, now);
                } else {
                    link_bulk_tx_on_end(&tx, m.verified);
                }
            }
            // Sender: put chunks on the wire (one per 2 ms, like a connection event rate).
            uint32_t off;
            size_t len;
            if (link_bulk_tx_next(&tx, &off, &len)) {
                if (!drop()) {
                    to_rx.push_back({now + 10, Msg::CHUNK, off, Bytes(src.begin() + off, src.begin() + off + len), false});
                }
                link_bulk_tx_sent(&tx, now);
            }
            link_bulk_tx_poll(&tx, now);
            now += 2;
        }
    }
};

TEST(LinkBulk, CleanTransferAcksPerWindow)
{
    BulkSim s(100'000, 498, 16, 0.0, 1);
    s.start(0);
    s.run();
    ASSERT_EQ(s.tx.state, LINK_BULK_TX_DONE);
    EXPECT_EQ(s.dst.data, s.src);
    EXPECT_EQ(s.dst.finishes, 1);
    EXPECT_EQ(s.tx.chunks_resent, 0U);
    const uint32_t chunks = (100'000 + 497) / 498;
    EXPECT_EQ(s.tx.chunks_sent, chunks);
    EXPECT_EQ(s.ack_msgs, (chunks - 1) / 16); // the last (partial) window ends with TransferEnd
}

TEST(LinkBulk, ExactMultipleOfWindowAndChunk)
{
    BulkSim s(498 * 32, 498, 16, 0.0, 2);
    s.start(0);
    s.run();
    ASSERT_EQ(s.tx.state, LINK_BULK_TX_DONE);
    EXPECT_EQ(s.dst.data, s.src);
}

TEST(LinkBulk, TinyAndEmptyTransfers)
{
    for (size_t size : {0U, 1U, 497U, 498U, 499U}) {
        BulkSim s(size, 498, 16, 0.0, 3);
        s.start(0);
        s.run();
        ASSERT_EQ(s.tx.state, LINK_BULK_TX_DONE) << size;
        EXPECT_EQ(s.dst.data, s.src) << size;
        EXPECT_EQ(s.dst.finishes, 1) << size;
    }
}

TEST(LinkBulk, LossInjectionAlwaysConverges)
{
    for (double loss : {0.01, 0.05, 0.1, 0.2}) {
        for (uint32_t seed = 1; seed <= 10; seed++) {
            BulkSim s(60'000, 244, 16, loss, seed);
            s.start(0);
            s.run();
            ASSERT_EQ(s.tx.state, LINK_BULK_TX_DONE) << "loss " << loss << " seed " << seed;
            EXPECT_EQ(s.dst.data, s.src) << "loss " << loss << " seed " << seed;
            EXPECT_EQ(s.dst.finishes, 1);
        }
    }
}

TEST(LinkBulk, WindowSizeOneAndMaxUnderLoss)
{
    for (uint32_t window : {1U, 4U, static_cast<uint32_t>(LINK_BULK_WINDOW_MAX)}) {
        BulkSim s(20'000, 100, window, 0.1, 40 + window);
        s.start(0);
        s.run();
        ASSERT_EQ(s.tx.state, LINK_BULK_TX_DONE) << window;
        EXPECT_EQ(s.dst.data, s.src) << window;
    }
}

TEST(LinkBulk, LostChunkCostsAtMostAWindowOfRetransmission)
{
    BulkSim s(498 * 64, 498, 16, 0.0, 5);
    s.start(0);
    // Drop exactly chunk index 20 on its way to the receiver.
    uint32_t sent = 0;
    while (!s.finished() && s.now < 600'000) {
        while (!s.to_rx.empty() && s.to_rx.front().at <= s.now) {
            auto m = std::move(s.to_rx.front());
            s.to_rx.pop_front();
            s.rx_event(link_bulk_rx_chunk(&s.rx, 7, m.offset, m.data.data(), m.data.size()));
        }
        while (!s.to_tx.empty() && s.to_tx.front().at <= s.now) {
            auto m = std::move(s.to_tx.front());
            s.to_tx.pop_front();
            if (m.kind == BulkSim::Msg::STATUS) {
                link_bulk_tx_on_status(&s.tx, m.offset, s.now);
            } else {
                link_bulk_tx_on_end(&s.tx, m.verified);
            }
        }
        uint32_t off;
        size_t len;
        if (link_bulk_tx_next(&s.tx, &off, &len)) {
            if (sent++ != 20) {
                s.to_rx.push_back({s.now + 10, BulkSim::Msg::CHUNK, off, Bytes(s.src.begin() + off, s.src.begin() + off + len), false});
            }
            link_bulk_tx_sent(&s.tx, s.now);
        }
        link_bulk_tx_poll(&s.tx, s.now);
        s.now += 2;
    }
    ASSERT_EQ(s.tx.state, LINK_BULK_TX_DONE);
    EXPECT_EQ(s.dst.data, s.src);
    EXPECT_LE(s.tx.chunks_resent, 16U);
    EXPECT_GE(s.tx.chunks_resent, 1U);
    EXPECT_EQ(s.rx.gap_chunks > 0, true);
}

TEST(LinkBulk, ResumeStartsAtReceiverOffset)
{
    BulkSim s(50'000, 498, 16, 0.0, 6);
    s.dst.data.assign(s.src.begin(), s.src.begin() + 20'000); // what an earlier try stored
    s.start(20'000);
    EXPECT_EQ(s.tx.sent, 20'000U);
    s.run();
    ASSERT_EQ(s.tx.state, LINK_BULK_TX_DONE);
    EXPECT_EQ(s.dst.data, s.src);
    EXPECT_EQ(s.tx.chunks_sent, (30'000U + 497) / 498);
}

TEST(LinkBulk, FullyResumedCompletesWithoutChunks)
{
    BulkSim s(1000, 498, 16, 0.0, 7);
    s.dst.data = s.src;
    s.start(1000);
    EXPECT_EQ(s.rx.state, LINK_BULK_RX_DONE);
    s.run();
    EXPECT_EQ(s.tx.state, LINK_BULK_TX_DONE);
    EXPECT_EQ(s.tx.chunks_sent, 0U);
}

TEST(LinkBulk, RejectedHashFailsSender)
{
    BulkSim s(5000, 498, 16, 0.0, 8);
    s.dst.verdict = false;
    s.start(0);
    s.run();
    EXPECT_EQ(s.tx.state, LINK_BULK_TX_FAILED);
    EXPECT_EQ(s.rx.verified, false);
}

TEST(LinkBulk, StorageErrorEndsReceiver)
{
    Sink dst;
    dst.fail_write = true;
    link_bulk_rx_t rx;
    auto sink = dst.sink();
    ASSERT_TRUE(link_bulk_rx_begin(&rx, 1, 100, 4, 0, &sink));
    const Bytes d(10, 1);
    EXPECT_EQ(link_bulk_rx_chunk(&rx, 1, 0, d.data(), d.size()), LINK_BULK_RX_ERROR);
    EXPECT_EQ(rx.state, LINK_BULK_RX_FAILED);
    EXPECT_EQ(link_bulk_rx_chunk(&rx, 1, 0, d.data(), d.size()), LINK_BULK_RX_NONE);
}

TEST(LinkBulk, ReceiverGapAckedOnceAndDuplicatesQuiet)
{
    Sink dst;
    link_bulk_rx_t rx;
    auto sink = dst.sink();
    ASSERT_TRUE(link_bulk_rx_begin(&rx, 1, 1000, 8, 0, &sink));
    const Bytes d(100, 1);
    EXPECT_EQ(link_bulk_rx_chunk(&rx, 1, 0, d.data(), d.size()), LINK_BULK_RX_NONE);
    // Chunk at 100 lost; 200, 300, 400 arrive: one ack for the whole burst.
    EXPECT_EQ(link_bulk_rx_chunk(&rx, 1, 200, d.data(), d.size()), LINK_BULK_RX_ACK);
    EXPECT_EQ(link_bulk_rx_chunk(&rx, 1, 300, d.data(), d.size()), LINK_BULK_RX_NONE);
    EXPECT_EQ(link_bulk_rx_chunk(&rx, 1, 400, d.data(), d.size()), LINK_BULK_RX_NONE);
    EXPECT_EQ(link_bulk_rx_next(&rx), 100U);
    // Sender rewound: 100 arrives, in order again.
    EXPECT_EQ(link_bulk_rx_chunk(&rx, 1, 100, d.data(), d.size()), LINK_BULK_RX_NONE);
    // A wrong transfer id is ignored.
    EXPECT_EQ(link_bulk_rx_chunk(&rx, 2, 200, d.data(), d.size()), LINK_BULK_RX_NONE);
}

TEST(LinkBulk, ParameterValidation)
{
    Sink dst;
    auto sink = dst.sink();
    link_bulk_rx_t rx;
    EXPECT_FALSE(link_bulk_rx_begin(&rx, 1, 100, 0, 0, &sink));
    EXPECT_FALSE(link_bulk_rx_begin(&rx, 1, 100, LINK_BULK_WINDOW_MAX + 1, 0, &sink));
    EXPECT_FALSE(link_bulk_rx_begin(&rx, 1, 100, 4, 101, &sink));
    link_bulk_tx_t tx;
    EXPECT_FALSE(link_bulk_tx_begin(&tx, 1, 100, 4, 0, 0, 0));
    EXPECT_FALSE(link_bulk_tx_begin(&tx, 1, 100, 0, 10, 0, 0));
    EXPECT_FALSE(link_bulk_tx_begin(&tx, 1, 100, 4, 10, 101, 0));
}

TEST(LinkBulk, SenderGivesUpWhenReceiverIsSilent)
{
    link_bulk_tx_t tx;
    ASSERT_TRUE(link_bulk_tx_begin(&tx, 1, 10'000, 4, 100, 0, 0));
    uint32_t now = 0;
    for (int guard = 0; guard < 100'000 && tx.state != LINK_BULK_TX_FAILED; guard++) {
        uint32_t off;
        size_t len;
        if (link_bulk_tx_next(&tx, &off, &len)) {
            link_bulk_tx_sent(&tx, now);
        }
        link_bulk_tx_poll(&tx, now);
        now += 10;
    }
    EXPECT_EQ(tx.state, LINK_BULK_TX_FAILED);
    EXPECT_LE(now, (LINK_BULK_MAX_STALLS + 1) * LINK_BULK_ACK_TIMEOUT_MS + 1000);
}

TEST(LinkBulk, SenderIgnoresStaleAndBogusStatus)
{
    link_bulk_tx_t tx;
    ASSERT_TRUE(link_bulk_tx_begin(&tx, 1, 1000, 2, 100, 0, 0));
    uint32_t off;
    size_t len;
    for (int i = 0; i < 2; i++) {
        ASSERT_TRUE(link_bulk_tx_next(&tx, &off, &len));
        link_bulk_tx_sent(&tx, 0);
    }
    EXPECT_EQ(tx.state, LINK_BULK_TX_WAIT_ACK);
    link_bulk_tx_on_status(&tx, 200, 1); // whole window
    EXPECT_EQ(tx.state, LINK_BULK_TX_SENDING);
    link_bulk_tx_on_status(&tx, 100, 2); // older than acked
    EXPECT_EQ(tx.acked, 200U);
    link_bulk_tx_on_status(&tx, 5000, 2); // beyond size
    EXPECT_EQ(tx.acked, 200U);
    EXPECT_EQ(tx.sent, 200U);
}

TEST(LinkBulk, LostTransferEndIsRepeatedWhenSenderResendsLastChunk)
{
    BulkSim s(1000, 498, 16, 0.0, 9);
    s.start(0);
    // Run, but eat the first TransferEnd.
    bool ate = false;
    while (!s.finished() && s.now < 60'000) {
        while (!s.to_rx.empty() && s.to_rx.front().at <= s.now) {
            auto m = std::move(s.to_rx.front());
            s.to_rx.pop_front();
            s.rx_event(link_bulk_rx_chunk(&s.rx, 7, m.offset, m.data.data(), m.data.size()));
        }
        while (!s.to_tx.empty() && s.to_tx.front().at <= s.now) {
            auto m = std::move(s.to_tx.front());
            s.to_tx.pop_front();
            if (m.kind == BulkSim::Msg::END && !ate) {
                ate = true;
                continue;
            }
            if (m.kind == BulkSim::Msg::STATUS) {
                link_bulk_tx_on_status(&s.tx, m.offset, s.now);
            } else {
                link_bulk_tx_on_end(&s.tx, m.verified);
            }
        }
        uint32_t off;
        size_t len;
        if (link_bulk_tx_next(&s.tx, &off, &len)) {
            s.to_rx.push_back({s.now + 10, BulkSim::Msg::CHUNK, off, Bytes(s.src.begin() + off, s.src.begin() + off + len), false});
            link_bulk_tx_sent(&s.tx, s.now);
        }
        link_bulk_tx_poll(&s.tx, s.now);
        s.now += 2;
    }
    EXPECT_TRUE(ate);
    ASSERT_EQ(s.tx.state, LINK_BULK_TX_DONE);
    EXPECT_EQ(s.dst.data, s.src);
    EXPECT_EQ(s.dst.finishes, 1); // verified once, End repeated without re-verifying
}

} // namespace
