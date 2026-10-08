// watchfaces: JSON pull parser (wf_json.h) and the face.json parser (wf_decl.h),
// including malformed and hostile input, and the two sample faces.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "wf_decl.h"
#include "wf_json.h"

namespace {

// --- wf_json ----------------------------------------------------------------------------

bool skip_all(const std::string &s, std::string *err = nullptr)
{
    wf_json_t j;
    wf_json_init(&j, s.data(), s.size());
    const bool ok = wf_json_skip(&j) && wf_json_finish(&j);
    if (err) {
        *err = ok ? "" : wf_json_error(&j);
    }
    return ok;
}

std::string json_error(const std::string &s)
{
    std::string err;
    EXPECT_FALSE(skip_all(s, &err)) << s;
    return err;
}

TEST(WfJson, WalksAnObject)
{
    const std::string s = R"( {"a": 12, "b": [1, -2, 3], "c": {"d": true}, "e": "x\"y", "f": null} )";
    wf_json_t j;
    wf_json_init(&j, s.data(), s.size());
    char key[8];
    int32_t a = 0, sum = 0;
    bool d = false;
    char e[8] = "";
    ASSERT_TRUE(wf_json_obj_begin(&j));
    while (wf_json_obj_next(&j, key, sizeof key)) {
        if (strcmp(key, "a") == 0) {
            ASSERT_TRUE(wf_json_int(&j, &a));
        } else if (strcmp(key, "b") == 0) {
            ASSERT_TRUE(wf_json_arr_begin(&j));
            while (wf_json_arr_next(&j)) {
                int32_t v;
                ASSERT_TRUE(wf_json_int(&j, &v));
                sum += v;
            }
        } else if (strcmp(key, "c") == 0) {
            ASSERT_TRUE(wf_json_obj_begin(&j));
            ASSERT_TRUE(wf_json_obj_next(&j, key, sizeof key));
            ASSERT_TRUE(wf_json_bool(&j, &d));
            ASSERT_FALSE(wf_json_obj_next(&j, key, sizeof key));
        } else if (strcmp(key, "e") == 0) {
            ASSERT_TRUE(wf_json_string(&j, e, sizeof e));
        } else {
            ASSERT_EQ(wf_json_peek(&j), WF_JSON_NULL);
            ASSERT_TRUE(wf_json_skip(&j));
        }
    }
    EXPECT_TRUE(wf_json_finish(&j)) << wf_json_error(&j);
    EXPECT_EQ(a, 12);
    EXPECT_EQ(sum, 2);
    EXPECT_TRUE(d);
    EXPECT_STREQ(e, "x\"y");
}

