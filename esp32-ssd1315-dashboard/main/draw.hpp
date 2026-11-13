// Paints a Screen into a u8g2 frame buffer. No IDF headers.
#pragma once

#include "status.hpp"
#include "u8g2.h"

namespace dash {

// Computes the layouts for the fonts in use. Call once after u8g2_InitDisplay.
void draw_init(u8g2_t &u8g2);

// Clears the buffer, draws the screen (plus the "refresh?" overlay in the
// top-right corner when prompt is set) and sends the buffer to the panel.
void draw_screen(u8g2_t &u8g2, const Screen &screen, bool prompt);

}  // namespace dash
