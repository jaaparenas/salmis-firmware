#include "calendar_view.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <ctime>

#include "application.h"
#include "custom_display.h"
#include "display/calendar_renderer.h"
#include "device_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mcp_server.h"

#define TAG "SalmisCalendar"

namespace {

constexpr size_t kTaskStackSize = 3072;

// Weekdays run from Monday, while struct tm runs from Sunday.
int WeekdayFromMonday(const struct tm& local) {
    return (local.tm_wday + 6) % 7;
}

int DaysInMonth(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 30;
    }
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month - 1];
}

}  // namespace

CalendarView& CalendarView::GetInstance() {
    static CalendarView instance;
    return instance;
}

void CalendarView::Start(CustomLcdDisplay* display) {
    display_ = display;
    if (task_ == nullptr) {
        xTaskCreate(TaskEntry, "salmis_calendar", kTaskStackSize, this, 3, &task_);
    }
}

void CalendarView::TaskEntry(void* arg) {
    static_cast<CalendarView*>(arg)->Task();
    vTaskDelete(nullptr);
}

void CalendarView::Task() {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!visible_) {
            last_state_ = -1;
            continue;
        }

        visible_seconds_++;
        if (visible_seconds_ >= kAutoHideSeconds) {
            ESP_LOGI(TAG, "Calendar hidden after %d s", visible_seconds_);
            Hide();
            continue;
        }

        // A new turn of the assistant means the screen is needed again, so
        // the calendar makes room. The turn during which the calendar was
        // asked for does not count.
        int state = Application::GetInstance().GetDeviceState();
        bool new_turn = state == kDeviceStateSpeaking && last_state_ != kDeviceStateSpeaking;
        last_state_ = state;
        if (new_turn && !tool_turn_) {
            ESP_LOGI(TAG, "Calendar hidden, the assistant started speaking again");
            Hide();
        }
        tool_turn_ = false;
    }
}

bool CalendarView::EnsureBuffer() {
    if (buffer_ != nullptr) {
        return true;
    }
    size_t size = static_cast<size_t>(kWidth) * kHeight * 2;
    buffer_ = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer_ == nullptr) {
        buffer_ = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
    }
    if (buffer_ == nullptr) {
        ESP_LOGE(TAG, "No memory for the calendar buffer");
        return false;
    }
    return true;
}

void CalendarView::RenderCurrentMonth() {
    time_t now = time(nullptr);
    struct tm local{};
    localtime_r(&now, &local);

    int year = local.tm_year + 1900;
    int month = local.tm_mon + 1;
    display::CalendarLabels labels = display::CalendarLabelsForBuild();
    display::CalendarColors colors;
    display::RenderMonthCalendar(buffer_, kWidth, kHeight, kWidth * 2, year, month,
                                  WeekdayFromMonday(local), DaysInMonth(year, month), local.tm_mday,
                                  labels, colors);
    ESP_LOGI(TAG, "Calendar drawn: %04d-%02d", year, month);
}

bool CalendarView::Show() {
    if (display_ == nullptr || !EnsureBuffer()) {
        return false;
    }
    RenderCurrentMonth();

    // The photo slideshow uses the same layer, so it has to step aside.
    display_->ShowSlide(buffer_, static_cast<size_t>(kWidth) * kHeight * 2, kWidth, kHeight,
                        kWidth * 2);
    visible_ = true;
    visible_seconds_ = 0;
    tool_turn_ = true;
    last_state_ = Application::GetInstance().GetDeviceState();
    ESP_LOGI(TAG, "Calendar shown");
    return true;
}

void CalendarView::Hide() {
    if (!visible_) {
        return;
    }
    visible_ = false;
    visible_seconds_ = 0;
    if (display_ != nullptr) {
        display_->HideSlide();
    }
    ESP_LOGI(TAG, "Calendar hidden");
}

void CalendarView::NotifyInteraction() {
    if (visible_) {
        Hide();
    }
}

void CalendarView::RegisterTools() {
    auto& server = McpServer::GetInstance();

    server.AddTool(
        "self.screen.show_calendar",
        "Show the calendar of the current month on the device's screen, in place of its eyes. "
        "Use this whenever the user asks to see the calendar, the date, what day today is, "
        "what month it is, or how many days the month has. The result is one of: `shown` (the "
        "calendar is on the screen; say so in one short sentence) or `unavailable` (this device "
        "cannot show a calendar).",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            if (!CalendarView::GetInstance().Show()) {
                cJSON* root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "status", "unavailable");
                return root;
            }
            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "shown");
            return root;
        });

    server.AddTool(
        "self.screen.hide_calendar",
        "Hide the calendar from the device's screen and bring its eyes back. Use when the user "
        "asks to close the calendar, hide it, or take it off the screen.",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            CalendarView::GetInstance().Hide();
            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "hidden");
            return root;
        });
}
