// Copyright 2023 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_SCREEN_STRING_INTERNAL_HPP
#define FTXUI_SCREEN_STRING_INTERNAL_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/util/export.hpp"

namespace ftxui {

// Internal functions exported for the 'component' library.
FTXUI_EXPORT(SCREEN)
bool EatCodePoint(std::string_view input,
                  size_t start,
                  size_t* end,
                  uint32_t* ucs);
FTXUI_EXPORT(SCREEN)
bool EatCodePoint(std::wstring_view input,
                  size_t start,
                  size_t* end,
                  uint32_t* ucs);

FTXUI_EXPORT(SCREEN) bool IsCombining(uint32_t ucs);
FTXUI_EXPORT(SCREEN) bool IsAmbiguousWidth(uint32_t ucs);
FTXUI_EXPORT(SCREEN) bool IsFullWidth(uint32_t ucs);
FTXUI_EXPORT(SCREEN) bool IsControl(uint32_t ucs);

// East-Asian Ambiguous codepoints occupy one cell in Western terminals and
// usually two cells under CJK fonts. Terminals decide per glyph, so the
// layout width is a runtime choice: when enabled, ambiguous codepoints
// occupy two cells, except box-drawing glyphs that CJK fonts almost always
// render narrow.
FTXUI_EXPORT(SCREEN) void SetAmbiguousWidthIsWide(bool wide);
FTXUI_EXPORT(SCREEN) bool AmbiguousWidthIsWide();

// @brief Test whether one locale string implies CJK ambiguous-width layout
//
// @param locale getenv-style locale string; null and empty mean Western
// @return True for zh/ja/ko language prefixes
FTXUI_EXPORT(SCREEN)
bool LocaleTreatsAmbiguousAsWide(const char* locale);

// @brief Cells one codepoint occupies under the current ambiguous mode
//
// Fullwidth always takes two cells; East-Asian Ambiguous codepoints take
// two cells when the wide mode is enabled, except box drawing.
//
// @param ucs Unicode codepoint
// @return 1 or 2 cells; control and combining inputs are not handled
FTXUI_EXPORT(SCREEN) int CodepointCellWidth(uint32_t ucs);

// @brief Neutral-width companions of ambiguous spinner animations
//
// U+25D2/U+25D3 pair with the ambiguous U+25D0/U+25D1 in one animation;
// layouts that widen ambiguous codepoints must widen them too.
//
// @param codepoint Unicode codepoint
// @return True for the neutral half-circle companions
FTXUI_EXPORT(SCREEN)
bool IsAmbiguousWidthAnimationCompanion(uint32_t codepoint);

FTXUI_EXPORT(SCREEN) size_t GlyphPrevious(std::string_view input, size_t start);
FTXUI_EXPORT(SCREEN) size_t GlyphNext(std::string_view input, size_t start);

// Return the index in the |input| string of the glyph at |glyph_offset|,
// starting at |start|
FTXUI_EXPORT(SCREEN)
size_t GlyphIterate(std::string_view input, int glyph_offset, size_t start = 0);

// Returns the number of glyphs in |input|.
FTXUI_EXPORT(SCREEN) int GlyphCount(std::string_view input);

// Properties from:
// https://www.unicode.org/Public/UCD/latest/ucd/auxiliary/WordBreakProperty.txt
enum class WordBreakProperty : int8_t {
  ALetter,
  CR,
  Double_Quote,
  Extend,
  ExtendNumLet,
  Format,
  Hebrew_Letter,
  Katakana,
  LF,
  MidLetter,
  MidNum,
  MidNumLet,
  Newline,
  Numeric,
  Regional_Indicator,
  Single_Quote,
  WSegSpace,
  ZWJ,
};
FTXUI_EXPORT(SCREEN)
WordBreakProperty CodepointToWordBreakProperty(uint32_t codepoint);
FTXUI_EXPORT(SCREEN)
std::vector<WordBreakProperty> Utf8ToWordBreakProperty(std::string_view input);

FTXUI_EXPORT(SCREEN)
bool IsWordBreakingCharacter(std::string_view input, size_t glyph_index);
}  // namespace ftxui

#endif /* end of include guard: FTXUI_SCREEN_STRING_INTERNAL_HPP */
