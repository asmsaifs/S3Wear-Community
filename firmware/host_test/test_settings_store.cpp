// Settings store (components/svc_settings/settings_store.c) against a fake NVS.
#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <variant>

#include "settings_store.h"

namespace {

// Fake NVS: typed entries, pending writes become visible to a "reboot" only after commit.
struct FakeNvs {
    using Value = std::variant<int32_t, std::string>;
    std::map<std::string, Value> live;      // what get_* sees (NVS reads see uncommitted writes)
    std::map<std::string, Value> committed; // what survives a reboot
    std::set<std::string> fail_keys;        // set_* on these keys fails
    bool fail_commit = false;
    int writes = 0;
    int commits = 0;

    void reboot() { live = committed; }
};

esp_err_t fake_get_i32(void *ctx, const char *key, int32_t *out)
{
    auto *n = static_cast<FakeNvs *>(ctx);
    auto it = n->live.find(key);
    if (it == n->live.end() || !std::holds_alternative<int32_t>(it->second)) {
        return ESP_ERR_NOT_FOUND; // real NVS: lookups are by key and type
    }
    *out = std::get<int32_t>(it->second);
    return ESP_OK;
}

esp_err_t fake_set_i32(void *ctx, const char *key, int32_t v)
{
    auto *n = static_cast<FakeNvs *>(ctx);
    if (n->fail_keys.count(key)) {
        return ESP_FAIL;
    }
    n->writes++;
    n->live[key] = v;
    return ESP_OK;
}

esp_err_t fake_get_str(void *ctx, const char *key, char *out, size_t *len)
{
    auto *n = static_cast<FakeNvs *>(ctx);
    auto it = n->live.find(key);
    if (it == n->live.end() || !std::holds_alternative<std::string>(it->second)) {
        return ESP_ERR_NOT_FOUND;
    }
    const std::string &s = std::get<std::string>(it->second);
    if (s.size() + 1 > *len) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::memcpy(out, s.c_str(), s.size() + 1);
    *len = s.size() + 1;
    return ESP_OK;
}

esp_err_t fake_set_str(void *ctx, const char *key, const char *v)
{
    auto *n = static_cast<FakeNvs *>(ctx);
    if (n->fail_keys.count(key)) {
        return ESP_FAIL;
    }
    n->writes++;
    n->live[key] = std::string(v);
    return ESP_OK;
}

esp_err_t fake_commit(void *ctx)
{
    auto *n = static_cast<FakeNvs *>(ctx);
    if (n->fail_commit) {
        return ESP_FAIL;
    }
    n->commits++;
    n->committed = n->live;
    return ESP_OK;
}

esp_err_t fake_erase_all(void *ctx)
{
    auto *n = static_cast<FakeNvs *>(ctx);
    n->live.clear();
    n->committed.clear();
    return ESP_OK;
}

class Settings : public ::testing::Test {
protected:
    void SetUp() override
    {
        be = {fake_get_i32, fake_set_i32, fake_get_str, fake_set_str, fake_commit, fake_erase_all, &nvs};
        EXPECT_EQ(settings_store_load(&s, &be), 0);
    }
    // Simulated power cycle: only committed data survives, RAM is rebuilt from NVS.
    int reboot()
    {
        nvs.reboot();
        std::memset(&s, 0xA5, sizeof s);
        return settings_store_load(&s, &be);
    }
    FakeNvs nvs;
    settings_backend_t be{};
    settings_store_t s{};
};

} // namespace

TEST_F(Settings, SchemaKeysAreUniqueAndFitNvs)
{
    std::set<std::string> keys;
    for (int i = 0; i < S3W_SETTING_COUNT; i++) {
        const s3w_setting_info_t *in = settings_info(static_cast<s3w_setting_t>(i));
        ASSERT_NE(in, nullptr);
        EXPECT_LE(std::strlen(in->key), static_cast<size_t>(S3W_SETTINGS_KEY_MAX)) << in->key;
        EXPECT_TRUE(keys.insert(in->key).second) << "duplicate key " << in->key;
        if (in->type != S3W_SETTING_TYPE_STR) {
            EXPECT_GE(in->def, in->min) << in->key;
            EXPECT_LE(in->def, in->max) << in->key;
        }
    }
    EXPECT_EQ(settings_info(S3W_SETTING_COUNT), nullptr);
}

