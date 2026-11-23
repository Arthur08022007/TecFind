#!/usr/bin/env python3
"""Regression checks for the TEC departures screen text rendering."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parent.parent
MAIN = ROOT / "main"
TEC_VIEW = MAIN / "tec/tec_view.c"
TEC_MODEL = MAIN / "tec/tec.c"
FONTS = sorted((MAIN / "assets").glob("tec_font_*.c"))
FRENCH_GLYPHS = "éèêàçôûÉ’…·–"


def font_ranges(path: Path) -> list[tuple[int, int]]:
    text = path.read_text()
    return [(int(start), int(start) + int(length) - 1) for start, length in
            re.findall(r"\.range_start = (\d+), \.range_length = (\d+)", text)]


class TecViewTests(unittest.TestCase):
    def test_text_labels_use_fonts_with_french_glyphs(self) -> None:
        text = TEC_VIEW.read_text()
        self.assertNotRegex(text, r"lv_font_montserrat_(?!16)\d+")
        self.assertEqual(2, text.count("lv_font_montserrat_16"))
        self.assertGreaterEqual(len(FONTS), 5)
        for font in FONTS:
            ranges = font_ranges(font)
            for glyph in FRENCH_GLYPHS:
                self.assertTrue(any(lo <= ord(glyph) <= hi for lo, hi in ranges),
                                f"{font.name} lacks {glyph!r}")

    def test_view_uses_fonts_it_declares(self) -> None:
        text = TEC_VIEW.read_text()
        for font in FONTS:
            self.assertIn(f"LV_FONT_DECLARE({font.stem})", text)

    def test_model_never_splits_utf8_sequences(self) -> None:
        text = TEC_MODEL.read_text()
        self.assertIn("static void utf8_copy", text)
        offenders = re.findall(r"strlcpy\(\w+(?:->|\.)(?:line|destination|stop_name),", text)
        self.assertEqual([], offenders)

    def test_status_messages_fit_their_buffer(self) -> None:
        types = (MAIN / "view_data_types.h").read_text()
        size = int(re.search(r"char status\[(\d+)\];\n    int count;", types).group(1))
        for message in re.findall(r'data\.status, "([^"]+)"|\.status = "([^"]+)"', TEC_MODEL.read_text()):
            message = "".join(message)
            self.assertLess(len(message.encode()), size, message)


if __name__ == "__main__":
    unittest.main(verbosity=2)
