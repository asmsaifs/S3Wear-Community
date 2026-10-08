#!/usr/bin/env bash
# Generate the S3Wear UI fonts (docs/04-ui-ux.md §2) as LVGL C fonts.
#
#   tools/fonts/gen_fonts.sh
#
# Output: firmware/components/ui_framework/ui/fonts/s3w_font_*.c (checked in, like
# components/proto, so the firmware and simulator builds need no Node.js).
# Re-run after changing sizes, weights or glyph ranges here, then rebuild.
#
# Needs: curl, unzip, shasum, Node.js (npx). Sources are pinned by SHA-256 and
# cached in build/fonts-cache/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/firmware/components/ui_framework/ui/fonts"
CACHE="$ROOT/build/fonts-cache"
LV_FONT_CONV="lv_font_conv@1.5.3"

# Inter 4.1 (SIL OFL 1.1), static TTFs from the release zip.
INTER_URL="https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip"
INTER_SHA="9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e"
# LVGL's symbol font (LV_SYMBOL_*), same LVGL version as the firmware.
FA_URL="https://raw.githubusercontent.com/lvgl/lvgl/v9.3.0/scripts/built_in_font/FontAwesome5-Solid%2BBrands%2BRegular.woff"
FA_SHA="f4e42f6cd69e5dbdcccc0f2f5be136cebde0e427641e45403bf9173a92da95f4"

# Text ranges: Basic Latin, Latin-1, Latin Extended-A, Greek, Cyrillic, general
# punctuation (dashes, quotes, bullet, ellipsis), per mille, euro.
TEXT_RANGE="0x20-0x7E,0xA0-0x17F,0x384-0x3CE,0x400-0x45F,0x2010-0x2027,0x2030,0x20AC"
# LV_SYMBOL_* code points (lvgl/scripts/built_in_font/built_in_font_gen.py).
SYMBOLS="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"
# Weather icons (P6-02, ui_apps weather_apps.h): sun, moon, cloud, cloud-sun, cloud-moon,
# cloud-rain, snowflake, smog, umbrella, wind (the storm bolt is LV_SYMBOL_CHARGE).
SYMBOLS="$SYMBOLS,61829,61830,61634,63172,63171,63293,62172,63327,61673,63278"
# Display font: digits and time/number punctuation only.
DISPLAY_SYMBOLS="0123456789:.,-+% "

fetch() { # url sha file
    if [ ! -f "$3" ] || ! echo "$2  $3" | shasum -a 256 -c --status; then
        curl -fsSL -o "$3" "$1"
    fi
    echo "$2  $3" | shasum -a 256 -c --status || { echo "checksum mismatch: $3" >&2; exit 1; }
}

mkdir -p "$CACHE" "$OUT"
fetch "$INTER_URL" "$INTER_SHA" "$CACHE/Inter-4.1.zip"
fetch "$FA_URL" "$FA_SHA" "$CACHE/FontAwesome5.woff"
unzip -o -q -j "$CACHE/Inter-4.1.zip" \
    extras/ttf/Inter-Regular.ttf extras/ttf/Inter-SemiBold.ttf extras/ttf/InterDisplay-SemiBold.ttf \
    extras/ttf/InterDisplay-Bold.ttf extras/ttf/InterDisplay-ExtraLight.ttf \
    LICENSE.txt -d "$CACHE"

# 4 bpp, uncompressed: rendering is I-cache bound (docs/02 §6), RLE would cost
# CPU per glyph. Kerning kept (fast format).
conv() { # name size ttf [extra args...]
    local name="$1" size="$2" ttf="$3"
    shift 3
    # Relative paths: lv_font_conv records its arguments in the file header.
    (cd "$CACHE" && npx -y "$LV_FONT_CONV" --bpp 4 --no-compress --size "$size" --format lvgl \
        --force-fast-kern-format --lv-include lvgl.h --lv-font-name "$name" \
        --font "$ttf" "$@" -o "$name.c")
    mv "$CACHE/$name.c" "$OUT/$name.c"
    echo "  $name.c ($(wc -c <"$OUT/$name.c") bytes)"
}

text_font() { # name size ttf
    conv "$1" "$2" "$3" -r "$TEXT_RANGE" --font FontAwesome5.woff -r "$SYMBOLS"
}

echo "lv_font_conv -> $OUT"
conv      s3w_font_display_96 96 InterDisplay-SemiBold.ttf --symbols "$DISPLAY_SYMBOLS"
# Watch faces (P3-02): Digital Bold's stacked digits; light digits for AOD faces
# (few lit pixels) and the Minimal face.
conv      s3w_font_display_bold_160 160 InterDisplay-Bold.ttf --symbols "$DISPLAY_SYMBOLS"
conv      s3w_font_display_light_96 96 InterDisplay-ExtraLight.ttf --symbols "$DISPLAY_SYMBOLS"
text_font s3w_font_title_32   32 Inter-SemiBold.ttf
text_font s3w_font_body_26    26 Inter-Regular.ttf
text_font s3w_font_caption_22 22 Inter-Regular.ttf
cp "$CACHE/LICENSE.txt" "$OUT/LICENSE-Inter.txt"
