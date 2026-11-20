#include "custom_display.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <cstring>

#include "application.h"
#include "assets/lang_config.h"
#include "device_state.h"
#include "display/lvgl_display/lvgl_theme.h"

#define TAG "SalmisDisplay"

namespace {

// The face for each mood. Both are among the expressions of the board.
constexpr const char* kListeningFace = "surprised";
constexpr const char* kMusicFace = "cool";
constexpr const char* kNeutralFace = "neutral";

}  // namespace

CustomLcdDisplay::CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle,
                                   esp_lcd_panel_handle_t panel_handle, int width, int height,
                                   int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                                   bool swap_xy)
    : SpiLcdDisplay(io_handle, panel_handle, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {
    // UI customization belongs in SetupUI(), not in the constructor, so the lvgl objects
    // already exist when it runs.
}

CustomLcdDisplay::~CustomLcdDisplay() {
    FreeSlideBuffer();
}

void CustomLcdDisplay::SetupUI() {
    // Call parent SetupUI() first to create all lvgl objects
    SpiLcdDisplay::SetupUI();

    DisplayLockGuard lock(this);
    // The screen is round, so the top bar needs side padding, otherwise the circle cuts the
    // Wi-Fi and battery icons. It also needs a top padding: at the very top of the circle
    // there is almost no room, and the icons were being clipped.
    if (top_bar_ != nullptr) {
        lv_obj_set_style_pad_left(top_bar_, LV_HOR_RES * 0.32, 0);
        lv_obj_set_style_pad_right(top_bar_, LV_HOR_RES * 0.32, 0);
        lv_obj_set_style_pad_top(top_bar_, 10, 0);
    }
    ESP_LOGI(TAG, "theme=%s", static_cast<LvglTheme*>(current_theme_)->name().c_str());

    // The clock sits in the free middle of the status bar. Its font is left inherited from
    // the screen on purpose: the assets replace the theme font at runtime and free the old
    // one, and only the styles the display knows about are rebound to the new font.
    clock_label_ = lv_label_create(lv_screen_active());
    auto theme = static_cast<LvglTheme*>(current_theme_);
    // The letters take the colour that contrasts with the bar, and the halo takes the
    // background colour, so the clock reads over the band and over the photo.
    lv_obj_set_style_text_color(clock_label_, theme->text_color(), 0);
    lv_obj_set_style_text_outline_stroke_width(clock_label_, 2, 0);
    lv_obj_set_style_text_outline_stroke_color(clock_label_, theme->background_color(), 0);
    lv_obj_set_style_text_outline_stroke_opa(clock_label_, LV_OPA_80, 0);
    lv_obj_set_style_bg_opa(clock_label_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_label_, 0, 0);
    lv_obj_align(clock_label_, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_add_flag(clock_label_, LV_OBJ_FLAG_HIDDEN);
    // The clock goes on top of the status bar and of the photo.
    lv_obj_move_foreground(top_bar_);
    lv_obj_move_foreground(clock_label_);

    // The hint at the bottom says what the robot is doing, the same way the clock says
    // the time. It inherits the font from the screen like the clock does, for the same
    // reason: the theme font is replaced at runtime and a label that keeps its own
    // pointer would end up pointing at released memory.
    hint_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_color(hint_label_, theme->text_color(), 0);
    lv_obj_set_style_text_outline_stroke_width(hint_label_, 2, 0);
    lv_obj_set_style_text_outline_stroke_color(hint_label_, theme->background_color(), 0);
    lv_obj_set_style_text_outline_stroke_opa(hint_label_, LV_OPA_80, 0);
    lv_obj_set_style_bg_opa(hint_label_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hint_label_, 0, 0);
    // The bottom of a round screen is narrow, so the hint sits a little above the edge.
    lv_obj_align(hint_label_, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
}

void CustomLcdDisplay::SetMood(Mood mood) {
    if (mood_ == mood) {
        return;
    }
    mood_ = mood;
    switch (mood) {
        case Mood::kListening:
            ApplyFace(kListeningFace);
            ShowHint(Lang::Strings::LISTENING);
            break;
        case Mood::kMusic:
            ApplyFace(kMusicFace);
            HideHint();
            break;
        case Mood::kNormal:
            HideHint();
            ApplyFace(kNeutralFace);
            break;
    }
    ESP_LOGI(TAG, "mood=%d", static_cast<int>(mood_));
}

void CustomLcdDisplay::ApplyFace(const char* name) {
    // Goes straight to the base class, so the mood can set the face without going
    // through the check that keeps the server from replacing it.
    LcdDisplay::SetEmotion(name);
}

void CustomLcdDisplay::ShowHint(const char* text) {
    DisplayLockGuard lock(this);
    if (hint_label_ == nullptr) {
        return;
    }
    lv_label_set_text(hint_label_, text);
    lv_obj_remove_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(hint_label_);
    if (status_label_ != nullptr) {
        // The upstream status text sits exactly where the clock is. While the hint
        // is up, the top belongs to the clock and the bottom to the hint.
        lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void CustomLcdDisplay::HideHint() {
    DisplayLockGuard lock(this);
    if (hint_label_ == nullptr) {
        return;
    }
    lv_obj_add_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
    if (status_label_ != nullptr) {
        lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool CustomLcdDisplay::SetEmotion(const char* emotion) {
    if (mood_ != Mood::kNormal) {
        // The board holds the face while a mood is active, so the expression the server
        // asks for does not replace it.
        return true;
    }
    return LcdDisplay::SetEmotion(emotion);
}

void CustomLcdDisplay::SetStatus(const char* status) {
    // Listening and playing music both say "Speaking...", so the device state is what
    // tells them apart.
    switch (Application::GetInstance().GetDeviceState()) {
        case kDeviceStateListening:
            SetMood(Mood::kListening);
            break;
        case kDeviceStateNotifying:
            SetMood(Mood::kMusic);
            break;
        default:
            SetMood(Mood::kNormal);
            break;
    }
    LcdDisplay::SetStatus(status);
}

void CustomLcdDisplay::FreeSlideBuffer() {
    if (slide_buffer_ != nullptr) {
        heap_caps_free(slide_buffer_);
        slide_buffer_ = nullptr;
    }
    slide_buffer_size_ = 0;
}

void CustomLcdDisplay::ShowSlide(const uint8_t* rgb565, size_t size, int width, int height,
                                 int stride) {
    if (rgb565 == nullptr || width <= 0 || height <= 0) {
        return;
    }
    size_t needed = static_cast<size_t>(width) * height * 2;
    if (size < needed) {
        ESP_LOGW(TAG, "Slide data too small: %u < %u", static_cast<unsigned>(size),
                 static_cast<unsigned>(needed));
        return;
    }

    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }

    if (slide_buffer_ == nullptr || slide_buffer_size_ < needed) {
        FreeSlideBuffer();
        slide_buffer_ = static_cast<uint8_t*>(
            heap_caps_malloc(needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (slide_buffer_ == nullptr) {
            slide_buffer_ = static_cast<uint8_t*>(heap_caps_malloc(needed, MALLOC_CAP_8BIT));
        }
        if (slide_buffer_ == nullptr) {
            ESP_LOGE(TAG, "No memory for the photo (%u bytes)", static_cast<unsigned>(needed));
            return;
        }
        slide_buffer_size_ = needed;
    }

    int row_bytes = width * 2;
    for (int y = 0; y < height; y++) {
        memcpy(slide_buffer_ + y * row_bytes, rgb565 + y * stride, row_bytes);
    }

    bzero(&slide_dsc_, sizeof(slide_dsc_));
    slide_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    slide_dsc_.header.w = width;
    slide_dsc_.header.h = height;
    slide_dsc_.header.stride = width * 2;
    slide_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    slide_dsc_.data_size = needed;
    slide_dsc_.data = slide_buffer_;

    if (slide_image_ == nullptr) {
        slide_image_ = lv_image_create(lv_screen_active());
        lv_obj_align(slide_image_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(slide_image_, LV_OBJ_FLAG_HIDDEN);
    }

    lv_image_set_src(slide_image_, &slide_dsc_);
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (hint_label_ != nullptr) {
        // The photo or the calendar covers the screen, the hint has no room.
        lv_obj_add_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(slide_image_, LV_OBJ_FLAG_HIDDEN);
    // The photo covers the screen, so the status bar and the clock go back on top.
    if (top_bar_ != nullptr) {
        lv_obj_move_foreground(top_bar_);
    }
    if (clock_label_ != nullptr) {
        lv_obj_move_foreground(clock_label_);
    }
    slide_visible_ = true;
}

void CustomLcdDisplay::HideSlide() {
    DisplayLockGuard lock(this);
    if (!lock || !slide_visible_) {
        return;
    }
    slide_visible_ = false;
    if (slide_image_ != nullptr) {
        lv_obj_add_flag(slide_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (hint_label_ != nullptr && mood_ == Mood::kListening) {
        // The robot is still listening, so the hint comes back with the eyes.
        lv_obj_remove_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(hint_label_);
    }
    ShowEmotion();
}

void CustomLcdDisplay::UpdateClock(const char* text) {
    if (clock_label_ == nullptr || text == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    lv_label_set_text(clock_label_, text);
    lv_obj_remove_flag(clock_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(clock_label_);
}

void CustomLcdDisplay::HideClock() {
    if (clock_label_ == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    lv_obj_add_flag(clock_label_, LV_OBJ_FLAG_HIDDEN);
}
