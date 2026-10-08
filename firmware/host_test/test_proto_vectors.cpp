// Protocol golden vectors (protocol/testvectors): decode each .bin with nanopb,
// compare the fields with the .json, re-encode and compare the bytes.
// The Kotlin tests in android :core:protocol run the same vectors, so watch and
// phone agree on the wire format.
#include <gtest/gtest.h>

#include <pb_decode.h>
#include <pb_encode.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "envelope.pb.h"

namespace {

using nlohmann::json;

const std::filesystem::path kVectorsDir{S3W_TESTVECTORS_DIR};

// Every vector must have a TEST below; VectorsCovered enforces it.
const std::set<std::string> kCoveredVectors = {
    "envelope_ack_error",
    "envelope_hello",
    "envelope_hello_ack",
    "envelope_time_sync",
};

std::vector<uint8_t> read_bin(const std::string &name)
{
    std::ifstream f(kVectorsDir / (name + ".bin"), std::ios::binary);
    EXPECT_TRUE(f.is_open()) << name << ".bin";
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

json read_json(const std::string &name)
{
    std::ifstream f(kVectorsDir / (name + ".json"));
    EXPECT_TRUE(f.is_open()) << name << ".json";
    return json::parse(f);
}

// Decodes the vector, re-encodes the result and expects identical bytes.
s3w_v1_Envelope decode_round_trip(const std::string &name)
{
    const std::vector<uint8_t> bin = read_bin(name);

    s3w_v1_Envelope env = s3w_v1_Envelope_init_zero;
    pb_istream_t in = pb_istream_from_buffer(bin.data(), bin.size());
    EXPECT_TRUE(pb_decode(&in, s3w_v1_Envelope_fields, &env)) << PB_GET_ERROR(&in);

    std::vector<uint8_t> out(s3w_v1_Envelope_size);
    pb_ostream_t os = pb_ostream_from_buffer(out.data(), out.size());
    EXPECT_TRUE(pb_encode(&os, s3w_v1_Envelope_fields, &env)) << PB_GET_ERROR(&os);
    out.resize(os.bytes_written);
    EXPECT_EQ(out, bin) << name << ": re-encoded bytes differ";
    return env;
}

// proto3 JSON omits default values.
uint32_t u32(const json &j, const char *key)
{
    return j.value(key, 0U);
}

std::string str(const json &j, const char *key)
{
    return j.value(key, std::string{});
}

void expect_header(const s3w_v1_Envelope &env, const json &j)
{
    EXPECT_EQ(env.id, u32(j, "id"));
    EXPECT_EQ(env.reply_to, u32(j, "replyTo"));
    ASSERT_EQ(env.has_status, j.contains("status"));
    if (env.has_status) {
        const json &s = j["status"];
        EXPECT_EQ(env.status.code, s.value("code", 0));
        EXPECT_EQ(std::string(env.status.message), str(s, "message"));
    }
}

template <size_t N, size_t M>
void expect_caps(pb_size_t count, const char (&caps)[N][M], const json &j)
{
    const json expected = j.value("caps", json::array());
    ASSERT_EQ(count, expected.size());
    for (pb_size_t i = 0; i < count; i++) {
        EXPECT_EQ(std::string(caps[i]), expected[i].get<std::string>());
    }
}

TEST(GoldenVectors, VectorsCovered)
{
    std::set<std::string> found;
    for (const auto &entry : std::filesystem::directory_iterator(kVectorsDir)) {
        if (entry.path().extension() == ".json") {
            found.insert(entry.path().stem().string());
        }
    }
    EXPECT_EQ(found, kCoveredVectors) << "add a TEST for each new vector";
}

TEST(GoldenVectors, EnvelopeHello)
{
    const json j = read_json("envelope_hello");
    const s3w_v1_Envelope env = decode_round_trip("envelope_hello");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_hello_tag);

    const s3w_v1_Hello &m = env.body.hello;
    const json &b = j["hello"];
    EXPECT_EQ(m.proto_major, u32(b, "protoMajor"));
    EXPECT_EQ(m.proto_minor, u32(b, "protoMinor"));
    EXPECT_EQ(std::string(m.app_version), str(b, "appVersion"));
    expect_caps(m.caps_count, m.caps, b);
    EXPECT_EQ(std::string(m.phone_model), str(b, "phoneModel"));
    EXPECT_EQ(std::string(m.locale), str(b, "locale"));
    EXPECT_EQ(std::string(m.tz_name), str(b, "tzName"));
}

TEST(GoldenVectors, EnvelopeHelloAck)
{
    const json j = read_json("envelope_hello_ack");
    const s3w_v1_Envelope env = decode_round_trip("envelope_hello_ack");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_hello_ack_tag);

    const s3w_v1_HelloAck &m = env.body.hello_ack;
    const json &b = j["helloAck"];
    EXPECT_EQ(m.proto_major, u32(b, "protoMajor"));
    EXPECT_EQ(m.proto_minor, u32(b, "protoMinor"));
    EXPECT_EQ(std::string(m.fw_version), str(b, "fwVersion"));
    EXPECT_EQ(std::string(m.hw_rev), str(b, "hwRev"));
    EXPECT_EQ(std::string(m.serial), str(b, "serial"));
    expect_caps(m.caps_count, m.caps, b);
    EXPECT_EQ(m.battery_pct, u32(b, "batteryPct"));
    EXPECT_EQ(m.storage_free_kb, u32(b, "storageFreeKb"));
    EXPECT_EQ(m.storage_total_kb, u32(b, "storageTotalKb"));
    EXPECT_EQ(m.api_level, u32(b, "apiLevel"));
}

TEST(GoldenVectors, EnvelopeTimeSync)
{
    const json j = read_json("envelope_time_sync");
    const s3w_v1_Envelope env = decode_round_trip("envelope_time_sync");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_time_sync_tag);

    const s3w_v1_TimeSync &m = env.body.time_sync;
    const json &b = j["timeSync"];
    // proto3 JSON carries int64 as a string.
    EXPECT_EQ(m.unix_ms, std::stoll(str(b, "unixMs")));
    EXPECT_EQ(std::string(m.tz_posix), str(b, "tzPosix"));
    EXPECT_EQ(std::string(m.tz_name), str(b, "tzName"));
    EXPECT_EQ(m.is_24h, b.value("is24h", false));
}

TEST(GoldenVectors, EnvelopeAckError)
{
    const json j = read_json("envelope_ack_error");
    const s3w_v1_Envelope env = decode_round_trip("envelope_ack_error");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_ack_tag);
    EXPECT_EQ(env.status.code, static_cast<int32_t>(s3w_v1_StatusCode_STATUS_BUSY));
}

} // namespace
