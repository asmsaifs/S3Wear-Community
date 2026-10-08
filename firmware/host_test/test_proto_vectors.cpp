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
    "envelope_app_cmd_result",
    "envelope_app_cmd_result_declined",
    "envelope_app_cmd_uninstall",
    "envelope_app_icon",
    "envelope_app_install",
    "envelope_app_list",
    "envelope_app_list_request",
    "envelope_calendar",
    "envelope_calendar_empty",
    "envelope_call_command",
    "envelope_call_state",
    "envelope_call_state_active",
    "envelope_call_state_missed",
    "envelope_device_status",
    "envelope_find_phone_stopped",
    "envelope_find_watch",
    "envelope_ha_command",
    "envelope_ha_config",
    "envelope_ha_config_clear",
    "envelope_ha_states",
    "envelope_ha_states_denied",
    "envelope_hello",
    "envelope_hello_ack",
    "envelope_http_request",
    "envelope_http_response",
    "envelope_http_response_error",
    "envelope_media_artwork",
    "envelope_media_artwork_request",
    "envelope_media_command",
    "envelope_media_state",
    "envelope_media_state_bitmap",
    "envelope_media_state_none",
    "envelope_media_text_bitmap",
    "envelope_notif_action",
    "envelope_license_install",
    "envelope_license_install_query",
    "envelope_license_status",
    "envelope_license_status_refused",
    "envelope_notif_action_dismiss",
    "envelope_notif_posted",
    "envelope_notif_removed",
    "envelope_screenshot",
    "envelope_text_bitmap",
    "envelope_time_sync",
    "envelope_weather",
    "envelope_weather_minimal",
    "envelope_wifi_config_add",
    "envelope_wifi_config_get",
    "envelope_wifi_status",
    "envelope_wifi_status_auth",
    "envelope_xfer_begin",
    "envelope_xfer_end",
    "envelope_xfer_status",
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

// proto3 JSON bytes are standard base64.
std::vector<uint8_t> b64(const json &j, const char *key)
{
    const std::string in = str(j, key);
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : in) {
        int v;
        if (c >= 'A' && c <= 'Z') {
            v = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            v = c - 'a' + 26;
        } else if (c >= '0' && c <= '9') {
            v = c - '0' + 52;
        } else if (c == '+') {
            v = 62;
        } else if (c == '/') {
            v = 63;
        } else {
            break; // '=' padding
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)(acc >> bits));
        }
    }
    return out;
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

TEST(GoldenVectors, EnvelopeDeviceStatus)
{
    const json j = read_json("envelope_device_status");
    const s3w_v1_Envelope env = decode_round_trip("envelope_device_status");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_device_status_tag);

    const s3w_v1_DeviceStatus &m = env.body.device_status;
    const json &b = j["deviceStatus"];
    EXPECT_EQ(m.battery_pct, u32(b, "batteryPct"));
    EXPECT_EQ(m.charging, b.value("charging", false));
    EXPECT_EQ(m.usb_power, b.value("usbPower", false));
    EXPECT_EQ(m.storage_free_kb, u32(b, "storageFreeKb"));
    EXPECT_EQ(m.storage_total_kb, u32(b, "storageTotalKb"));
    EXPECT_EQ(m.time_valid, b.value("timeValid", false));
}

TEST(GoldenVectors, EnvelopeXferBegin)
{
    const json j = read_json("envelope_xfer_begin");
    const s3w_v1_Envelope env = decode_round_trip("envelope_xfer_begin");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_xfer_begin_tag);

    const s3w_v1_TransferBegin &m = env.body.xfer_begin;
    const json &b = j["xferBegin"];
    EXPECT_EQ(m.transfer_id, u32(b, "transferId"));
    EXPECT_EQ(m.kind, u32(b, "kind"));
    EXPECT_EQ(m.size, u32(b, "size"));
    EXPECT_EQ(std::string(m.name), str(b, "name"));
    EXPECT_EQ(m.window, u32(b, "window"));
    EXPECT_EQ(m.chunk_size, u32(b, "chunkSize"));
    // sha256 is bytes 0xa0..0xbf in the vector.
    for (uint8_t i = 0; i < 32; i++) {
        EXPECT_EQ(m.sha256[i], 0xa0 + i) << int{i};
    }
}

