// Paints a Screen into a u8g2 frame buffer. No IDF headers.
#pragma once

#include "status.hpp"
#include "u8g2.h"

namespace dash {

// The fonts of every screen, shared with the host tests.
// NOTE: sizes chosen to keep the Pi's proportions (17 px label, 30 px DejaVu
// digits, 15 px messages) on 64 pixels. u8g2 sizes count the whole glyph box,
// so they come out smaller than Pillow's point sizes.
namespace fonts {
inline constexpr const uint8_t *kSmall = u8g2_font_helvR12_tr;  // since, missing N
inline constexpr const uint8_t *kBig = u8g2_font_logisoso26_tr; // elapsed time
inline constexpr const uint8_t *kMessage = u8g2_font_helvR14_tr;
inline constexpr const uint8_t *kPrompt = u8g2_font_5x7_tr; // refresh?
} // namespace fonts

// Computes the layouts for the fonts in use. Call once after u8g2_InitDisplay.
void draw_init(u8g2_t &u8g2);

// Clears the buffer, draws the screen (plus the "refresh?" overlay in the
// top-right corner when prompt is set) and sends the buffer to the panel.
void draw_screen(u8g2_t &u8g2, const Screen &screen, bool prompt);

}  // namespace dash
