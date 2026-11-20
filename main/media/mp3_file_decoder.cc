#include "mp3_file_decoder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include <esp_log.h>

#include "esp_ae_rate_cvt.h"
#include "esp_audio_simple_dec.h"
#include "esp_mp3_dec.h"
#include "esp_opus_enc.h"

namespace {
const char* TAG = "Mp3FileDecoder";
constexpr size_t kReadChunkBytes = 4096;
constexpr uint32_t kOpusBitrate = 64000;

struct FileCloser {
    void operator()(FILE* file) const {
        if (file != nullptr) {
            fclose(file);
        }
    }
};

struct DecoderCloser {
    void operator()(void* handle) const {
        if (handle != nullptr) {
            esp_audio_simple_dec_close(handle);
        }
    }
};

struct EncoderCloser {
    void operator()(void* handle) const {
        if (handle != nullptr) {
            esp_opus_enc_close(handle);
        }
    }
};

struct ResamplerCloser {
    void operator()(void* handle) const {
        if (handle != nullptr) {
            esp_ae_rate_cvt_close(handle);
        }
    }
};

void SkipId3v2(FILE* file) {
    uint8_t header[10];
    size_t read = fread(header, 1, sizeof(header), file);
    if (read == sizeof(header) && memcmp(header, "ID3", 3) == 0) {
        uint32_t tag_size = ((header[6] & 0x7F) << 21) | ((header[7] & 0x7F) << 14) |
                            ((header[8] & 0x7F) << 7) | (header[9] & 0x7F);
        fseek(file, 10 + tag_size, SEEK_SET);
    } else {
        fseek(file, 0, SEEK_SET);
    }
}
}  // namespace