TEST(GoldenVectors, EnvelopeXferStatus)
{
    const json j = read_json("envelope_xfer_status");
    const s3w_v1_Envelope env = decode_round_trip("envelope_xfer_status");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_xfer_status_tag);

    const s3w_v1_TransferStatus &m = env.body.xfer_status;
    const json &b = j["xferStatus"];
    EXPECT_EQ(m.transfer_id, u32(b, "transferId"));
    EXPECT_EQ(m.next_offset, u32(b, "nextOffset"));
}

TEST(GoldenVectors, EnvelopeXferEnd)
{
    const json j = read_json("envelope_xfer_end");
    const s3w_v1_Envelope env = decode_round_trip("envelope_xfer_end");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_xfer_end_tag);

    const s3w_v1_TransferEnd &m = env.body.xfer_end;
    const json &b = j["xferEnd"];
    EXPECT_EQ(m.transfer_id, u32(b, "transferId"));
    EXPECT_EQ(m.verified, b.value("verified", false));
}

TEST(GoldenVectors, EnvelopeNotifPosted)
{
    const json j = read_json("envelope_notif_posted");
    const s3w_v1_Envelope env = decode_round_trip("envelope_notif_posted");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_notif_posted_tag);

    const s3w_v1_NotificationPosted &m = env.body.notif_posted;
    const json &b = j["notifPosted"];
    EXPECT_EQ(m.nid, u32(b, "nid"));
    EXPECT_EQ(std::string(m.app_id), str(b, "appId"));
    EXPECT_EQ(std::string(m.app_name), str(b, "appName"));
    EXPECT_EQ(m.icon_hash, u32(b, "iconHash"));
    EXPECT_EQ(std::string(m.title), str(b, "title"));
    EXPECT_EQ(std::string(m.text), str(b, "text"));
    EXPECT_EQ(m.when_ms, std::stoll(str(b, "whenMs")));
    EXPECT_EQ(m.category, u32(b, "category"));
    EXPECT_EQ(m.silent, b.value("silent", false));
    EXPECT_EQ(m.text_bitmap_id, u32(b, "textBitmapId"));
    const json &actions = b["actions"];
    ASSERT_EQ(m.actions_count, actions.size());
    for (pb_size_t i = 0; i < m.actions_count; i++) {
        EXPECT_EQ(m.actions[i].id, u32(actions[i], "id"));
        EXPECT_EQ(std::string(m.actions[i].title), str(actions[i], "title"));
        EXPECT_EQ(m.actions[i].is_reply, actions[i].value("isReply", false));
    }
}

TEST(GoldenVectors, EnvelopeNotifRemoved)
{
    const json j = read_json("envelope_notif_removed");
    const s3w_v1_Envelope env = decode_round_trip("envelope_notif_removed");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_notif_removed_tag);
    EXPECT_EQ(env.body.notif_removed.nid, u32(j["notifRemoved"], "nid"));
}

void expect_find(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_find_tag);
    const json &b = j["find"];
    EXPECT_EQ(env.body.find.target, u32(b, "target"));
    EXPECT_EQ(env.body.find.ring, b.value("ring", false));
}

TEST(GoldenVectors, EnvelopeFindWatch)
{
    expect_find("envelope_find_watch");
    EXPECT_EQ(decode_round_trip("envelope_find_watch").body.find.target, (uint32_t)s3w_v1_FindTarget_FIND_WATCH);
}

TEST(GoldenVectors, EnvelopeFindPhoneStopped)
{
    expect_find("envelope_find_phone_stopped");
    EXPECT_EQ(decode_round_trip("envelope_find_phone_stopped").body.find.target, (uint32_t)s3w_v1_FindTarget_FIND_PHONE);
}

TEST(GoldenVectors, EnvelopeHttpRequest)
{
    const json j = read_json("envelope_http_request");
    const s3w_v1_Envelope env = decode_round_trip("envelope_http_request");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_http_req_tag);
    const s3w_v1_HttpProxyRequest &m = env.body.http_req;
    const json &b = j["httpReq"];
    EXPECT_EQ(m.request_id, u32(b, "requestId"));
    EXPECT_EQ(std::string(m.app_id), str(b, "appId"));
    EXPECT_EQ(m.method, (uint32_t)s3w_v1_HttpMethod_HTTP_POST);
    EXPECT_EQ(std::string(m.url), str(b, "url"));
    EXPECT_EQ(std::string(m.content_type), str(b, "contentType"));
    EXPECT_EQ(std::vector<uint8_t>(m.body.bytes, m.body.bytes + m.body.size), b64(b, "body"));
    EXPECT_EQ(m.max_bytes, u32(b, "maxBytes"));
}

