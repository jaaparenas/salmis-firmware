#include "slideshow.h"

#include <dirent.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <strings.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "application.h"
#include "custom_display.h"
#include "device_state.h"
#include "display/lvgl_display/jpg/jpeg_to_image.h"

#define TAG "SalmisSlide"

namespace {

constexpr int kScreenSize = 240;
constexpr int kDecodeSize = 640;
constexpr size_t kTaskStackSize = 8192;

bool HasJpegExtension(const std::string& name) {
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string ext = name.substr(dot);
    return strcasecmp(ext.c_str(), ".jpg") == 0 || strcasecmp(ext.c_str(), ".jpeg") == 0;
}

uint16_t PixelAt(const uint8_t* src, size_t stride, int x, int y) {
    const uint8_t* pixel = src + y * stride + x * 2;
    return static_cast<uint16_t>(pixel[0] | (pixel[1] << 8));
}

// Builds an RGB565 image with the center square of the source.
// The result is at most max_size pixels per side and is never enlarged: a photo smaller
// than the screen keeps its own size (sharp, centered), a bigger one is averaged down.
// Returns the side of the produced image.
int BuildSquare(const uint8_t* src, size_t stride, int width, int height, uint8_t* dst,
                int max_size) {
    int side = std::min(width, height);
    int offset_x = (width - side) / 2;
    int offset_y = (height - side) / 2;
    int out_size = std::min(side, max_size);
    float ratio = static_cast<float>(side) / out_size;

    for (int y = 0; y < out_size; y++) {
        for (int x = 0; x < out_size; x++) {
            float fx = offset_x + (x + 0.5f) * ratio - 0.5f;
            float fy = offset_y + (y + 0.5f) * ratio - 0.5f;
            uint16_t value = 0;

            if (ratio > 1.0f) {
                // Shrinking: average every source pixel of this target pixel, per channel.
                int x0 = static_cast<int>(floorf(fx));
                int y0 = static_cast<int>(floorf(fy));
                int x1 = static_cast<int>(ceilf(fx + ratio));
                int y1 = static_cast<int>(ceilf(fy + ratio));
                uint32_t sums[3] = {0, 0, 0};
                uint32_t count = 0;
                for (int sy = y0; sy < y1 && sy < height; sy++) {
                    for (int sx = x0; sx < x1 && sx < width; sx++) {
                        uint16_t pixel = PixelAt(src, stride, sx, sy);
                        sums[0] += pixel & 0x1F;
                        sums[1] += (pixel >> 5) & 0x3F;
                        sums[2] += (pixel >> 11) & 0x1F;
                        count++;
                    }
                }
                if (count > 0) {
                    value = static_cast<uint16_t>((sums[0] / count) | ((sums[1] / count) << 5) |
                                                   ((sums[2] / count) << 11));
                }
            } else {
                // Same size: copy the pixel as it is.
                int sx = std::min(width - 1, std::max(0, static_cast<int>(fx + 0.5f)));
                int sy = std::min(height - 1, std::max(0, static_cast<int>(fy + 0.5f)));
                value = PixelAt(src, stride, sx, sy);
            }

            uint8_t* out = dst + (y * out_size + x) * 2;
            out[0] = value & 0xFF;
            out[1] = value >> 8;
        }
    }
    return out_size;
}

}  // namespace

SlideShow& SlideShow::GetInstance() {
    static SlideShow instance;
    return instance;
}

void SlideShow::Start(CustomLcdDisplay* display) {
    display_ = display;
    if (task_ != nullptr) {
        return;
    }
    xTaskCreate(TaskEntry, "salmis_slide", kTaskStackSize, this, 3, &task_);
    ESP_LOGI(TAG, "Slideshow started");
}

void SlideShow::NotifyInteraction() {
    if (active_) {
        ESP_LOGI(TAG, "Interaction, leaving the slideshow");
        Leave();
    }
    idle_seconds_ = 0;
}

void SlideShow::TaskEntry(void* arg) {
    static_cast<SlideShow*>(arg)->Task();
    vTaskDelete(nullptr);
}

void SlideShow::Task() {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // The clock runs always, not only during the slideshow.
        if (display_ != nullptr) {
            time_t now = time(nullptr);
            struct tm local{};
            localtime_r(&now, &local);
            char clock[8];
            strftime(clock, sizeof(clock), "%H:%M", &local);
            display_->UpdateClock(clock);
        }

        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
            idle_seconds_ = 0;
            if (active_) {
                Leave();
            }
            continue;
        }

        idle_seconds_++;
        if (!active_) {
            if (idle_seconds_ < kIdleSecondsBeforeStart) {
                continue;
            }
            if (!scanned_) {
                ScanImages();
            }
            if (images_.empty()) {
                // Nothing to show: check again in a minute instead of every second.
                idle_seconds_ = 0;
                continue;
            }
            if (ShowNextImage()) {
                active_ = true;
                image_seconds_ = 0;
            } else {
                idle_seconds_ = 0;
            }
            continue;
        }

        image_seconds_++;
        if (image_seconds_ >= kImageSeconds) {
            image_seconds_ = 0;
            if (!ShowNextImage()) {
                Leave();
            }
        }
    }
}

