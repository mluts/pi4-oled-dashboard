#include "draw.hpp"

#include "etl/to_string.h"

#include "layout.hpp"

namespace dash {
namespace {

using layout::kPanelWidth;

constexpr const char *kLabel = "since";
constexpr const char *kPrompt = "refresh?";

layout::StatusLayout s_status;
layout::MessageLayout s_message;
int s_prompt_baseline;

void draw_left(u8g2_t &u8g2, const uint8_t *font, int baseline, const char *text)
{
    u8g2_SetFont(&u8g2, font);
    u8g2_DrawStr(&u8g2, 0, baseline, text);
}

void draw_centered(u8g2_t &u8g2, const uint8_t *font, int baseline, const char *text)
{
    u8g2_SetFont(&u8g2, font);
    int width = u8g2_GetStrWidth(&u8g2, text);
    int x = width >= kPanelWidth ? 0 : (kPanelWidth - width) / 2;
    u8g2_DrawStr(&u8g2, x, baseline, text);
}

void draw_right(u8g2_t &u8g2, const uint8_t *font, int baseline, const char *text)
{
    u8g2_SetFont(&u8g2, font);
    int width = u8g2_GetStrWidth(&u8g2, text);
    u8g2_DrawStr(&u8g2, kPanelWidth - width, baseline, text);
}

void draw_status(u8g2_t &u8g2, const Screen &screen)
{
    etl::string<24> missing{"missing "};
    etl::to_string(screen.missing, missing, true);

    draw_left(u8g2, fonts::kSmall, s_status.label_baseline, kLabel);
    draw_centered(u8g2, fonts::kBig, s_status.big_baseline, screen.elapsed.c_str());
    draw_left(u8g2, fonts::kSmall, s_status.bottom_baseline, missing.c_str());
}

void draw_message(u8g2_t &u8g2, const Screen &screen)
{
    if (screen.line2.empty()) {
        draw_centered(u8g2, fonts::kMessage, s_message.single_baseline, screen.line1.c_str());
        return;
    }
    draw_centered(u8g2, fonts::kSmall, s_message.first_baseline, screen.line1.c_str());
    draw_centered(u8g2, fonts::kSmall, s_message.second_baseline, screen.line2.c_str());
}

}  // namespace

void draw_init(u8g2_t &u8g2)
{
    u8g2_SetFontPosBaseline(&u8g2);
    s_status = layout::StatusLayout::from(u8g2, fonts::kSmall, fonts::kBig);
    s_message = layout::MessageLayout::from(u8g2, fonts::kMessage, fonts::kSmall);
    s_prompt_baseline = layout::prompt_baseline(u8g2, fonts::kPrompt);
}

void draw_screen(u8g2_t &u8g2, const Screen &screen, bool prompt)
{
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFontMode(&u8g2, 1);  // transparent glyph background
    u8g2_SetDrawColor(&u8g2, 1);

    if (screen.kind == ScreenKind::status) {
        draw_status(u8g2, screen);
    } else {
        draw_message(u8g2, screen);
    }
    if (prompt) {
        draw_right(u8g2, fonts::kPrompt, s_prompt_baseline, kPrompt);
    }
    u8g2_SendBuffer(&u8g2);
}

}  // namespace dash