void expect_http_response(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_http_resp_tag);
    const s3w_v1_HttpProxyResponse &m = env.body.http_resp;
    const json &b = j["httpResp"];
    EXPECT_EQ(m.request_id, u32(b, "requestId"));
    EXPECT_EQ(m.status, u32(b, "status"));
    EXPECT_EQ(m.error, u32(b, "error"));
    EXPECT_EQ(m.offset, u32(b, "offset"));
    EXPECT_EQ(std::vector<uint8_t>(m.body.bytes, m.body.bytes + m.body.size), b64(b, "body"));
    EXPECT_EQ(m.last, b.value("last", false));
    EXPECT_EQ(m.total, u32(b, "total"));
    EXPECT_EQ(m.truncated, b.value("truncated", false));
}

TEST(GoldenVectors, EnvelopeHttpResponse)
{
    expect_http_response("envelope_http_response");
}

TEST(GoldenVectors, EnvelopeHttpResponseError)
{
    expect_http_response("envelope_http_response_error");
    EXPECT_EQ(decode_round_trip("envelope_http_response_error").body.http_resp.error,
              (uint32_t)s3w_v1_HttpError_HTTP_ERR_TIMEOUT);
}

void expect_notif_action(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_notif_action_tag);

    const s3w_v1_NotificationAction &m = env.body.notif_action;
    const json &b = j["notifAction"];
    EXPECT_EQ(m.nid, u32(b, "nid"));
    EXPECT_EQ(m.action_id, u32(b, "actionId"));
    EXPECT_EQ(std::string(m.reply_text), str(b, "replyText"));
    EXPECT_EQ(m.dismiss, b.value("dismiss", false));
}

TEST(GoldenVectors, EnvelopeNotifAction)
{
    expect_notif_action("envelope_notif_action");
}

TEST(GoldenVectors, EnvelopeNotifActionDismiss)
{
    expect_notif_action("envelope_notif_action_dismiss");
}

TEST(GoldenVectors, EnvelopeAppIcon)
{
    const json j = read_json("envelope_app_icon");
    const s3w_v1_Envelope env = decode_round_trip("envelope_app_icon");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_app_icon_tag);

    const s3w_v1_AppIcon &m = env.body.app_icon;
    const json &b = j["appIcon"];
    EXPECT_EQ(m.icon_hash, u32(b, "iconHash"));
    EXPECT_EQ(m.width, u32(b, "width"));
    EXPECT_EQ(m.height, u32(b, "height"));
    EXPECT_EQ(std::vector<uint8_t>(m.pixels.bytes, m.pixels.bytes + m.pixels.size), b64(b, "pixels"));
    EXPECT_EQ(m.pixels.size, m.width * m.height * 3) << "RGB565A8";
}

TEST(GoldenVectors, EnvelopeTextBitmap)
{
    const json j = read_json("envelope_text_bitmap");
    const s3w_v1_Envelope env = decode_round_trip("envelope_text_bitmap");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_text_bitmap_tag);

    const s3w_v1_TextBitmap &m = env.body.text_bitmap;
    const json &b = j["textBitmap"];
    EXPECT_EQ(m.bitmap_id, u32(b, "bitmapId"));
    EXPECT_EQ(m.width, u32(b, "width"));
    EXPECT_EQ(m.height, u32(b, "height"));
    EXPECT_EQ(m.y, u32(b, "y"));
    EXPECT_EQ(std::vector<uint8_t>(m.pixels.bytes, m.pixels.bytes + m.pixels.size), b64(b, "pixels"));
    EXPECT_EQ(m.pixels.size % (m.width / 2), 0U) << "whole rows";
}

void expect_media_state(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_media_state_tag);

    const s3w_v1_MediaState &m = env.body.media_state;
    const json &b = j["mediaState"];
    EXPECT_EQ(std::string(m.app), str(b, "app"));
    EXPECT_EQ(std::string(m.title), str(b, "title"));
    EXPECT_EQ(std::string(m.artist), str(b, "artist"));
    EXPECT_EQ(std::string(m.album), str(b, "album"));
    EXPECT_EQ(m.playing, b.value("playing", false));
    EXPECT_EQ(m.position_ms, u32(b, "positionMs"));
    EXPECT_EQ(m.duration_ms, u32(b, "durationMs"));
    EXPECT_EQ(m.volume, u32(b, "volume"));
    EXPECT_EQ(m.volume_max, u32(b, "volumeMax"));
    EXPECT_EQ(m.artwork_hash, u32(b, "artworkHash"));
    EXPECT_EQ(m.text_bitmap_id, u32(b, "textBitmapId"));
}

