#!/usr/bin/env sh
# Regenerates the LVGL fonts used by the TEC screen from Inter 4.0 (SIL OFL 1.1).
# The glyph range covers Latin-1 (French accents) plus typographic punctuation
# (’ … · – € œ) that LVGL's built-in Montserrat fonts do not contain.
# Requires: curl, unzip and `npm i -g lv_font_conv`.
set -eu
cd "$(dirname "$0")/.."
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
curl -fsSL -o "$WORK/inter.zip" https://github.com/rsms/inter/releases/download/v4.0/Inter-4.0.zip
unzip -q -j "$WORK/inter.zip" "extras/ttf/Inter-*.ttf" -d "$WORK"
RANGE="0x20-0x7E,0xA0-0xFF,0x152-0x153,0x2013-0x2014,0x2018-0x2019,0x201C-0x201D,0x2022,0x2026,0x20AC"
gen() {
    lv_font_conv --no-compress --no-prefilter --bpp 4 --format lvgl --lv-include lvgl.h \
        --font "$WORK/$1.ttf" -r "$RANGE" --size "$2" -o "main/assets/$3.c"
}
gen Inter-SemiBold 16 tec_font_semibold_16
gen Inter-Regular 18 tec_font_regular_18
gen Inter-Bold 24 tec_font_bold_24
gen Inter-Bold 30 tec_font_bold_30
gen Inter-Bold 34 tec_font_bold_34
