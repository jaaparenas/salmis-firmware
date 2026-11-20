#include "calendar_renderer.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

namespace display {
namespace {

// A 5x7 bitmap font. Every glyph is five columns, and every column is a byte
// whose bit 0 is the top row. The font is deliberately limited to ASCII: the
// calendar is drawn without touching LVGL, so it cannot be affected by the
// theme fonts being replaced at runtime.
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kGlyphAdvance = 6;  // one column of spacing

struct Glyph {
    char character;
    uint8_t columns[kGlyphWidth];
};

// clang-format off
const Glyph kFont[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
};
// clang-format on

const Glyph* FindGlyph(char character) {
    if (character >= 'a' && character <= 'z') {
        character = static_cast<char>(character - 'a' + 'A');
    }
    for (const auto& glyph : kFont) {
        if (glyph.character == character) {
            return &glyph;
        }
    }
    return nullptr;
}

// Layout of the calendar, in pixels.
constexpr int kTextScale = 2;
constexpr int kTitleTop = 34;
constexpr int kWeekdayTop = 58;
constexpr int kGridTop = 78;
constexpr int kCellWidth = 27;
constexpr int kCellHeight = 19;
constexpr int kColumns = 7;

class Canvas {
public:
    Canvas(uint8_t* buffer, int width, int height, int stride_bytes)
        : buffer_(buffer), width_(width), height_(height), stride_(stride_bytes) {}

    void Fill(uint16_t color) {
        for (int y = 0; y < height_; y++) {
            for (int x = 0; x < width_; x++) {
                SetPixel(x, y, color);
            }
        }
    }

    void SetPixel(int x, int y, uint16_t color) {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return;
        }
        size_t offset = static_cast<size_t>(y) * stride_ + static_cast<size_t>(x) * 2;
        buffer_[offset] = static_cast<uint8_t>(color & 0xFF);
        buffer_[offset + 1] = static_cast<uint8_t>(color >> 8);
    }

    // Draws a filled rectangle.
    void FillRect(int x, int y, int w, int h, uint16_t color) {
        for (int row = 0; row < h; row++) {
            for (int column = 0; column < w; column++) {
                SetPixel(x + column, y + row, color);
            }
        }
    }

    // Draws a one pixel frame.
    void DrawRect(int x, int y, int w, int h, uint16_t color) {
        for (int column = 0; column < w; column++) {
            SetPixel(x + column, y, color);
            SetPixel(x + column, y + h - 1, color);
        }
        for (int row = 0; row < h; row++) {
            SetPixel(x, y + row, color);
            SetPixel(x + w - 1, y + row, color);
        }
    }

    // Draws a string and returns its width in pixels.
    int DrawText(int x, int y, const char* text, uint16_t color, int scale = kTextScale) {
        int cursor = x;
        for (const char* p = text; *p != '\0'; p++) {
            const Glyph* glyph = FindGlyph(*p);
            if (glyph != nullptr) {
                for (int column = 0; column < kGlyphWidth; column++) {
                    uint8_t bits = glyph->columns[column];
                    for (int row = 0; row < kGlyphHeight; row++) {
                        if (bits & (1 << row)) {
                            FillRect(cursor + column * scale, y + row * scale, scale, scale, color);
                        }
                    }
                }
            }
            cursor += (kGlyphWidth + 1) * scale;
        }
        return cursor - x;
    }

    static int TextWidth(const char* text, int scale = kTextScale) {
        int length = 0;
        for (const char* p = text; *p != '\0'; p++) {
            length++;
        }
        return length * (kGlyphWidth + 1) * scale - scale;
    }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    uint8_t* buffer_;
    int width_;
    int height_;
    int stride_;
};

// Spanish labels. The month names of Spanish have no accents, so the ASCII
// font is enough.
constexpr CalendarLabels kSpanishLabels = {
    {"ENERO", "FEBRERO", "MARZO", "ABRIL", "MAYO", "JUNIO",
     "JULIO", "AGOSTO", "SEPTIEMBRE", "OCTUBRE", "NOVIEMBRE", "DICIEMBRE"},
    {"L", "M", "X", "J", "V", "S", "D"},
    0,  // Monday
};

constexpr CalendarLabels kEnglishLabels = {
    {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
     "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"},
    {"M", "T", "W", "T", "F", "S", "S"},
    6,  // Sunday
};

}  // namespace