TEST(GoldenVectors, EnvelopeMediaStateBitmap)
{
    expect_media_state("envelope_media_state_bitmap");
    EXPECT_EQ(decode_round_trip("envelope_media_state_bitmap").body.media_state.text_bitmap_id, 7U);
}

TEST(GoldenVectors, EnvelopeMediaTextBitmap)
{
    const json j = read_json("envelope_media_text_bitmap");
    const s3w_v1_Envelope env = decode_round_trip("envelope_media_text_bitmap");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_media_text_bitmap_tag);
    const s3w_v1_TextBitmap &m = env.body.media_text_bitmap;
    const json &b = j["mediaTextBitmap"];
    EXPECT_EQ(m.bitmap_id, u32(b, "bitmapId"));
    EXPECT_EQ(m.width, u32(b, "width"));
    EXPECT_EQ(m.height, u32(b, "height"));
    EXPECT_EQ(m.y, u32(b, "y"));
    EXPECT_EQ(std::vector<uint8_t>(m.pixels.bytes, m.pixels.bytes + m.pixels.size), b64(b, "pixels"));
}

TEST(GoldenVectors, EnvelopeMediaState)
{
    expect_media_state("envelope_media_state");
}

TEST(GoldenVectors, EnvelopeMediaStateNone)
{
    expect_media_state("envelope_media_state_none");
    EXPECT_STREQ(decode_round_trip("envelope_media_state_none").body.media_state.app, "") << "no session";
}

TEST(GoldenVectors, EnvelopeMediaCommand)
{
    const json j = read_json("envelope_media_command");
    const s3w_v1_Envelope env = decode_round_trip("envelope_media_command");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_media_command_tag);
    EXPECT_EQ(env.body.media_command.cmd, u32(j["mediaCommand"], "cmd"));
    EXPECT_EQ(env.body.media_command.cmd, (uint32_t)s3w_v1_MediaCmd_MEDIA_SET_VOL);
    EXPECT_EQ(env.body.media_command.value, u32(j["mediaCommand"], "value"));
}

void expect_media_artwork(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_media_artwork_tag);

    const s3w_v1_MediaArtwork &m = env.body.media_artwork;
    const json &b = j["mediaArtwork"];
    EXPECT_EQ(m.artwork_hash, u32(b, "artworkHash"));
    EXPECT_EQ(m.width, u32(b, "width"));
    EXPECT_EQ(m.height, u32(b, "height"));
    EXPECT_EQ(std::vector<uint8_t>(m.jpeg.bytes, m.jpeg.bytes + m.jpeg.size), b64(b, "jpeg"));
}

TEST(GoldenVectors, EnvelopeMediaArtwork)
{
    expect_media_artwork("envelope_media_artwork");
}

TEST(GoldenVectors, EnvelopeMediaArtworkRequest)
{
    expect_media_artwork("envelope_media_artwork_request");
    EXPECT_EQ(decode_round_trip("envelope_media_artwork_request").body.media_artwork.jpeg.size, 0U) << "hash only";
}

int32_t i32(const json &j, const char *key)
{
    return j.value(key, 0);
}

