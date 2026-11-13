// Flat C API over the IDF-free modules, for the pytest suite to call through
// ctypes. Built for the Mac by the Makefile next to this file.
#include "draw.hpp"
#include "layout.hpp"
#include "u8g2.h"

namespace {

// NOTE: same setup as screen.cpp, so the _f variant points this at u8g2's own
// static 1 KB frame buffer; draw_screen's SendBuffer goes nowhere and the frame
// stays there for dash_buffer().
u8g2_t s_u8g2;

// No I2C on the Mac: accept every message. Returning 0 would make u8g2 treat
// the message as failed.
uint8_t byte_cb(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }
uint8_t gpio_delay_cb(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }

// Indexed by dash_draw_text's font argument; conftest.py's Font enum mirrors
// this order.
const uint8_t *const kFonts[] = {dash::fonts::kSmall, dash::fonts::kBig,
                                 dash::fonts::kMessage, dash::fonts::kPrompt};

} // namespace

extern "C" {

void dash_init() {
  u8g2_Setup_ssd1306_i2c_128x64_noname_f(&s_u8g2, U8G2_R0, byte_cb,
                                         gpio_delay_cb);
  u8g2_InitDisplay(&s_u8g2);
  dash::draw_init(s_u8g2);
}

// The 1 KB frame buffer: 8 pages of 128 bytes, each byte a column of 8 pixels
// with the top one in bit 0.
const uint8_t *dash_buffer() { return u8g2_GetBufferPtr(&s_u8g2); }

void dash_draw_status(const char *elapsed, int missing, bool prompt) {
  dash::draw_screen(s_u8g2, dash::Screen::status(elapsed, missing), prompt);
}

// line2 is "" for a one-line message.
void dash_draw_message(const char *line1, const char *line2, bool prompt) {
  dash::draw_screen(s_u8g2, dash::Screen::message(line1, line2), prompt);
}

// The baselines draw_init computed, recomputed with the same fonts.
// out: label, big, bottom.
void dash_status_layout(int out[3]) {
  auto l = dash::layout::StatusLayout::from(s_u8g2, dash::fonts::kSmall,
                                            dash::fonts::kBig);
  out[0] = l.label_baseline;
  out[1] = l.big_baseline;
  out[2] = l.bottom_baseline;
}

// out: single, first, second.
void dash_message_layout(int out[3]) {
  auto l = dash::layout::MessageLayout::from(s_u8g2, dash::fonts::kMessage,
                                             dash::fonts::kSmall);
  out[0] = l.single_baseline;
  out[1] = l.first_baseline;
  out[2] = l.second_baseline;
}

int dash_prompt_baseline() {
  return dash::layout::prompt_baseline(s_u8g2, dash::fonts::kPrompt);
}

// Clears the buffer and draws one string alone, in the font mode draw_screen
// uses (baseline position from draw_init, transparent glyphs). Returns its
// width.
int dash_draw_text(int font, int x, int baseline, const char *text) {
  u8g2_ClearBuffer(&s_u8g2);
  u8g2_SetFontMode(&s_u8g2, 1);
  u8g2_SetDrawColor(&s_u8g2, 1);
  u8g2_SetFont(&s_u8g2, kFonts[font]);
  u8g2_DrawStr(&s_u8g2, x, baseline, text);
  return u8g2_GetStrWidth(&s_u8g2, text);
}

} // extern "C"
