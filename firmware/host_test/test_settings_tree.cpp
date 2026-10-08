// Settings app tree (ui_apps/settings_tree.c) against the settings schema: every row
// must point at a setting of the right type, with values the schema accepts.
#include <gtest/gtest.h>

#include <set>
#include <string>

extern "C" {
#include "settings_store.h"
#include "settings_tree.h"
}

namespace {

void check(const settings_node_t *n, int depth, std::set<int> *seen)
{
    ASSERT_NE(n->title, nullptr);
    ASSERT_STRNE(n->title, "");
    ASSERT_LE(depth, 4) << n->title;
    switch (n->kind) {
    case SETTINGS_PAGE: {
        ASSERT_GT(n->n_children, 0) << n->title;
        std::set<std::string> titles;
        for (int i = 0; i < n->n_children; i++) {
            EXPECT_TRUE(titles.insert(n->children[i].title).second) << "duplicate row " << n->children[i].title << " in " << n->title;
            check(&n->children[i], depth + 1, seen);
        }
        break;
    }
    case SETTINGS_TOGGLE:
    case SETTINGS_SLIDER:
    case SETTINGS_CHOICE:
    case SETTINGS_TIME:
    case SETTINGS_ZONE: {
        const s3w_setting_info_t *info = settings_info(n->setting);
        ASSERT_NE(info, nullptr) << n->title;
        EXPECT_TRUE(seen->insert(n->setting).second) << n->title << ": setting shown twice";
        if (n->kind == SETTINGS_TOGGLE) {
            EXPECT_EQ(info->type, S3W_SETTING_TYPE_BOOL) << n->title;
        } else if (n->kind == SETTINGS_ZONE) {
            EXPECT_EQ(info->type, S3W_SETTING_TYPE_STR) << n->title;
        } else {
            EXPECT_EQ(info->type, S3W_SETTING_TYPE_INT) << n->title;
        }
        if (n->kind == SETTINGS_SLIDER) {
            EXPECT_LT(n->min, n->max) << n->title;
            EXPECT_GE(n->min, info->min) << n->title;
            EXPECT_LE(n->max, info->max) << n->title;
            EXPECT_GE(info->def, n->min) << n->title;
            EXPECT_LE(info->def, n->max) << n->title;
        }
        if (n->kind == SETTINGS_TIME) {
            EXPECT_EQ(info->min, 0) << n->title;
            EXPECT_EQ(info->max, 1439) << n->title;
        }
        if (n->kind == SETTINGS_CHOICE) {
            ASSERT_GT(n->n_opts, 0) << n->title;
            std::set<int> values;
            for (int i = 0; i < n->n_opts; i++) {
                EXPECT_STRNE(n->opts[i].label, "") << n->title;
                EXPECT_TRUE(values.insert(n->opts[i].value).second) << n->title << ": duplicate value";
                EXPECT_GE(n->opts[i].value, info->min) << n->title;
                EXPECT_LE(n->opts[i].value, info->max) << n->title;
            }
            EXPECT_GE(settings_opt_index(n, info->def), 0) << n->title << ": the default is not an option";
        }
        break;
    }
    case SETTINGS_APP:
        EXPECT_NE(n->arg, nullptr) << n->title;
        break;
    case SETTINGS_ACTION:
        if (n->danger) {
            EXPECT_NE(n->arg, nullptr) << n->title << ": a danger action explains itself";
        }
        break;
    case SETTINGS_INFO:
        if (n->info == SETTINGS_INFO_TEXT) {
            EXPECT_NE(n->arg, nullptr) << n->title;
        }
        break;
    case SETTINGS_SOON:
        break;
    }
}

} // namespace

TEST(SettingsTree, EveryRowMatchesTheSchema)
{
    std::set<int> seen;
    check(settings_tree_root(), 0, &seen);
}

TEST(SettingsTree, OptionLookup)
{
    const settings_node_t *root = settings_tree_root();
    const settings_node_t *display = nullptr;
    for (int i = 0; i < root->n_children; i++) {
        if (std::string(root->children[i].title) == "Display") {
            display = &root->children[i];
        }
    }
    ASSERT_NE(display, nullptr);
    const settings_node_t *timeout = nullptr;
    for (int i = 0; i < display->n_children; i++) {
        if (display->children[i].setting == S3W_SETTING_SCREEN_TIMEOUT_S && display->children[i].kind == SETTINGS_CHOICE) {
            timeout = &display->children[i];
        }
    }
    ASSERT_NE(timeout, nullptr);
    EXPECT_STREQ(settings_opt_label(timeout, 60), "1 min");
    EXPECT_EQ(settings_opt_label(timeout, 61), nullptr);
    EXPECT_EQ(settings_opt_index(timeout, 61), -1);
    EXPECT_EQ(settings_opt_index(nullptr, 60), -1);
}