void expect_weather(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_weather_tag);

    const s3w_v1_WeatherUpdate &w = env.body.weather;
    const json &b = j["weather"];
    EXPECT_EQ(w.fetched_ms, std::stoll(str(b, "fetchedMs")));
    EXPECT_EQ(std::string(w.location), str(b, "location"));
    ASSERT_EQ(w.has_current, b.contains("current"));
    const json c = b.value("current", json::object());
    EXPECT_EQ(w.current.temp_dc, i32(c, "tempDc"));
    EXPECT_EQ(w.current.feels_dc, i32(c, "feelsDc"));
    EXPECT_EQ(w.current.condition, u32(c, "condition"));
    EXPECT_EQ(w.current.is_day, c.value("isDay", false));
    EXPECT_EQ(w.current.humidity_pct, u32(c, "humidityPct"));
    EXPECT_EQ(w.current.wind_kmh, u32(c, "windKmh"));
    EXPECT_EQ(w.current.uv_x10, u32(c, "uvX10"));
    EXPECT_EQ(w.current.aqi, u32(c, "aqi"));

    const json hours = b.value("hourly", json::array());
    ASSERT_EQ(w.hourly_count, hours.size());
    for (size_t i = 0; i < hours.size(); i++) {
        const s3w_v1_WeatherUpdate_Hour &h = w.hourly[i];
        EXPECT_EQ(h.time, u32(hours[i], "time"));
        EXPECT_EQ(h.temp_dc, i32(hours[i], "tempDc"));
        EXPECT_EQ(h.condition, u32(hours[i], "condition"));
        EXPECT_EQ(h.is_day, hours[i].value("isDay", false));
        EXPECT_EQ(h.precip_pct, u32(hours[i], "precipPct"));
    }
    const json days = b.value("daily", json::array());
    ASSERT_EQ(w.daily_count, days.size());
    for (size_t i = 0; i < days.size(); i++) {
        const s3w_v1_WeatherUpdate_Day &d = w.daily[i];
        EXPECT_EQ(d.date, u32(days[i], "date"));
        EXPECT_EQ(d.lo_dc, i32(days[i], "loDc"));
        EXPECT_EQ(d.hi_dc, i32(days[i], "hiDc"));
        EXPECT_EQ(d.condition, u32(days[i], "condition"));
        EXPECT_EQ(d.precip_pct, u32(days[i], "precipPct"));
        EXPECT_EQ(d.sunrise, u32(days[i], "sunrise"));
        EXPECT_EQ(d.sunset, u32(days[i], "sunset"));
        EXPECT_EQ(d.uv_max_x10, u32(days[i], "uvMaxX10"));
    }
}

TEST(GoldenVectors, EnvelopeWeather)
{
    expect_weather("envelope_weather");
    const s3w_v1_WeatherUpdate w = decode_round_trip("envelope_weather").body.weather;
    EXPECT_EQ(w.hourly[2].temp_dc, -15) << "negative temperatures (sint32)";
    EXPECT_EQ(w.daily[1].lo_dc, -48);
    EXPECT_EQ(w.current.condition, (uint32_t)s3w_v1_WeatherCondition_WEATHER_PARTLY_CLOUDY);
}

TEST(GoldenVectors, EnvelopeWeatherMinimal)
{
    expect_weather("envelope_weather_minimal");
    EXPECT_EQ(decode_round_trip("envelope_weather_minimal").body.weather.hourly_count, 0U) << "current only";
}

void expect_calendar(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_calendar_tag);

    const s3w_v1_CalendarUpdate &c = env.body.calendar;
    const json &b = j["calendar"];
    EXPECT_EQ(c.synced_ms, std::stoll(str(b, "syncedMs")));
    const json events = b.value("events", json::array());
    ASSERT_EQ(c.events_count, events.size());
    for (size_t i = 0; i < events.size(); i++) {
        const s3w_v1_CalendarUpdate_Event &e = c.events[i];
        EXPECT_EQ(e.id, u32(events[i], "id"));
        EXPECT_EQ(std::string(e.title), str(events[i], "title"));
        EXPECT_EQ(e.start, u32(events[i], "start"));
        EXPECT_EQ(e.end, u32(events[i], "end"));
        EXPECT_EQ(e.all_day, events[i].value("allDay", false));
        EXPECT_EQ(std::string(e.location), str(events[i], "location"));
        EXPECT_EQ(e.color, u32(events[i], "color"));
    }
}

TEST(GoldenVectors, EnvelopeCalendar)
{
    expect_calendar("envelope_calendar");
    EXPECT_TRUE(decode_round_trip("envelope_calendar").body.calendar.events[1].all_day);
}

TEST(GoldenVectors, EnvelopeCalendarEmpty)
{
    expect_calendar("envelope_calendar_empty");
    EXPECT_EQ(decode_round_trip("envelope_calendar_empty").body.calendar.events_count, 0U) << "nothing in 48 h";
}

void expect_call_state(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_call_state_tag);

    const s3w_v1_CallState &c = env.body.call_state;
    const json &b = j["callState"];
    EXPECT_EQ(c.call_id, u32(b, "callId"));
    EXPECT_EQ(c.state, u32(b, "state"));
    EXPECT_EQ(std::string(c.name), str(b, "name"));
    EXPECT_EQ(std::string(c.number), str(b, "number"));
    EXPECT_EQ(c.since_ms, std::stoll(str(b, "sinceMs")));
    EXPECT_EQ(c.missed, b.value("missed", false));
    EXPECT_EQ(c.can_control, b.value("canControl", false));
    EXPECT_EQ(c.outgoing, b.value("outgoing", false));
}

