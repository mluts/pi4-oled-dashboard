#include "draw.hpp"

#include "etl/to_string.h"
#include "u8g2.h"

namespace dash {
namespace {

constexpr const uint8_t *kFontSmall = u8g2_font_helvR12_tr;
constexpr const uint8_t *kFontBig = u8g2_font_logisoso26_tr;
constexpr const uint8_t *kFontMessage = u8g2_font_helvR14_tr;
constexpr const uint8_t *kFontPrompt = u8g2_font_5x7_tr;

constexpr const char *kLabel = "since";
constexpr const char *kPrompt = "refresh?";

void draw_centered(u8g2_t &u8g2, const char *text, int baseline) {
  int width = u8g2_GetStrWidth(&u8g2, text);
  int x = width >= kPanelWidth ? 0 : (kPanelWidth - width) / 2;
  u8g2_DrawStr(&u8g2, x, baseline, text);
}

void draw_status(u8g2_t &u8g2, const Screen &screen) {
  etl::string<24> missing{"missing "};
  etl::to_string(screen.sittings_missing, missing, true);

  // Note: as on the Pi, the label hugs ther top edge and the bottom line the
  // bottom edge;
  // The big number is centered in the band between them.

  u8g2_SetFont(&u8g2, kFontSmall);
  int ascent = u8g2_GetAscent(&u8g2);
  int descent = u8g2_GetDescent(&u8g2); // it's negative number, not positive
  u8g2_DrawStr(&u8g2, 0, ascent, kLabel);
  u8g2_DrawStr(&u8g2, 0, kPanelHeight - 1 + descent, missing.c_str());

  int band_top = ascent + 1;
  int band_bottom = kPanelHeight - 1 + descent - ascent;
}
} // namespace
} // namespace dash