TEST(WfJson, UnescapesToUtf8)
{
    const std::string s = R"("a\\b\/c\n\u00e9\u20ac\ud83d\ude00")";
    wf_json_t j;
    wf_json_init(&j, s.data(), s.size());
    char buf[32];
    ASSERT_TRUE(wf_json_string(&j, buf, sizeof buf)) << wf_json_error(&j);
    EXPECT_STREQ(buf, "a\\b/c\n\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
}

TEST(WfJson, IntegerLimits)
{
    for (const auto &[text, want] : {std::pair{"0", 0}, {"-0", 0}, {"2147483647", INT32_MAX}, {"-2147483648", INT32_MIN}}) {
        wf_json_t j;
        wf_json_init(&j, text, strlen(text));
        int32_t v = 1;
        EXPECT_TRUE(wf_json_int(&j, &v)) << text;
        EXPECT_EQ(v, want) << text;
    }
    for (const char *text : {"2147483648", "-2147483649", "99999999999999999999", "1.5", "1e3"}) {
        wf_json_t j;
        wf_json_init(&j, text, strlen(text));
        int32_t v;
        EXPECT_FALSE(wf_json_int(&j, &v)) << text;
    }
}

TEST(WfJson, SkipsValidDocuments)
{
    for (const char *s : {"{}", "[]", "[[]]", "0", "-1.25e+10", "\"\"", "true", "false", "null", " {\"a\" : [ {} , 1 ] } ",
                          "[1E-2, 0.5, -0]"}) {
        EXPECT_TRUE(skip_all(s)) << s;
    }
}

TEST(WfJson, RejectsMalformed)
{
    EXPECT_EQ(json_error(""), "unexpected end of input");
    EXPECT_EQ(json_error("{"), "unexpected end of input");
    EXPECT_EQ(json_error("{\"a\":1,}"), "trailing comma");
    EXPECT_EQ(json_error("[1,2,]"), "trailing comma");
    EXPECT_EQ(json_error("[1 2]"), "expected ',' or ']'");
    EXPECT_EQ(json_error("{\"a\" 1}"), "expected ':'");
    EXPECT_EQ(json_error("{\"a\":1 \"b\":2}"), "expected ',' or '}'");
    EXPECT_EQ(json_error("{a:1}"), "expected a key");
    EXPECT_EQ(json_error("\"abc"), "unterminated string");
    EXPECT_EQ(json_error("\"a\tb\""), "control character in string");
    EXPECT_EQ(json_error("\"\\x\""), "bad escape");
    EXPECT_EQ(json_error("\"\\u12\""), "bad \\u escape");
    EXPECT_EQ(json_error("\"\\ud83d\""), "unpaired surrogate");
    EXPECT_EQ(json_error("\"\\ude00\""), "unpaired surrogate");
    EXPECT_EQ(json_error("\"\\u0000\""), "NUL in string");
    EXPECT_EQ(json_error("01"), "unexpected data after the end");
    EXPECT_EQ(json_error("1."), "bad number");
    EXPECT_EQ(json_error("-"), "bad number");
    EXPECT_EQ(json_error("+1"), "expected a value");
    EXPECT_EQ(json_error("tru"), "bad literal");
    EXPECT_EQ(json_error("nul"), "bad literal");
    EXPECT_EQ(json_error("{} {}"), "unexpected data after the end");
    EXPECT_EQ(json_error("'a'"), "expected a value");
    EXPECT_EQ(json_error(std::string(WF_JSON_MAX_DEPTH + 1, '[') + std::string(WF_JSON_MAX_DEPTH + 1, ']')),
              "nested too deep");
    EXPECT_TRUE(skip_all(std::string(WF_JSON_MAX_DEPTH, '[') + std::string(WF_JSON_MAX_DEPTH, ']')));
}

TEST(WfJson, StringTooLongAndPosition)
{
    const std::string s = "{\n  \"key\": \"abcdefgh\"\n}";
    wf_json_t j;
    wf_json_init(&j, s.data(), s.size());
    char key[8], val[4];
    ASSERT_TRUE(wf_json_obj_begin(&j));
    ASSERT_TRUE(wf_json_obj_next(&j, key, sizeof key));
    EXPECT_FALSE(wf_json_string(&j, val, sizeof val));
    EXPECT_STREQ(wf_json_error(&j), "string too long");
    int line, col;
    wf_json_line_col(&j, wf_json_error_pos(&j), &line, &col);
    EXPECT_EQ(line, 2);
    EXPECT_EQ(col, 14);
    // The first error sticks.
    EXPECT_FALSE(wf_json_obj_next(&j, key, sizeof key));
    EXPECT_STREQ(wf_json_error(&j), "string too long");
}

// --- wf_decl ----------------------------------------------------------------------------

std::string read_file(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

class WfDeclTest : public ::testing::Test {
protected:
    bool parse(const std::string &json)
    {
        return wf_decl_parse(json.data(), json.size(), face.get(), &err);
    }

    /** Parse must fail; returns the message. */
    std::string error(const std::string &json)
    {
        EXPECT_FALSE(parse(json)) << json;
        return err.msg;
    }

    /** A face with one element (or extra top-level members before "elements"). */
    static std::string with(const std::string &element, const std::string &extra = "")
    {
        return R"({"id":"t","name":"T","api":1,)" + extra + R"("elements":[)" + element + "]}";
    }

    std::unique_ptr<wf_decl_face_t> face = std::make_unique<wf_decl_face_t>();
    wf_decl_err_t err{};
};

TEST_F(WfDeclTest, SampleNeon)
{
    const std::string json = read_file(S3W_WF_SAMPLES_DIR "/s3w.neon/face.json");
    ASSERT_FALSE(json.empty());
    ASSERT_TRUE(parse(json)) << err.line << ":" << err.col << ": " << err.msg;
    EXPECT_STREQ(face->id, "s3w.neon");
    EXPECT_STREQ(face->name, "Neon");
    EXPECT_STREQ(face->version, "1.0.0");
    EXPECT_EQ(face->elem_n, 6);
    EXPECT_EQ(face->aod_n, 2);
    ASSERT_EQ(face->slot_n, 1);
    EXPECT_STREQ(face->slots[0].id, "bottom");
    EXPECT_EQ(face->slots[0].def, WF_COMP_BATTERY);
    EXPECT_FALSE(face->slots[0].line);
    EXPECT_TRUE(face->deps & WF_DATA_SECOND); // time.ss
    EXPECT_TRUE(face->deps & WF_DATA_STEPS);
    EXPECT_FALSE(face->aod_deps & WF_DATA_SECOND);

    const wf_decl_elem_t &arc = face->elems[0];
    EXPECT_EQ(arc.type, WF_DECL_ARC);
    EXPECT_EQ(arc.r, 195);
    EXPECT_EQ(arc.w, 10);
    EXPECT_EQ(arc.start, 135);
    EXPECT_EQ(arc.end, 405);
    EXPECT_EQ(arc.max, 1000);
    EXPECT_EQ(arc.color, 0x76FF03u);
    EXPECT_EQ(arc.track, 0x1A3300u);
    EXPECT_TRUE(arc.rounded);
    const wf_decl_elem_t &date = face->elems[1];
    EXPECT_EQ(date.type, WF_DECL_TEXT);
    EXPECT_TRUE(date.upper);
    EXPECT_STREQ(date.bind.pattern, "EEE d MMM");
}

TEST_F(WfDeclTest, SampleDial)
{
    const std::string json = read_file(S3W_WF_SAMPLES_DIR "/s3w.dial/face.json");
    ASSERT_TRUE(parse(json)) << err.line << ":" << err.col << ": " << err.msg;
    EXPECT_STREQ(face->id, "s3w.dial");
    EXPECT_EQ(face->elem_n, 11);
    EXPECT_EQ(face->aod_n, 4);
    ASSERT_EQ(face->slot_n, 1);
    EXPECT_EQ(face->slots[0].def, WF_COMP_STEPS);
    EXPECT_EQ(face->slots[0].y, 352);
    const wf_decl_elem_t &ticks = face->elems[0];
    EXPECT_EQ(ticks.type, WF_DECL_TICKS);
    EXPECT_EQ(ticks.count, 60);
    EXPECT_EQ(ticks.major, 5);
    EXPECT_EQ(ticks.x, 205); // default: centre
    EXPECT_EQ(face->elems[1].color, WF_DECL_COLOR_ACCENT);
    EXPECT_STREQ(face->elems[2].text, "12");
    EXPECT_FALSE(face->elems[2].has_bind);
    const wf_decl_elem_t &second = face->elems[8];
    EXPECT_EQ(second.type, WF_DECL_HAND);
    EXPECT_EQ(second.tail, 32);
    // AOD ticks: major length/width default to twice the minor ones.
    EXPECT_EQ(face->aod[0].major_len, 20);
    EXPECT_EQ(face->aod[0].major_w, 8);
}

TEST_F(WfDeclTest, MinimalFaceGetsDefaults)
{
    ASSERT_TRUE(parse(with(R"({"type":"text","bind":"time.hh:mm","x":205,"y":251})"))) << err.msg;
    EXPECT_EQ(face->background, 0u);
    const wf_decl_elem_t &t = face->elems[0];
    EXPECT_EQ(t.font, WF_DECL_FONT_BODY);
    EXPECT_EQ(t.align, WF_DECL_ALIGN_CENTER);
    EXPECT_EQ(t.color, 0xFFFFFFu);
    // No "aod": the time, light and dim.
    ASSERT_EQ(face->aod_n, 1);
    EXPECT_EQ(face->aod[0].font, WF_DECL_FONT_DIGITS_96_LIGHT);
    EXPECT_EQ(face->aod[0].color, 0x9A9AA0u);
    EXPECT_TRUE(face->aod[0].has_bind);
    EXPECT_EQ(face->deps, (uint32_t)WF_DATA_TIME);
}

TEST_F(WfDeclTest, MembersInAnyOrder)
{
    ASSERT_TRUE(parse(R"({"elements":[{"y":10,"x":20,"text":"hi","type":"text","align":"right"}],
                          "aod":{"elements":[]},"api":1,"name":"N","id":"a.b-c_d"})"))
        << err.msg;
    EXPECT_EQ(face->elems[0].x, 20);
    EXPECT_EQ(face->elems[0].align, WF_DECL_ALIGN_RIGHT);
    EXPECT_EQ(face->aod_n, 0); // explicit empty AOD: black
}

TEST_F(WfDeclTest, EveryElementType)
{
    const std::string els = R"(
        {"type":"text","text":"x","x":1,"y":2,"font":"digits_160","color":"accent","upper":true},
        {"type":"arc","bind":"battery.ratio","r":50,"max":500,"track":"none","rounded":false},
        {"type":"hand","bind":"time.minute","image":"min.png","pivot":[4,180]},
        {"type":"circle","x":10,"y":10,"r":5,"w":2},
        {"type":"rect","x":10,"y":10,"w":20,"h":30,"radius":4},
        {"type":"ticks","r":100,"count":12},
        {"type":"image","image":"logo.bin","x":205,"y":60},
        {"type":"complication","slot":"top","x":205,"y":80,"style":"line","default":"moon"})";
    ASSERT_TRUE(parse(with(els, R"("background":"bg.png",)"))) << err.msg;
    EXPECT_EQ(face->elem_n, 8);
    EXPECT_STREQ(face->background_image, "bg.png");
    EXPECT_EQ(face->elems[0].font, WF_DECL_FONT_DIGITS_160);
    EXPECT_EQ(face->elems[1].track, WF_DECL_COLOR_NONE);
    EXPECT_FALSE(face->elems[1].rounded);
    EXPECT_STREQ(face->elems[2].image, "min.png");
    EXPECT_EQ(face->elems[2].pivot_y, 180);
    EXPECT_EQ(face->elems[2].len, 150);
    EXPECT_EQ(face->elems[4].r, 4);
    EXPECT_STREQ(face->elems[6].image, "logo.bin");
    EXPECT_TRUE(face->slots[0].line);
    EXPECT_EQ(face->slots[0].def, WF_COMP_MOON);
}

TEST_F(WfDeclTest, RejectsBrokenJson)
{
    EXPECT_EQ(error(""), "unexpected end of input");
    EXPECT_EQ(error("[]"), "expected an object");
    EXPECT_EQ(error("{\"id\":\"t\",}"), "trailing comma");
    EXPECT_EQ(error(R"({"id":"t","name":"T","api":1,"elements":[{"type":"text" "x":1}]})"),
              "elements[0]: expected ',' or '}'");
    EXPECT_EQ(error(R"({"id":"t","name":"T","api":1,"elements":[{"type":"text","text":"a","x":1,"y":2})"),
              "elements: unexpected end of input");
    EXPECT_EQ(error(R"({"id":"t","name":"T","api":1,"elements":[{"type":"text","text":"a","x":1,"y":2}})"),
              "elements: expected ',' or ']'");
    EXPECT_EQ(error(with(R"({"type":"text","bind":"time.hh:mm","x":205,"y":251})") + "x"),
              "unexpected data after the end");
}

TEST_F(WfDeclTest, ReportsLineAndColumn)
{
    const std::string json = "{\"id\":\"t\",\"name\":\"T\",\"api\":1,\n\"elements\":[\n"
                             "  {\"type\":\"text\",\"text\":\"a\",\"x\":1,\"y\":2,\"font\":\"huge\"}]}";
    EXPECT_EQ(error(json), "elements[0].font: unknown font 'huge'");
    EXPECT_EQ(err.line, 3);
    EXPECT_EQ(err.col, 48); // the value
}

TEST_F(WfDeclTest, RejectsBadTopLevel)
{
    const std::string el = R"({"type":"text","text":"a","x":1,"y":2})";
    EXPECT_EQ(error(R"({"name":"T","api":1,"elements":[)" + el + "]}"), "missing \"id\"");
    EXPECT_EQ(error(R"({"id":"t","api":1,"elements":[)" + el + "]}"), "missing \"name\"");
    EXPECT_EQ(error(R"({"id":"t","name":"T","elements":[)" + el + "]}"), "missing \"api\"");
    EXPECT_EQ(error(R"({"id":"t","name":"T","api":1})"), "missing \"elements\"");
    EXPECT_EQ(error(R"({"id":"t","name":"T","api":1,"elements":[]})"), "elements: empty");
    EXPECT_EQ(error(R"({"id":"t","name":"T","api":1,"elements":{}})"), "elements: expected an array");
    EXPECT_EQ(error(with(el, R"("api":2,)")), "api: duplicate key");
    EXPECT_EQ(error(R"({"id":"t","name":"T","api":2,"elements":[)" + el + "]}"),
              "api: 2 is not supported (this watch: 1)");
    EXPECT_EQ(error(R"({"id":"t","name":"T","api":"1","elements":[)" + el + "]}"), "api: expected an integer");
    EXPECT_EQ(error(with(el, R"("script":"x",)")), "script: unknown key");
    EXPECT_EQ(error(R"({"id":"Neon!","name":"T","api":1,"elements":[)" + el + "]}"),
              "id: 'Neon!' must be 1..47 of [a-z0-9._-]");
    EXPECT_EQ(error(R"({"id":")" + std::string(48, 'a') + R"(","name":"T","api":1,"elements":[)" + el + "]}"),
              "id: string too long");
    EXPECT_EQ(error(R"({"id":"t","name":"","api":1,"elements":[)" + el + "]}"), "name: empty");
    EXPECT_EQ(error(with(el, R"("author":{"x":1},)")), "author: expected a string");
    EXPECT_EQ(error(with(el, R"("background":"#12345",)")),
              "background: '#12345' is not \"#RRGGBB\"");
    EXPECT_EQ(error(with(el, R"("background":"../etc/passwd",)")),
              "background: '../etc/passwd' is neither \"#RRGGBB\" nor a .png or .bin file name");
    EXPECT_EQ(error(with(el, R"("aod":{"elements":[],"background":"#000000"},)")), "aod.background: unknown key");
    EXPECT_EQ(error(with(el, R"("aod":[],)")), "aod: expected an object");
}

TEST_F(WfDeclTest, RejectsBadElements)
{
    EXPECT_EQ(error(with("1")), "elements[0]: expected an object");
    EXPECT_EQ(error(with(R"({"x":1})")), "elements[0]: missing \"type\"");
    EXPECT_EQ(error(with(R"({"type":"lua"})")), "elements[0].type: unknown type 'lua'");
    EXPECT_EQ(error(with(R"({"type":7})")), "elements[0].type: expected a string");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","x":1,"y":2,"r":3})")), "elements[0].r: not a field of text");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","x":1,"y":2,"onTap":"x"})")),
              "elements[0].onTap: not a field of text");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","x":1,"x":2,"y":2})")), "elements[0].x: duplicate key");
    EXPECT_EQ(error(with(R"({"type":"text","x":1,"y":2})")), "elements[0]: text needs \"bind\" or \"text\"");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","bind":"time.hh","x":1,"y":2})")),
              "elements[0]: text has both \"bind\" and \"text\"");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","y":2})")), "elements[0]: text needs \"x\"");
    EXPECT_EQ(error(with(R"({"type":"text","bind":"time.HH","x":1,"y":2})")),
              "elements[0].bind: unknown binding 'time.HH'");
    EXPECT_EQ(error(with(R"({"type":"text","bind":"time.hour","x":1,"y":2})")),
              "elements[0].bind: 'time.hour' has no text form");
    EXPECT_EQ(error(with(R"({"type":"arc","bind":"next_event.title","r":5})")),
              "elements[0].bind: 'next_event.title' has no numeric form");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","x":1,"y":2,"color":"red"})")),
              "elements[0].color: 'red' is not a colour (\"#RRGGBB\" or \"accent\")");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","x":1,"y":2,"color":"none"})")),
              "elements[0].color: 'none' is not a colour (\"#RRGGBB\" or \"accent\")");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","x":1,"y":2,"align":"middle"})")),
              "elements[0].align: 'middle' is not center, left or right");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","x":1000,"y":2})")),
              "elements[0].x: 1000 is out of range (-205..615)");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","x":1.5,"y":2})")), "elements[0].x: expected an integer");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","x":"1","y":2})")), "elements[0].x: expected an integer");
    EXPECT_EQ(error(with(R"({"type":"text","text":")" + std::string(40, 'a') + R"(","x":1,"y":2})")),
              "elements[0].text: string too long");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","x":1,"y":2,"upper":1})")),
              "elements[0].upper: expected true or false");
    EXPECT_EQ(error(with(R"({"type":"arc","bind":"battery.ratio","r":5,"start":0,"end":0})")),
              "elements[0]: arc end - start must be 1..360 degrees");
    EXPECT_EQ(error(with(R"({"type":"arc","bind":"battery.ratio","r":5,"start":0,"end":400})")),
              "elements[0]: arc end - start must be 1..360 degrees");
    EXPECT_EQ(error(with(R"({"type":"arc","bind":"battery.ratio","r":5,"w":0})")), "elements[0].w: must be at least 1");
    EXPECT_EQ(error(with(R"({"type":"arc","bind":"battery.ratio"})")), "elements[0]: arc needs \"r\"");
    EXPECT_EQ(error(with(R"({"type":"ticks","r":100,"count":12,"major":13})")), "elements[0].major: larger than count");
    EXPECT_EQ(error(with(R"({"type":"ticks","r":100,"count":0})")), "elements[0].count: 0 is out of range (1..120)");
    EXPECT_EQ(error(with(R"({"type":"hand","bind":"time.hour","pivot":[1]})")),
              "elements[0].pivot: expected [x, y], 0..1000");
    EXPECT_EQ(error(with(R"({"type":"hand","bind":"time.hour","pivot":[1,2,3]})")),
              "elements[0].pivot: expected [x, y], 0..1000");
    EXPECT_EQ(error(with(R"({"type":"image","image":"a/b.png","x":1,"y":2})")),
              "elements[0].image: 'a/b.png' is not a .png or .bin file name");
    EXPECT_EQ(error(with(R"({"type":"image","image":".png","x":1,"y":2})")),
              "elements[0].image: '.png' is not a .png or .bin file name");
    EXPECT_EQ(error(with(R"({"type":"image","image":"a.gif","x":1,"y":2})")),
              "elements[0].image: 'a.gif' is not a .png or .bin file name");
}