TEST(GoldenVectors, EnvelopeCallState)
{
    expect_call_state("envelope_call_state");
    EXPECT_EQ(decode_round_trip("envelope_call_state").body.call_state.state, (uint32_t)s3w_v1_CallPhase_CALL_RINGING);
}

TEST(GoldenVectors, EnvelopeCallStateActive)
{
    expect_call_state("envelope_call_state_active");
    EXPECT_EQ(decode_round_trip("envelope_call_state_active").body.call_state.state, (uint32_t)s3w_v1_CallPhase_CALL_ACTIVE);
}

TEST(GoldenVectors, EnvelopeCallStateMissed)
{
    expect_call_state("envelope_call_state_missed");
    EXPECT_EQ(decode_round_trip("envelope_call_state_missed").body.call_state.state, (uint32_t)s3w_v1_CallPhase_CALL_IDLE);
}

TEST(GoldenVectors, EnvelopeCallCommand)
{
    const json j = read_json("envelope_call_command");
    const s3w_v1_Envelope env = decode_round_trip("envelope_call_command");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_call_command_tag);
    EXPECT_EQ(env.body.call_command.call_id, u32(j["callCommand"], "callId"));
    EXPECT_EQ(env.body.call_command.cmd, u32(j["callCommand"], "cmd"));
    EXPECT_EQ(env.body.call_command.cmd, (uint32_t)s3w_v1_CallCmd_CALL_SILENCE);
}

TEST(GoldenVectors, EnvelopeAppList)
{
    const json j = read_json("envelope_app_list");
    const s3w_v1_Envelope env = decode_round_trip("envelope_app_list");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_app_list_tag);
    const s3w_v1_AppList &l = env.body.app_list;
    const json &b = j["appList"];
    ASSERT_EQ(l.apps_count, b["apps"].size());
    for (pb_size_t i = 0; i < l.apps_count; i++) {
        const json &a = b["apps"][i];
        EXPECT_EQ(std::string(l.apps[i].id), str(a, "id"));
        EXPECT_EQ(std::string(l.apps[i].name), str(a, "name"));
        EXPECT_EQ(std::string(l.apps[i].version), str(a, "version"));
        EXPECT_EQ(std::string(l.apps[i].category), str(a, "category"));
        EXPECT_EQ(l.apps[i].system, a.value("system", false));
    }
    EXPECT_EQ(l.storage_free_kb, u32(b, "storageFreeKb"));
    EXPECT_EQ(l.api_level, u32(b, "apiLevel"));
}

TEST(GoldenVectors, EnvelopeAppListRequest)
{
    const json j = read_json("envelope_app_list_request");
    const s3w_v1_Envelope env = decode_round_trip("envelope_app_list_request");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_app_list_tag);
    EXPECT_EQ(env.body.app_list.apps_count, 0U);
}

TEST(GoldenVectors, EnvelopeAppInstall)
{
    const json j = read_json("envelope_app_install");
    const s3w_v1_Envelope env = decode_round_trip("envelope_app_install");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_app_install_tag);
    const s3w_v1_AppInstallBegin &m = env.body.app_install;
    const json &b = j["appInstall"];
    EXPECT_EQ(std::string(m.app_id), str(b, "appId"));
    EXPECT_EQ(std::string(m.version), str(b, "version"));
    EXPECT_EQ(m.size, u32(b, "size"));
    EXPECT_EQ(std::vector<uint8_t>(m.sha256, m.sha256 + sizeof m.sha256), b64(b, "sha256"));
}

void expect_app_cmd(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_app_cmd_tag);
    const s3w_v1_AppCommand &c = env.body.app_cmd;
    const json &b = j["appCmd"];
    EXPECT_EQ(c.cmd, u32(b, "cmd"));
    EXPECT_EQ(std::string(c.app_id), str(b, "appId"));
    EXPECT_EQ(c.install, b.value("install", false));
    ASSERT_EQ(c.has_result, b.contains("result"));
    if (c.has_result) {
        EXPECT_EQ(c.result.code, b["result"].value("code", 0));
        EXPECT_EQ(std::string(c.result.message), str(b["result"], "message"));
    }
    EXPECT_EQ(std::string(c.version), str(b, "version"));
}

TEST(GoldenVectors, EnvelopeAppCmdUninstall)
{
    expect_app_cmd("envelope_app_cmd_uninstall");
    EXPECT_EQ(decode_round_trip("envelope_app_cmd_uninstall").body.app_cmd.cmd, (uint32_t)s3w_v1_AppCmd_APP_CMD_UNINSTALL);
}