TEST_F(Settings, StringsDoNotOverlap)
{
    // Fill every string to its max length, then check each kept its own value.
    for (int i = 0; i < S3W_SETTING_COUNT; i++) {
        const auto id = static_cast<s3w_setting_t>(i);
        const s3w_setting_info_t *in = settings_info(id);
        if (in->type == S3W_SETTING_TYPE_STR) {
            ASSERT_EQ(settings_store_set_str(&s, id, std::string(in->max, char('a' + i)).c_str(), nullptr), ESP_OK);
        }
    }
    for (int i = 0; i < S3W_SETTING_COUNT; i++) {
        const auto id = static_cast<s3w_setting_t>(i);
        const s3w_setting_info_t *in = settings_info(id);
        if (in->type == S3W_SETTING_TYPE_STR) {
            EXPECT_EQ(std::string(settings_store_get_str(&s, id)), std::string(in->max, char('a' + i))) << in->key;
        }
    }
}

TEST_F(Settings, EmptyNvsGivesDefaultsAndWritesNothing)
{
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS), 60);
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_SCREEN_TIMEOUT_S), 10);
    EXPECT_FALSE(settings_store_get_bool(&s, S3W_SETTING_AOD));
    EXPECT_TRUE(settings_store_get_bool(&s, S3W_SETTING_RAISE_TO_WAKE));
    EXPECT_STREQ(settings_store_get_str(&s, S3W_SETTING_TIMEZONE), "UTC0");
    EXPECT_STREQ(settings_store_get_str(&s, S3W_SETTING_LANGUAGE), "en");
    EXPECT_FALSE(settings_store_any_dirty(&s));
    EXPECT_EQ(settings_store_flush(&s, &be), ESP_OK);
    EXPECT_EQ(nvs.writes, 0);
    EXPECT_EQ(nvs.commits, 0);
}

TEST_F(Settings, SetFlushRebootRoundTrip)
{
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS, 85, nullptr), ESP_OK);
    EXPECT_EQ(settings_store_set_bool(&s, S3W_SETTING_AOD, true, nullptr), ESP_OK);
    EXPECT_EQ(settings_store_set_str(&s, S3W_SETTING_TIMEZONE, "CET-1CEST,M3.5.0,M10.5.0/3", nullptr), ESP_OK);
    EXPECT_EQ(settings_store_flush(&s, &be), ESP_OK);
    EXPECT_EQ(nvs.writes, 3);
    EXPECT_EQ(nvs.commits, 1);
    EXPECT_FALSE(settings_store_any_dirty(&s));

    EXPECT_EQ(reboot(), 0);
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS), 85);
    EXPECT_TRUE(settings_store_get_bool(&s, S3W_SETTING_AOD));
    EXPECT_STREQ(settings_store_get_str(&s, S3W_SETTING_TIMEZONE), "CET-1CEST,M3.5.0,M10.5.0/3");
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_SCREEN_TIMEOUT_S), 10); // untouched -> default
    EXPECT_FALSE(settings_store_any_dirty(&s));
}

TEST_F(Settings, UnflushedChangesAreLostOnReboot)
{
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_VOLUME_MEDIA, 10, nullptr), ESP_OK);
    reboot();
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_VOLUME_MEDIA), 60);
}

