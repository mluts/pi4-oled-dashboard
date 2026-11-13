#include "layout.hpp"

namespace dash::layout {
namespace {

// The pixels the current font lights up around a baseline, in u8g2's terms:
// ascent pixels up (positive), descent pixels down (descent is negative).
int top_pixel(u8g2_t &u8g2, int baseline)
{
    return baseline - u8g2_GetAscent(&u8g2);
}

int bottom_pixel(u8g2_t &u8g2, int baseline)
{
    return baseline - u8g2_GetDescent(&u8g2);
}

int baseline_for_top(u8g2_t &u8g2, int top)
{
    return top + u8g2_GetAscent(&u8g2);
}

int baseline_for_bottom(u8g2_t &u8g2, int bottom)
{
    return bottom + u8g2_GetDescent(&u8g2);
}

// Height of the current font's full glyph box, descenders included.
int box_height(u8g2_t &u8g2)
{
    return u8g2_GetAscent(&u8g2) - u8g2_GetDescent(&u8g2) + 1;
}

}  // namespace

StatusLayout StatusLayout::from(u8g2_t &u8g2, const uint8_t *small, const uint8_t *big, int gap)
{
    u8g2_SetFont(&u8g2, small);
    int label_baseline = baseline_for_top(u8g2, 0);
    int bottom_baseline = baseline_for_bottom(u8g2, kPanelHeight - 1);
    // The band the big number may use: below the label, above the bottom line,
    // minus the gap on each side.
    int band_top = bottom_pixel(u8g2, label_baseline) + 1 + gap;
    int band_bottom = top_pixel(u8g2, bottom_baseline) - 1 - gap;

    u8g2_SetFont(&u8g2, big);
    // NOTE: digits, "h" and "m" sit on the baseline, so only the ascent counts;
    // the font's descent (for g, j, ...) would push the number up for nothing.
    int big_height = u8g2_GetAscent(&u8g2) + 1;
    int band_height = band_bottom - band_top + 1;
    int big_top = band_top + (band_height - big_height) / 2;
    int big_baseline = baseline_for_top(u8g2, big_top);

    u8g2_SetFont(&u8g2, small);
    return {label_baseline, big_baseline, bottom_baseline};
}

MessageLayout MessageLayout::from(u8g2_t &u8g2, const uint8_t *message, const uint8_t *small, int gap)
{
    // Single line: the full glyph box centred, descenders included ("wakeup").
    u8g2_SetFont(&u8g2, message);
    int single_top = (kPanelHeight - box_height(u8g2)) / 2;
    int single_baseline = baseline_for_top(u8g2, single_top);

    // Two lines: two glyph boxes and the gap, centred as a block.
    u8g2_SetFont(&u8g2, small);
    int line_height = box_height(u8g2);
    int block_top = (kPanelHeight - (2 * line_height + gap)) / 2;
    int first_baseline = baseline_for_top(u8g2, block_top);
    int second_baseline = baseline_for_top(u8g2, block_top + line_height + gap);

    return {single_baseline, first_baseline, second_baseline};
}

int prompt_baseline(u8g2_t &u8g2, const uint8_t *prompt)
{
    u8g2_SetFont(&u8g2, prompt);
    return baseline_for_top(u8g2, 0);
}

}  // namespace dash::layout