TEST(GoldenVectors, EnvelopeAppCmdResult)
{
    expect_app_cmd("envelope_app_cmd_result");
    EXPECT_EQ(decode_round_trip("envelope_app_cmd_result").body.app_cmd.result.code, s3w_v1_StatusCode_STATUS_OK);
}

TEST(GoldenVectors, EnvelopeAppCmdResultDeclined)
{
    expect_app_cmd("envelope_app_cmd_result_declined");
    EXPECT_EQ(decode_round_trip("envelope_app_cmd_result_declined").body.app_cmd.result.code,
              s3w_v1_StatusCode_STATUS_DENIED);
}

void expect_wifi_config(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_wifi_config_tag);
    const s3w_v1_WifiConfig &m = env.body.wifi_config;
    const json &b = j["wifiConfig"];
    EXPECT_EQ(m.op, u32(b, "op"));
    EXPECT_EQ(std::string(m.ssid), str(b, "ssid"));
    EXPECT_EQ(std::string(m.password), str(b, "password"));
    EXPECT_EQ(m.on, b.value("on", false));
}

TEST(GoldenVectors, EnvelopeWifiConfigAdd)
{
    expect_wifi_config("envelope_wifi_config_add");
    EXPECT_EQ(decode_round_trip("envelope_wifi_config_add").body.wifi_config.op, (uint32_t)s3w_v1_WifiOp_WIFI_OP_ADD);
}

TEST(GoldenVectors, EnvelopeWifiConfigGet)
{
    expect_wifi_config("envelope_wifi_config_get");
    EXPECT_EQ(decode_round_trip("envelope_wifi_config_get").body.wifi_config.op, (uint32_t)s3w_v1_WifiOp_WIFI_OP_GET);
}

void expect_wifi_status(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_wifi_status_tag);
    const s3w_v1_WifiStatus &m = env.body.wifi_status;
    const json &b = j["wifiStatus"];
    EXPECT_EQ(m.on, b.value("on", false));
    EXPECT_EQ(m.state, u32(b, "state"));
    EXPECT_EQ(std::string(m.ssid), str(b, "ssid"));
    EXPECT_EQ(m.rssi, b.value("rssi", 0));
    EXPECT_EQ(std::string(m.ip), str(b, "ip"));
    EXPECT_EQ(m.error, u32(b, "error"));
    EXPECT_EQ(std::string(m.error_ssid), str(b, "errorSsid"));
    const json saved = b.value("saved", json::array());
    ASSERT_EQ(m.saved_count, saved.size());
    for (size_t i = 0; i < saved.size(); i++) {
        EXPECT_EQ(std::string(m.saved[i]), saved[i].get<std::string>());
    }
}

TEST(GoldenVectors, EnvelopeWifiStatus)
{
    expect_wifi_status("envelope_wifi_status");
    EXPECT_EQ(decode_round_trip("envelope_wifi_status").body.wifi_status.rssi, -58) << "sint32";
}

TEST(GoldenVectors, EnvelopeWifiStatusAuth)
{
    expect_wifi_status("envelope_wifi_status_auth");
    EXPECT_EQ(decode_round_trip("envelope_wifi_status_auth").body.wifi_status.error,
              (uint32_t)s3w_v1_WifiError_WIFI_ERR_AUTH);
}

void expect_ha_config(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_ha_config_tag);
    const s3w_v1_HaConfig &m = env.body.ha_config;
    const json &b = j["haConfig"];
    EXPECT_EQ(std::string(m.url), str(b, "url"));
    EXPECT_EQ(std::string(m.token), str(b, "token"));
    const json ents = b.value("entities", json::array());
    ASSERT_EQ(m.entities_count, ents.size());
    for (size_t i = 0; i < ents.size(); i++) {
        EXPECT_EQ(std::string(m.entities[i].entity_id), str(ents[i], "entityId"));
        EXPECT_EQ(std::string(m.entities[i].name), str(ents[i], "name"));
        EXPECT_EQ(m.entities[i].kind, u32(ents[i], "kind"));
    }
}

TEST(GoldenVectors, EnvelopeHaConfig)
{
    expect_ha_config("envelope_ha_config");
    const s3w_v1_Envelope env = decode_round_trip("envelope_ha_config");
    EXPECT_EQ(env.body.ha_config.entities[0].kind, (uint32_t)s3w_v1_HaKind_HA_KIND_TOGGLE);
    EXPECT_EQ(env.body.ha_config.entities[1].kind, (uint32_t)s3w_v1_HaKind_HA_KIND_SENSOR);
}