const CalendarLabels& CalendarLabelsForBuild() {
#if defined(es_es)
    return kSpanishLabels;
#else
    return kEnglishLabels;
#endif
}

bool RenderMonthCalendar(uint8_t* rgb565, int width, int height, int stride_bytes, int year,
                          int month, int first_weekday, int days_in_month, int today_day,
                          const CalendarLabels& labels, const CalendarColors& colors) {
    if (rgb565 == nullptr || width <= 0 || height <= 0 || month < 1 || month > 12) {
        return false;
    }
    size_t needed = static_cast<size_t>(height) * static_cast<size_t>(stride_bytes);
    if (needed < static_cast<size_t>(width) * static_cast<size_t>(height) * 2) {
        return false;
    }

    Canvas canvas(rgb565, width, height, stride_bytes);
    canvas.Fill(colors.background);

    // Title: the month name and the year.
    char title[32];
    snprintf(title, sizeof(title), "%s %d", labels.month_names[month - 1], year);
    int title_width = Canvas::TextWidth(title);
    canvas.DrawText((width - title_width) / 2, kTitleTop, title, colors.text);

    // Weekday initials. The columns start on the first day of the week of
    // the labels, so the initials are rotated to match.
    int grid_width = kColumns * kCellWidth;
    int grid_left = (width - grid_width) / 2;
    for (int column = 0; column < kColumns; column++) {
        int weekday = (labels.first_day_of_week + column) % 7;
        const char* initial = labels.weekday_initials[weekday];
        bool weekend = weekday == 5 || weekday == 6;
        int initial_width = Canvas::TextWidth(initial);
        int x = grid_left + column * kCellWidth + (kCellWidth - initial_width) / 2;
        canvas.DrawText(x, kWeekdayTop, initial, weekend ? colors.weekend : colors.text);
    }

    // Days. The first column is the first day of the week of the labels.
    int column = (first_weekday - labels.first_day_of_week + 7) % 7;
    int row = 0;
    for (int day = 1; day <= days_in_month; day++) {
        int cell_x = grid_left + column * kCellWidth;
        int cell_y = kGridTop + row * kCellHeight;
        int weekday = (first_weekday + day - 1) % 7;
        bool weekend = weekday == 5 || weekday == 6;

        // The day is written by hand: it is at most two digits, and that
        // keeps the compiler from warning about a possible truncation.
        char number[3];
        if (day >= 10) {
            number[0] = static_cast<char>('0' + day / 10);
            number[1] = static_cast<char>('0' + day % 10);
        } else {
            number[0] = static_cast<char>('0' + day);
        }
        number[2] = '\0';
        int number_width = Canvas::TextWidth(number);
        int x = cell_x + (kCellWidth - number_width) / 2;
        int y = cell_y + (kCellHeight - kGlyphHeight * kTextScale) / 2;

        if (day == today_day) {
            // The day of the month gets a frame around it.
            canvas.DrawRect(cell_x + 1, cell_y, kCellWidth - 2, kCellHeight - 1, colors.today);
        }
        canvas.DrawText(x, y, number, weekend ? colors.weekend : colors.text);

        column++;
        if (column == kColumns) {
            column = 0;
            row++;
        }
    }

    return true;
}

bool CalendarFitsRoundScreen(int width, int height) {
    // The centre of the circle is the centre of the area, and its radius is
    // half of the shorter side.
    float radius = static_cast<float>(std::min(width, height)) / 2.0f;
    float centre_x = static_cast<float>(width) / 2.0f;
    float centre_y = static_cast<float>(height) / 2.0f;
    float limit = radius * radius;

    // Check the four corners and the middle of every edge, which is where the
    // calendar text reaches.
    const int probes[] = {0, width / 2, width - 1};
    for (int y : {0, height / 2, height - 1}) {
        for (int x : probes) {
            float dx = static_cast<float>(x) + 0.5f - centre_x;
            float dy = static_cast<float>(y) + 0.5f - centre_y;
            if (dx * dx + dy * dy > limit) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace display
