#include "media_policy.h"

#include <esp_log.h>

#include "media_library.h"
#include "settings.h"

namespace media {
namespace {

const char* TAG = "MediaPolicy";

// NVS keys are limited to 15 characters.
constexpr const char* kSettingsNamespace = "media";
constexpr const char* kAllowRemoteKey = "allow_remote";

bool IsRemoteUrl(const std::string& url) {
    return url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
}

}  // namespace

bool ShouldRefuseCloudAudio(const std::string& audio_url) {
    // Local files never reach this path, and only remote audio is affected.
    if (!IsRemoteUrl(audio_url)) {
        return false;
    }

    Settings settings(kSettingsNamespace);
    if (settings.GetBool(kAllowRemoteKey, false)) {
        return false;
    }

    auto& library = MediaLibrary::GetInstance();
    if (!library.IsScanned()) {
        // The library is still being indexed. Refuse in the meantime instead
        // of letting cloud audio through during that window.
        ESP_LOGW(TAG, "Refusing cloud audio, the local library is still being indexed");
        return true;
    }

    // No local library means there is nothing else to play, so the cloud is
    // allowed to play instead of leaving the device mute.
    if (library.CachedCount() == 0) {
        return false;
    }

    ESP_LOGW(TAG, "Refusing cloud audio, the local library is the only music source: %s",
             audio_url.c_str());
    return true;
}

}  // namespace media