TEST(GoldenVectors, EnvelopeHaConfigClear)
{
    expect_ha_config("envelope_ha_config_clear");
    EXPECT_EQ(decode_round_trip("envelope_ha_config_clear").body.ha_config.url[0], '\0');
}

TEST(GoldenVectors, EnvelopeHaCommand)
{
    const json j = read_json("envelope_ha_command");
    const s3w_v1_Envelope env = decode_round_trip("envelope_ha_command");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_ha_command_tag);
    EXPECT_EQ(env.body.ha_command.op, (uint32_t)s3w_v1_HaOp_HA_OP_TOGGLE);
    EXPECT_EQ(std::string(env.body.ha_command.entity_id), str(j["haCommand"], "entityId"));
}

void expect_ha_states(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_ha_states_tag);
    const s3w_v1_HaStates &m = env.body.ha_states;
    const json st = j["haStates"].value("states", json::array());
    ASSERT_EQ(m.states_count, st.size());
    for (size_t i = 0; i < st.size(); i++) {
        EXPECT_EQ(std::string(m.states[i].entity_id), str(st[i], "entityId"));
        EXPECT_EQ(std::string(m.states[i].state), str(st[i], "state"));
        EXPECT_EQ(std::string(m.states[i].unit), str(st[i], "unit"));
    }
}

TEST(GoldenVectors, EnvelopeHaStates)
{
    expect_ha_states("envelope_ha_states");
}

TEST(GoldenVectors, EnvelopeHaStatesDenied)
{
    expect_ha_states("envelope_ha_states_denied");
    EXPECT_EQ(decode_round_trip("envelope_ha_states_denied").status.code, s3w_v1_StatusCode_STATUS_DENIED);
}

TEST(GoldenVectors, EnvelopeScreenshot)
{
    const json j = read_json("envelope_screenshot");
    const s3w_v1_Envelope env = decode_round_trip("envelope_screenshot");
    expect_header(env, j);
    EXPECT_EQ(env.which_body, s3w_v1_Envelope_screenshot_tag);
    EXPECT_EQ(s3w_v1_TransferKind_TRANSFER_SCREENSHOT_UP, 7);
}

TEST(GoldenVectors, EnvelopeLicenseInstall)
{
    const json j = read_json("envelope_license_install");
    const s3w_v1_Envelope env = decode_round_trip("envelope_license_install");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_license_install_tag);
    const s3w_v1_LicenseInstall &m = env.body.license_install;
    EXPECT_EQ(std::vector<uint8_t>(m.license.bytes, m.license.bytes + m.license.size), b64(j["licenseInstall"], "license"));
    EXPECT_EQ(m.license.size, 112u); // LICENSE_FILE_SIZE
}

TEST(GoldenVectors, EnvelopeLicenseInstallQuery)
{
    const json j = read_json("envelope_license_install_query");
    const s3w_v1_Envelope env = decode_round_trip("envelope_license_install_query");
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_license_install_tag);
    EXPECT_EQ(env.body.license_install.license.size, 0u);
}

void expect_license_status(const char *name)
{
    const json j = read_json(name);
    const s3w_v1_Envelope env = decode_round_trip(name);
    expect_header(env, j);
    ASSERT_EQ(env.which_body, s3w_v1_Envelope_license_status_tag);
    const s3w_v1_LicenseStatus &m = env.body.license_status;
    const json &b = j["licenseStatus"];
    EXPECT_EQ(m.result, u32(b, "result"));
    EXPECT_EQ(m.pro, b.value("pro", false));
    EXPECT_EQ(m.kind, u32(b, "kind"));
    EXPECT_EQ(m.issued, std::stoll(b.value("issued", std::string("0"))));
    EXPECT_EQ(m.expires, std::stoll(b.value("expires", std::string("0"))));
    EXPECT_EQ(std::vector<uint8_t>(m.mac.bytes, m.mac.bytes + m.mac.size), b64(b, "mac"));
    EXPECT_EQ(std::vector<uint8_t>(m.order.bytes, m.order.bytes + m.order.size), b64(b, "order"));
}

TEST(GoldenVectors, EnvelopeLicenseStatus)
{
    expect_license_status("envelope_license_status");
}

TEST(GoldenVectors, EnvelopeLicenseStatusRefused)
{
    expect_license_status("envelope_license_status_refused");
    EXPECT_EQ(decode_round_trip("envelope_license_status_refused").status.code, s3w_v1_StatusCode_STATUS_INVALID);
}

} // namespace
