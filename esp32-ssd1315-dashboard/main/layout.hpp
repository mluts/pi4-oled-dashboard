// Vertical layout of the 128x64 panel: the baseline of every line of text on
// each screen, derived once at start-up from the fonts' metrics. Horizontal
// placement depends on the text and stays in the painter.
#pragma once

#include "u8g2.h"

namespace dash::layout {

constexpr int kPanelWidth = 128;
constexpr int kPanelHeight = 64;

// "since" flush with the top edge, "missing N" flush with the bottom edge,
// the elapsed time centred in the band between them.
struct StatusLayout {
    int label_baseline;
    int big_baseline;
    int bottom_baseline;

    // `gap` is the minimum free pixels between the big number and each small
    // line. Leaves `small` selected on u8g2.
    static StatusLayout from(u8g2_t &u8g2, const uint8_t *small, const uint8_t *big, int gap = 2);
};

// One line ("no wakeup", errors, "booting") centred on the panel, or two
// lines (the SSID over "try N") centred as a block.
struct MessageLayout {
    int single_baseline;
    int first_baseline;
    int second_baseline;

    static MessageLayout from(u8g2_t &u8g2, const uint8_t *message, const uint8_t *small, int gap = 4);
};

// "refresh?" overlay flush with the top edge.
int prompt_baseline(u8g2_t &u8g2, const uint8_t *prompt);

}  // namespace dash::layout