void SlideShow::ScanImages() {
    images_.clear();
    const char* directories[] = {kImageDirectory, kAlternateImageDirectory};
    for (const char* directory : directories) {
        DIR* dir = opendir(directory);
        if (dir == nullptr) {
            continue;
        }
        while (auto* entry = readdir(dir)) {
            std::string name = entry->d_name;
            if (name.empty() || name[0] == '.') {
                continue;
            }
            if (HasJpegExtension(name)) {
                images_.push_back(std::string(directory) + "/" + name);
            } else {
                ESP_LOGW(TAG, "Skipping %s/%s: only JPEG is supported", directory, name.c_str());
            }
        }
        closedir(dir);
    }
    std::sort(images_.begin(), images_.end());
    scanned_ = true;
    if (images_.empty()) {
        ESP_LOGW(TAG, "No JPEG photos in %s or %s", kImageDirectory, kAlternateImageDirectory);
    } else {
        ESP_LOGI(TAG, "%u photos found", static_cast<unsigned>(images_.size()));
    }
}

bool SlideShow::ShowNextImage() {
    if (images_.empty() || display_ == nullptr) {
        return false;
    }
    size_t start = esp_random() % images_.size();
    for (size_t i = 0; i < images_.size(); i++) {
        size_t candidate = (start + i) % images_.size();
        if (images_.size() > 1 && candidate == current_) {
            continue;
        }
        if (ShowImage(candidate)) {
            current_ = candidate;
            return true;
        }
    }
    return false;
}

bool SlideShow::ShowImage(size_t index) {
    const std::string& path = images_[index];

    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        ESP_LOGW(TAG, "Cannot open %s", path.c_str());
        return false;
    }
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size <= 0) {
        fclose(file);
        return false;
    }

    uint8_t* jpeg = static_cast<uint8_t*>(
        heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (jpeg == nullptr) {
        jpeg = static_cast<uint8_t*>(heap_caps_malloc(file_size, MALLOC_CAP_8BIT));
    }
    if (jpeg == nullptr) {
        ESP_LOGE(TAG, "No memory to read %s", path.c_str());
        fclose(file);
        return false;
    }
    size_t read = fread(jpeg, 1, file_size, file);
    fclose(file);
    if (read != static_cast<size_t>(file_size)) {
        ESP_LOGW(TAG, "Short read on %s", path.c_str());
        heap_caps_free(jpeg);
        return false;
    }

    uint8_t* decoded = nullptr;
    size_t decoded_size = 0;
    size_t width = 0;
    size_t height = 0;
    size_t stride = 0;
    // Decode larger than the screen so the shrink to 240x240 averages real detail.
    esp_err_t ret = jpeg_to_image_fit(jpeg, read, kDecodeSize, kDecodeSize, &decoded, &decoded_size,
                                       &width, &height, &stride);
    heap_caps_free(jpeg);
    if (ret != ESP_OK || decoded == nullptr) {
        ESP_LOGW(TAG, "Cannot decode %s: %s", path.c_str(), esp_err_to_name(ret));
        return false;
    }

    // A photo bigger than the screen fills it; a smaller one keeps its size and stays sharp.
    uint8_t* screen = static_cast<uint8_t*>(
        heap_caps_malloc(kScreenSize * kScreenSize * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (screen == nullptr) {
        screen = static_cast<uint8_t*>(heap_caps_malloc(kScreenSize * kScreenSize * 2, MALLOC_CAP_8BIT));
    }
    if (screen == nullptr) {
        ESP_LOGE(TAG, "No memory for the photo");
        heap_caps_free(decoded);
        return false;
    }
    int size = BuildSquare(decoded, stride, width, height, screen, kScreenSize);
    heap_caps_free(decoded);

    display_->ShowSlide(screen, static_cast<size_t>(size) * size * 2, size, size, size * 2);
    heap_caps_free(screen);

    ESP_LOGI(TAG, "Showing %s", path.c_str());
    return true;
}

void SlideShow::Leave() {
    active_ = false;
    image_seconds_ = 0;
    idle_seconds_ = 0;
    if (display_ != nullptr) {
        display_->HideSlide();
    }
}
