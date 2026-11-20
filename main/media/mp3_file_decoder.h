#ifndef MP3_FILE_DECODER_H_
#define MP3_FILE_DECODER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// Streams an MP3 file as 24 kHz mono Opus packets with 60 ms frames, the format
// consumed by AudioService's decode queue. The file is read in small chunks and
// never loaded fully into memory.
class Mp3FileDecoder {
public:
    static constexpr int kSampleRate = 24000;
    static constexpr int kFrameDurationMs = 60;

    // Return false to stop decoding.
    using PacketCallback = std::function<bool(const uint8_t* data, size_t size)>;
    using CancelCheck = std::function<bool()>;

    // Returns true when the whole file was decoded and delivered, false on error,
    // cancellation, or when the packet callback asked to stop.
    static bool Decode(const std::string& path, const PacketCallback& on_packet,
                       const CancelCheck& is_cancelled);
};

#endif  // MP3_FILE_DECODER_H_
