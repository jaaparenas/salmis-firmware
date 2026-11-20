"""Render the calendar on the host and check the layout.

Compiles main/display/calendar_renderer.cc and draws the month as text, so
the bitmap font and the layout can be inspected without a device.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RENDERER = ROOT / "main/display/calendar_renderer.cc"
RENDERER_HEADER = ROOT / "main/display/calendar_renderer.h"

HARNESS = r"""
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "calendar_renderer.h"

namespace {

// December 2026 starts on a Tuesday. December 2026 has 31 days.
constexpr int kYear = 2026;
constexpr int kMonth = 12;
// 0 is Monday, so a Tuesday is 1.
constexpr int kFirstWeekday = 1;
constexpr int kDaysInMonth = 31;
constexpr int kToday = 20;
constexpr int kWidth = 240;
constexpr int kHeight = 240;
constexpr int kStride = kWidth * 2;

}  // namespace

int main() {
    std::vector<uint8_t> buffer(kWidth * kHeight * 2, 0);
    display::CalendarLabels labels = display::CalendarLabelsForBuild();
    display::CalendarColors colors;

    if (!display::RenderMonthCalendar(buffer.data(), kWidth, kHeight, kStride, kYear, kMonth,
                                      kFirstWeekday, kDaysInMonth, kToday, labels, colors)) {
        printf("render failed\n");
        return 1;
    }

    // Draw the buffer as text: a dot for the background, a hash for the text.
    printf("month: %s\n", labels.month_names[kMonth - 1]);
    for (int y = 0; y < kHeight; y++) {
        std::string line;
        for (int x = 0; x < kWidth; x++) {
            size_t offset = static_cast<size_t>(y) * kStride + static_cast<size_t>(x) * 2;
            uint16_t color = buffer[offset] | (buffer[offset + 1] << 8);
            line += (color == colors.background) ? '.' : '#';
        }
        // Trim the right margin so the output stays readable.
        while (!line.empty() && line.back() == '.') {
            line.pop_back();
        }
        printf("%s\n", line.c_str());
    }

    // Every drawn pixel must stay inside the circle of the round screen.
    int outside = 0;
    float radius = kWidth / 2.0f;
    for (int y = 0; y < kHeight; y++) {
        for (int x = 0; x < kWidth; x++) {
            size_t offset = static_cast<size_t>(y) * kStride + static_cast<size_t>(x) * 2;
            uint16_t color = buffer[offset] | (buffer[offset + 1] << 8);
            if (color == colors.background) {
                continue;
            }
            float dx = x + 0.5f - kWidth / 2.0f;
            float dy = y + 0.5f - kHeight / 2.0f;
            if (dx * dx + dy * dy > radius * radius) {
                outside++;
            }
        }
    }
    printf("pixels outside the circle: %d\n", outside);

    // The day of the month must be highlighted, so it must use the today colour.
    bool today_found = false;
    for (int y = 0; y < kHeight && !today_found; y++) {
        for (int x = 0; x < kWidth && !today_found; x++) {
            size_t offset = static_cast<size_t>(y) * kStride + static_cast<size_t>(x) * 2;
            uint16_t color = buffer[offset] | (buffer[offset + 1] << 8);
            if (color == colors.today) {
                today_found = true;
            }
        }
    }
    printf("today highlighted: %s\n", today_found ? "yes" : "no");
    return outside == 0 && today_found ? 0 : 1;
}
"""


@unittest.skipUnless(shutil.which("c++"), "requires a host C++ compiler")
class CalendarRendererTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        work = pathlib.Path(cls.tmp.name)
        (work / "harness.cc").write_text(HARNESS)
        cls.binary = str(work / "calendar-test")
        subprocess.run(
            ["c++", "-std=c++17", "-Wall", "-Werror",
             "-I", str(RENDERER_HEADER.parent),
             str(RENDERER), str(work / "harness.cc"),
             "-o", cls.binary],
            check=True,
        )

    def test_calendar_is_drawn_inside_the_round_screen(self):
        result = subprocess.run([self.binary], capture_output=True, text=True)
        print(result.stdout)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