bool Mp3FileDecoder::Decode(const std::string& path, const PacketCallback& on_packet,
                            const CancelCheck& is_cancelled) {
    std::unique_ptr<FILE, FileCloser> file(fopen(path.c_str(), "rb"));
    if (!file) {
        ESP_LOGE(TAG, "Cannot open %s", path.c_str());
        return false;
    }
    SkipId3v2(file.get());

    static bool decoder_registered = false;
    if (!decoder_registered) {
        esp_mp3_dec_register();
        decoder_registered = true;
    }

    esp_audio_simple_dec_cfg_t dec_cfg = {};
    dec_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    dec_cfg.use_frame_dec = false;
    esp_audio_simple_dec_handle_t dec_handle = nullptr;
    if (esp_audio_simple_dec_open(&dec_cfg, &dec_handle) != ESP_AUDIO_ERR_OK || !dec_handle) {
        ESP_LOGE(TAG, "Failed to open the MP3 decoder");
        return false;
    }
    std::unique_ptr<void, DecoderCloser> decoder(dec_handle);

    esp_opus_enc_config_t enc_cfg = {
        .sample_rate = kSampleRate,
        .channel = ESP_AUDIO_MONO,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .bitrate = static_cast<int>(kOpusBitrate),
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
        .complexity = 0,
        .enable_fec = false,
        .enable_dtx = false,
        .enable_vbr = true,
    };
    void* enc_handle = nullptr;
    if (esp_opus_enc_open(&enc_cfg, sizeof(enc_cfg), &enc_handle) != ESP_AUDIO_ERR_OK ||
        !enc_handle) {
        ESP_LOGE(TAG, "Failed to open the Opus encoder");
        return false;
    }
    std::unique_ptr<void, EncoderCloser> encoder(enc_handle);

    esp_audio_enc_frame_info_t frame_info = {};
    esp_opus_enc_get_frame_info_by_cfg(&enc_cfg, &frame_info);
    const size_t frame_samples = frame_info.in_frame_size / sizeof(int16_t);

    std::vector<uint8_t> read_buffer(kReadChunkBytes);
    std::vector<int16_t> decoded(4096);
    std::vector<int16_t> mono;
    std::vector<int16_t> resampled;
    std::vector<int16_t> frame(frame_samples);
    std::vector<uint8_t> encoded(frame_info.out_frame_size * 2);
    size_t frame_fill = 0;

    std::unique_ptr<void, ResamplerCloser> resampler;
    int source_rate = 0;
    int source_channels = 1;
    bool stopped = false;

    auto emit_frame = [&]() {
        esp_audio_enc_in_frame_t in = {};
        in.buffer = reinterpret_cast<uint8_t*>(frame.data());
        in.len = static_cast<uint32_t>(frame_samples * sizeof(int16_t));
        esp_audio_enc_out_frame_t out = {};
        out.buffer = encoded.data();
        out.len = static_cast<uint32_t>(encoded.size());
        if (esp_opus_enc_process(encoder.get(), &in, &out) == ESP_AUDIO_ERR_OK &&
            out.encoded_bytes > 0) {
            if (!on_packet(encoded.data(), out.encoded_bytes)) {
                stopped = true;
            }
        }
        frame_fill = 0;
    };

    auto push_pcm = [&](const int16_t* data, size_t count) {
        while (count > 0 && !stopped) {
            size_t chunk = std::min(count, frame_samples - frame_fill);
            memcpy(frame.data() + frame_fill, data, chunk * sizeof(int16_t));
            frame_fill += chunk;
            data += chunk;
            count -= chunk;
            if (frame_fill == frame_samples) {
                emit_frame();
            }
        }
    };

    int consecutive_errors = 0;
    while (!stopped && !is_cancelled()) {
        size_t bytes_read = fread(read_buffer.data(), 1, read_buffer.size(), file.get());
        if (bytes_read == 0) {
            break;
        }

        const uint8_t* cursor = read_buffer.data();
        size_t remaining = bytes_read;
        while (remaining > 0 && !stopped && !is_cancelled()) {
            esp_audio_simple_dec_raw_t raw = {};
            raw.buffer = const_cast<uint8_t*>(cursor);
            raw.len = static_cast<uint32_t>(remaining);
            raw.eos = false;

            esp_audio_simple_dec_out_t out = {};
            out.buffer = reinterpret_cast<uint8_t*>(decoded.data());
            out.len = static_cast<uint32_t>(decoded.size() * sizeof(int16_t));

            esp_audio_err_t ret = esp_audio_simple_dec_process(decoder.get(), &raw, &out);
            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                decoded.resize(decoded.size() * 2);
                continue;
            }
            if (ret != ESP_AUDIO_ERR_OK) {
                if (++consecutive_errors > 20) {
                    ESP_LOGE(TAG, "Too many MP3 decode errors, giving up");
                    return false;
                }
                size_t skip = raw.consumed > 0 ? raw.consumed : 1;
                cursor += skip;
                remaining -= std::min(skip, remaining);
                continue;
            }
            consecutive_errors = 0;
            if (raw.consumed == 0) {
                break;
            }
            cursor += raw.consumed;
            remaining -= std::min<size_t>(raw.consumed, remaining);

            if (source_rate == 0 && out.decoded_size > 0) {
                esp_audio_simple_dec_info_t info = {};
                if (esp_audio_simple_dec_get_info(decoder.get(), &info) == ESP_AUDIO_ERR_OK &&
                    info.sample_rate > 0) {
                    source_rate = static_cast<int>(info.sample_rate);
                    source_channels = info.channel > 0 ? info.channel : 1;
                    ESP_LOGI(TAG, "MP3: %d Hz, %d channel(s)", source_rate, source_channels);
                    if (source_rate != kSampleRate) {
                        esp_ae_rate_cvt_cfg_t cvt_cfg = {
                            .src_rate = static_cast<uint32_t>(source_rate),
                            .dest_rate = static_cast<uint32_t>(kSampleRate),
                            .channel = 1,
                            .bits_per_sample = ESP_AUDIO_BIT16,
                            .complexity = 2,
                            .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,
                        };
                        esp_ae_rate_cvt_handle_t handle = nullptr;
                        if (esp_ae_rate_cvt_open(&cvt_cfg, &handle) != ESP_AE_ERR_OK || !handle) {
                            ESP_LOGE(TAG, "Failed to open the resampler");
                            return false;
                        }
                        resampler.reset(handle);
                    }
                }
            }
            if (out.decoded_size == 0 || source_rate == 0) {
                continue;
            }

            size_t total_samples = out.decoded_size / sizeof(int16_t);
            size_t frames = total_samples / source_channels;
            mono.resize(frames);
            if (source_channels >= 2) {
                for (size_t i = 0; i < frames; i++) {
                    mono[i] = static_cast<int16_t>(
                        (static_cast<int32_t>(decoded[i * source_channels]) +
                         decoded[i * source_channels + 1]) >> 1);
                }
            } else {
                memcpy(mono.data(), decoded.data(), frames * sizeof(int16_t));
            }

            if (resampler) {
                uint32_t max_out = 0;
                esp_ae_rate_cvt_get_max_out_sample_num(resampler.get(),
                                                       static_cast<uint32_t>(frames), &max_out);
                resampled.resize(max_out);
                uint32_t actual_out = max_out;
                if (esp_ae_rate_cvt_process(resampler.get(),
                                            reinterpret_cast<esp_ae_sample_t>(mono.data()),
                                            static_cast<uint32_t>(frames),
                                            reinterpret_cast<esp_ae_sample_t>(resampled.data()),
                                            &actual_out) == ESP_AE_ERR_OK) {
                    push_pcm(resampled.data(), actual_out);
                }
            } else {
                push_pcm(mono.data(), frames);
            }
        }
    }

    if (stopped || is_cancelled()) {
        return false;
    }
    if (frame_fill > 0) {
        std::fill(frame.begin() + frame_fill, frame.end(), 0);
        emit_frame();
    }
    return !stopped;
}