TEST_F(WfDeclTest, RejectsBadComplications)
{
    const auto comp = [](const char *slot, const char *extra = "") {
        return std::string(R"({"type":"complication","slot":")") + slot + R"(","x":1,"y":2)" + extra + "}";
    };
    EXPECT_EQ(error(with(comp("a") + "," + comp("a"))), "elements[1].slot: 'a' is used twice");
    EXPECT_EQ(error(with(comp("a") + "," + comp("b") + "," + comp("c") + "," + comp("d") + "," + comp("e"))),
              "elements[4].slot: more than 4 complications");
    EXPECT_EQ(error(with(comp("Top"))), "elements[0].slot: 'Top' is not a slot id ([a-z0-9_-])");
    EXPECT_EQ(error(with(comp("a", R"(,"default":"pulse")"))), "elements[0].default: unknown complication 'pulse'");
    EXPECT_EQ(error(with(comp("a", R"(,"style":"square")"))), "elements[0].style: 'square' is not circle or line");
    EXPECT_EQ(error(with(R"({"type":"text","text":"a","x":1,"y":2})", R"("aod":{"elements":[)" + comp("a") + "]},")),
              "aod.elements[0]: complications are not drawn in AOD");
}

TEST_F(WfDeclTest, RejectsTooMuch)
{
    std::string els;
    for (int i = 0; i <= WF_DECL_MAX_ELEMENTS; i++) {
        els += std::string(i ? "," : "") + R"({"type":"circle","x":1,"y":1,"r":1})";
    }
    EXPECT_EQ(error(with(els)), "elements: more than 32 elements");
    std::string aod;
    for (int i = 0; i <= WF_DECL_MAX_AOD_ELEMENTS; i++) {
        aod += std::string(i ? "," : "") + R"({"type":"circle","x":1,"y":1,"r":1})";
    }
    EXPECT_EQ(error(with(R"({"type":"circle","x":1,"y":1,"r":1})", R"("aod":{"elements":[)" + aod + "]},")),
              "aod.elements: more than 8 elements");
    std::string big = with(R"({"type":"circle","x":1,"y":1,"r":1})");
    big.insert(1, std::string(WF_DECL_JSON_MAX, ' '));
    EXPECT_EQ(error(big), "face.json is larger than 16384 bytes");
}

// Every truncation and many single-byte corruptions of a valid face must be handled
// (parse or a clean error with a message), never crash or read out of bounds.
TEST_F(WfDeclTest, SurvivesTruncationAndCorruption)
{
    const std::string json = read_file(S3W_WF_SAMPLES_DIR "/s3w.dial/face.json");
    ASSERT_FALSE(json.empty());
    for (size_t n = 0; n < json.size(); n++) {
        const std::string cut = json.substr(0, n);
        // An exact-size heap copy so out-of-bounds reads would hit the end of the block.
        std::unique_ptr<char[]> buf(new char[n ? n : 1]);
        memcpy(buf.get(), cut.data(), n);
        const bool ok = wf_decl_parse(buf.get(), n, face.get(), &err);
        if (!ok) {
            EXPECT_NE(err.msg[0], '\0') << "length " << n;
        }
    }
    static const char NOISE[] = {'\0', '"', '{', '}', '[', ']', ',', ':', '\\', 'x', '9', '-', '\xff'};
    for (size_t i = 0; i < json.size(); i += 3) {
        for (char c : NOISE) {
            std::string bad = json;
            bad[i] = c;
            if (!wf_decl_parse(bad.data(), bad.size(), face.get(), &err)) {
                EXPECT_NE(err.msg[0], '\0') << "byte " << i;
            }
        }
    }
}

TEST(WfDeclFont, Names)
{
    EXPECT_EQ(wf_decl_font_find("digits_96"), WF_DECL_FONT_DIGITS_96);
    EXPECT_EQ(wf_decl_font_find("caption_22"), WF_DECL_FONT_CAPTION);
    EXPECT_EQ(wf_decl_font_find("Digits_96"), WF_DECL_FONT_COUNT);
    EXPECT_EQ(wf_decl_font_find(""), WF_DECL_FONT_COUNT);
}

} // namespace