TEST_F(Settings, ChangedFlagAndNoWriteForSameValue)
{
    bool changed = true;
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS, 60, &changed), ESP_OK);
    EXPECT_FALSE(changed);
    EXPECT_EQ(settings_store_set_str(&s, S3W_SETTING_LANGUAGE, "en", &changed), ESP_OK);
    EXPECT_FALSE(changed);
    EXPECT_FALSE(settings_store_any_dirty(&s));

    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS, 61, &changed), ESP_OK);
    EXPECT_TRUE(changed);
    EXPECT_TRUE(settings_store_is_dirty(&s, S3W_SETTING_DISPLAY_BRIGHTNESS));
    EXPECT_FALSE(settings_store_is_dirty(&s, S3W_SETTING_SCREEN_TIMEOUT_S));

    // Changing back before the flush still writes once (RAM differs from what may be stored).
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS, 60, &changed), ESP_OK);
    EXPECT_TRUE(changed);
    EXPECT_EQ(settings_store_flush(&s, &be), ESP_OK);
    EXPECT_EQ(nvs.writes, 1);
}

TEST_F(Settings, RejectsBadValues)
{
    bool changed = true;
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS, 4, &changed), ESP_ERR_INVALID_ARG);
    EXPECT_FALSE(changed);
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS, 101, nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS, 5, nullptr), ESP_OK);   // min
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS, 100, nullptr), ESP_OK); // max
    // Wrong type
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_AOD, 1, nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(settings_store_set_bool(&s, S3W_SETTING_DISPLAY_BRIGHTNESS, true, nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(settings_store_set_str(&s, S3W_SETTING_DISPLAY_BRIGHTNESS, "x", nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_TIMEZONE, 1, nullptr), ESP_ERR_INVALID_ARG);
    // Unknown id, NULL string, too long
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_COUNT, 1, nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(settings_store_set_str(&s, S3W_SETTING_LANGUAGE, nullptr, nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(settings_store_set_str(&s, S3W_SETTING_LANGUAGE, "12345678", nullptr), ESP_ERR_INVALID_SIZE);
    EXPECT_EQ(settings_store_set_str(&s, S3W_SETTING_LANGUAGE, "1234567", nullptr), ESP_OK);
    EXPECT_EQ(settings_store_set_str(&s, S3W_SETTING_LANGUAGE, "", nullptr), ESP_OK);
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS), 100);
    // Getters with the wrong type return zero values
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_AOD), 0);
    EXPECT_FALSE(settings_store_get_bool(&s, S3W_SETTING_RAISE_SENSITIVITY));
    EXPECT_STREQ(settings_store_get_str(&s, S3W_SETTING_DISPLAY_BRIGHTNESS), "");
    EXPECT_STREQ(settings_store_get_str(&s, S3W_SETTING_COUNT), "");
}

TEST_F(Settings, InvalidStoredValuesFallBackAndAreRepaired)
{
    nvs.committed["disp_bright"] = int32_t{250};          // out of range
    nvs.committed["disp_aod"] = int32_t{7};               // bool must be 0/1
    nvs.committed["vol_media"] = std::string("loud");     // wrong type: not found as i32
    nvs.committed["lang"] = std::string("much-too-long"); // longer than max_len 7
    nvs.committed["disp_timeout"] = int32_t{30};          // valid
    nvs.committed["old_key"] = int32_t{1};                // unknown keys are ignored

    EXPECT_EQ(reboot(), 3); // disp_bright, disp_aod, lang
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS), 60);
    EXPECT_FALSE(settings_store_get_bool(&s, S3W_SETTING_AOD));
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_VOLUME_MEDIA), 60);
    EXPECT_STREQ(settings_store_get_str(&s, S3W_SETTING_LANGUAGE), "en");
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_SCREEN_TIMEOUT_S), 30);
    EXPECT_TRUE(settings_store_is_dirty(&s, S3W_SETTING_DISPLAY_BRIGHTNESS));
    EXPECT_FALSE(settings_store_is_dirty(&s, S3W_SETTING_VOLUME_MEDIA)); // type mismatch reads as missing

    EXPECT_EQ(settings_store_flush(&s, &be), ESP_OK);
    EXPECT_EQ(reboot(), 0);
    EXPECT_EQ(std::get<int32_t>(nvs.committed["disp_bright"]), 60);
    EXPECT_EQ(std::get<std::string>(nvs.committed["lang"]), "en");
}

