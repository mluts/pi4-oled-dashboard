// Renders a Screen into a u8g2 frame buffer. No IDF headers
#pragma once

#include "status.hpp"
#include "u8g2.h"

namespace dash {
constexpr int kPanelWidth = 128;
constexpr int kPanelHeight = 64;

// Clears the buffer, draws the screen (plus the "refresh?" overlay in the
// top-right corner when prompt is set) and sends the buffer to the panel
void draw_screen(u8g2_t &u8g2, const Screen &screen, bool prompt);
} // namespace dash
