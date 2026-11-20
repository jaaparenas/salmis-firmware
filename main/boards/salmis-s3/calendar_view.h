#ifndef SALMIS_CALENDAR_VIEW_H_
#define SALMIS_CALENDAR_VIEW_H_

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class CustomLcdDisplay;

// Shows the calendar of the current month on the screen instead of the eyes.
//
// The assistant asks for it with the self.screen.show_calendar tool. The
// calendar stays until it is dismissed: a touch on the screen, the BOOT
// button, the wake word or the self.screen.hide_calendar tool. It is drawn
// with a bitmap font into an RGB565 buffer and shown through the same layer
// as the photo slideshow, so it never touches LVGL.
class CalendarView {
public:
    static constexpr int kWidth = 240;
    static constexpr int kHeight = 240;
    // The calendar is dismissed on its own after this many seconds, so it
    // cannot stay on the screen forever if nobody touches it.
    static constexpr int kAutoHideSeconds = 90;

    static CalendarView& GetInstance();

    void Start(CustomLcdDisplay* display);

    // Registers the MCP tools that show and hide the calendar.
    void RegisterTools();

    // Draws the calendar of the current month and shows it.
    bool Show();

    // Hides the calendar and brings the eyes back.
    void Hide();

    bool IsVisible() const { return visible_; }

    // Called from the board when the user touches the screen or presses the
    // button: any interaction leaves the calendar.
    void NotifyInteraction();

private:
    CalendarView() = default;

    static void TaskEntry(void* arg);
    void Task();

    bool EnsureBuffer();
    void RenderCurrentMonth();

    CustomLcdDisplay* display_ = nullptr;
    uint8_t* buffer_ = nullptr;
    bool visible_ = false;
    bool tool_turn_ = false;
    int visible_seconds_ = 0;
    int last_state_ = -1;
    TaskHandle_t task_ = nullptr;
};

#endif  // SALMIS_CALENDAR_VIEW_H_