TEST_F(Settings, FailedWriteStaysDirty)
{
    nvs.fail_keys.insert("vol_media");
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_VOLUME_MEDIA, 20, nullptr), ESP_OK);
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_VOLUME_SYSTEM, 30, nullptr), ESP_OK);
    EXPECT_EQ(settings_store_flush(&s, &be), ESP_FAIL);
    EXPECT_TRUE(settings_store_is_dirty(&s, S3W_SETTING_VOLUME_MEDIA));
    EXPECT_FALSE(settings_store_is_dirty(&s, S3W_SETTING_VOLUME_SYSTEM));
    EXPECT_EQ(nvs.commits, 1); // the good key is still committed

    nvs.fail_keys.clear();
    EXPECT_EQ(settings_store_flush(&s, &be), ESP_OK);
    EXPECT_FALSE(settings_store_any_dirty(&s));
    reboot();
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_VOLUME_MEDIA), 20);
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_VOLUME_SYSTEM), 30);
}

TEST_F(Settings, FailedCommitKeepsEverythingDirty)
{
    nvs.fail_commit = true;
    EXPECT_EQ(settings_store_set_bool(&s, S3W_SETTING_DND, true, nullptr), ESP_OK);
    EXPECT_EQ(settings_store_flush(&s, &be), ESP_FAIL);
    EXPECT_TRUE(settings_store_is_dirty(&s, S3W_SETTING_DND));
    nvs.fail_commit = false;
    EXPECT_EQ(settings_store_flush(&s, &be), ESP_OK);
    reboot();
    EXPECT_TRUE(settings_store_get_bool(&s, S3W_SETTING_DND));
}

TEST_F(Settings, FactoryResetRestoresDefaults)
{
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS, 90, nullptr), ESP_OK);
    EXPECT_EQ(settings_store_set_str(&s, S3W_SETTING_LANGUAGE, "de", nullptr), ESP_OK);
    EXPECT_EQ(settings_store_flush(&s, &be), ESP_OK);

    // What svc_settings_factory_reset does: defaults in RAM, then erase the namespace.
    settings_store_defaults(&s);
    EXPECT_EQ(be.erase_all(be.ctx), ESP_OK);
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS), 60);
    EXPECT_STREQ(settings_store_get_str(&s, S3W_SETTING_LANGUAGE), "en");
    EXPECT_FALSE(settings_store_any_dirty(&s));

    EXPECT_EQ(reboot(), 0);
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_DISPLAY_BRIGHTNESS), 60);
    EXPECT_STREQ(settings_store_get_str(&s, S3W_SETTING_LANGUAGE), "en");
}

TEST_F(Settings, SnapshotFlushLeavesLiveStoreAlone)
{
    // svc_settings flushes a copy taken under its lock; later sets mark the live store only.
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_HAPTICS_LEVEL, 3, nullptr), ESP_OK);
    settings_store_t snap = s;
    std::memset(s.dirty, 0, sizeof s.dirty);
    EXPECT_EQ(settings_store_set_int(&s, S3W_SETTING_HAPTICS_LEVEL, 0, nullptr), ESP_OK);
    EXPECT_EQ(settings_store_flush(&snap, &be), ESP_OK);
    EXPECT_TRUE(settings_store_is_dirty(&s, S3W_SETTING_HAPTICS_LEVEL));
    EXPECT_EQ(settings_store_flush(&s, &be), ESP_OK);
    reboot();
    EXPECT_EQ(settings_store_get_int(&s, S3W_SETTING_HAPTICS_LEVEL), 0);
}

TEST_F(Settings, FindByKey)
{
    s3w_setting_t id;
    ASSERT_EQ(settings_find("tz", &id), ESP_OK);
    EXPECT_EQ(id, S3W_SETTING_TIMEZONE);
    EXPECT_EQ(settings_find("nope", &id), ESP_ERR_NOT_FOUND);
    EXPECT_EQ(settings_find(nullptr, &id), ESP_ERR_NOT_FOUND);
}
