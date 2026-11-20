#ifndef CALENDAR_RENDERER_H_
#define CALENDAR_RENDERER_H_

#include <cstddef>
#include <cstdint>

namespace display {

// Text shown by the calendar. The month names are expected in uppercase and
// without accents, because the bitmap font is limited to ASCII.
struct CalendarLabels {
    const char* month_names[12];
    // One letter per column, starting on the first day of the week.
    const char* weekday_initials[7];
    // 0 for Monday, 6 for Sunday.
    int first_day_of_week;
};

// Colours of the calendar, as RGB565 values.
struct CalendarColors {
    uint16_t background = 0x0000;
    uint16_t text = 0xFFFF;
    uint16_t weekend = 0x8410;
    uint16_t today = 0x07E0;
};

// Labels for the language the firmware was built with.
const CalendarLabels& CalendarLabelsForBuild();

// Draws the calendar of one month into an RGB565 buffer, two bytes per pixel
// in little endian order, which is the order the LCD panel expects.
//
//   width, height      size of the drawn area
//   stride_bytes       distance between two rows of the buffer
//   year, month        the month to draw, month is 1 to 12
//   first_weekday      weekday of the first of the month, 0 for Monday
//   days_in_month      28 to 31
//   today_day          day to highlight, 0 to highlight none
//
// Returns false when the buffer is too small.
bool RenderMonthCalendar(uint8_t* rgb565, int width, int height, int stride_bytes, int year,
                          int month, int first_weekday, int days_in_month, int today_day,
                          const CalendarLabels& labels, const CalendarColors& colors);

// True when a pixel of a width by height area centred in the buffer falls
// outside the circle of a round display, so the caller can check that the
// whole calendar is visible.
bool CalendarFitsRoundScreen(int width, int height);

}  // namespace display

#endif  // CALENDAR_RENDERER_H_
