#ifndef SALMIS_CUSTOM_DISPLAY_H_
#define SALMIS_CUSTOM_DISPLAY_H_

#include <cstddef>
#include <cstdint>

#include <lvgl.h>

#include "display/lcd_display.h"

// Round-screen tweaks for the Salmis robot, plus the full-screen photo layer used by the
// idle slideshow. The photos replace the eyes on screen; HideSlide() brings the eyes back.
class CustomLcdDisplay : public SpiLcdDisplay {
public:
    CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_handle_t panel_handle,
                     int width, int height, int offset_x, int offset_y, bool mirror_x,
                     bool mirror_y, bool swap_xy);
    ~CustomLcdDisplay() override;

    virtual void SetupUI() override;

    // Shows an RGB565 image centered on the screen and hides the eyes.
    // The data is copied, so the caller keeps the ownership of its buffer.
    void ShowSlide(const uint8_t* rgb565, size_t size, int width, int height, int stride);

    // Hides the photo and shows the eyes again.
    void HideSlide();

    // Shows the clock over the photo, with a transparent background.
    void UpdateClock(const char* text);
    void HideClock();

    bool SlideVisible() const { return slide_visible_; }

private:
    void FreeSlideBuffer();

    lv_obj_t* slide_image_ = nullptr;
    lv_obj_t* clock_label_ = nullptr;
    lv_img_dsc_t slide_dsc_{};
    uint8_t* slide_buffer_ = nullptr;
    size_t slide_buffer_size_ = 0;
    bool slide_visible_ = false;
};

#endif  // SALMIS_CUSTOM_DISPLAY_H_
