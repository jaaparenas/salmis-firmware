#ifndef SALMIS_SLIDESHOW_H_
#define SALMIS_SLIDESHOW_H_

#include <cstddef>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class CustomLcdDisplay;

// Shows the photos of the microSD card while the robot waits, instead of the eyes.
// The slideshow starts after a minute of uninterrupted idle and changes photo every
// kImageSeconds. Any interaction (wake word, touch, button) leaves it and brings the eyes back.
class SlideShow {
public:
    static constexpr int kIdleSecondsBeforeStart = 60;
    static constexpr int kImageSeconds = 8;
    static constexpr const char* kImageDirectory = "/sdcard/images";
    static constexpr const char* kAlternateImageDirectory = "/sdcard/imagenes";

    static SlideShow& GetInstance();

    void Start(CustomLcdDisplay* display);

    // Leaves the slideshow, if it is running, and restores the eyes.
    void NotifyInteraction();

private:
    SlideShow() = default;

    static void TaskEntry(void* arg);
    void Task();
    void ScanImages();
    bool ShowNextImage();
    bool ShowImage(size_t index);
    void Leave();

    CustomLcdDisplay* display_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::vector<std::string> images_;
    size_t current_ = 0;
    bool active_ = false;
    bool scanned_ = false;
    int idle_seconds_ = 0;
    int image_seconds_ = 0;
};

#endif  // SALMIS_SLIDESHOW_H_
