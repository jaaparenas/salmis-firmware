#ifndef SALMIS_CUSTOM_DISPLAY_H_
#define SALMIS_CUSTOM_DISPLAY_H_

#include <cstddef>
#include <cstdint>

#include <lvgl.h>

#include "display/lcd_display.h"

// Round-screen tweaks for the Salmis robot, plus the full-screen photo layer used by the
// idle slideshow. The photos replace the eyes on screen; HideSlide() brings the eyes back.
//
// The board also owns the face while the device is doing something: it shows a "surprised"
// face with a "Listening..." hint at the bottom while the robot is listening, and a "cool"
// face while it plays music from the card. The expression the server asks for does not
// replace those, so the face always says what the robot is doing.
class CustomLcdDisplay : public SpiLcdDisplay {
public:
    enum class Mood {
        kNormal,    // the server drives the face
        kListening, // the robot is listening
        kMusic,     // the robot is playing music from the card
    };

    CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_handle_t panel_handle,
                     int width, int height, int offset_x, int offset_y, bool mirror_x,
                     bool mirror_y, bool swap_xy);
    ~CustomLcdDisplay() override;

    virtual void SetupUI() override;

    // The board picks the face from the device state, and keeps it while a mood
    // is active.
    virtual void SetStatus(const char* status) override;
    virtual bool SetEmotion(const char* emotion) override;

    // Shows an RGB565 image centered on the screen and hides the eyes.
    // The data is copied, so the caller keeps the ownership of its buffer.
    void ShowSlide(const uint8_t* rgb565, size_t size, int width, int height, int stride);

    // Hides the photo and shows the eyes again.
    void HideSlide();

    // Shows the clock over the photo, with a transparent background.
    void UpdateClock(const char* text);
    void HideClock();

    bool SlideVisible() const { return slide_visible_; }
    Mood GetMood() const { return mood_; }

private:
    void FreeSlideBuffer();
    void SetMood(Mood mood);
    // Applies a face without going through the mood check.
    void ApplyFace(const char* name);
    void ShowHint(const char* text);
    void HideHint();

    lv_obj_t* slide_image_ = nullptr;
    lv_obj_t* clock_label_ = nullptr;
    lv_obj_t* hint_label_ = nullptr;
    lv_img_dsc_t slide_dsc_{};
    uint8_t* slide_buffer_ = nullptr;
    size_t slide_buffer_size_ = 0;
    bool slide_visible_ = false;
    Mood mood_ = Mood::kNormal;
};

#endif  // SALMIS_CUSTOM_DISPLAY_H_

